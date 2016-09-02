#include "model.h"
#include "items/serveritem.h"
#include <QDebug>
#include <QSettings>
#include <algorithm>
#include <QWeakPointer>

using namespace ConnectionsTree;

Model::Model(QObject *parent) :
    QAbstractItemModel(parent),
    m_rawPointers(new QHash<TreeItem*, QWeakPointer<TreeItem>>())
{
    QObject::connect(this, &Model::itemChanged, this, &Model::onItemChanged);
    QObject::connect(this, &Model::itemChildsLoaded, this, &Model::onItemChildsLoaded);
    QObject::connect(this, &Model::itemChildsUnloaded, this, &Model::onItemChildsUnloaded);

    qRegisterMetaType<QWeakPointer<TreeItem>>("QWeakPointer<TreeItem>");

}

QVariant Model::data(const QModelIndex &index, int role) const
{
    const TreeItem *item = getItemFromIndex(index);

    if (item == nullptr)
        return QVariant();

    switch (role) {
        case itemName: return item->getDisplayName();
        case Qt::DecorationRole: return item->getIconUrl();
        case itemType: return item->getType();
        case itemOriginalName: return item->getName();
    }

    return QVariant();
}

QHash<int, QByteArray> Model::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[itemName] = "name";
    roles[itemType] = "type";
    return roles;
}

Qt::ItemFlags Model::flags(const QModelIndex &index) const
{
    const TreeItem *item = getItemFromIndex(index);

    if (item == nullptr)    
        return Qt::NoItemFlags;

    Qt::ItemFlags result = Qt::ItemIsSelectable;

    if (item->isEnabled())
        result |= Qt::ItemIsEnabled;

    return result;
}

QModelIndex Model::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    const TreeItem* parentItem = getItemFromIndex(parent);
    QSharedPointer<TreeItem> childItem;

    // get item from root items
    if (parentItem) {
        childItem = parentItem->child(row);
    } else if (row < m_treeItems.size()) {
        childItem = m_treeItems.at(row);
    }

    if (childItem.isNull())
        return QModelIndex();
    else {
        m_rawPointers->insert(childItem.data(), childItem.toWeakRef());
        return createIndex(row, column, childItem.data());
    }
}

QModelIndex Model::parent(const QModelIndex &index) const
{
    const TreeItem *childItem = getItemFromIndex(index);
    
    if (!childItem)
        return QModelIndex();

    QWeakPointer<TreeItem> parentItem = childItem->parent();

    if (!parentItem)
        return QModelIndex();

    m_rawPointers->insert(parentItem.data(), parentItem);
    return createIndex(parentItem.toStrongRef()->row(), 0, (void*)parentItem.data());
}

int Model::rowCount(const QModelIndex &parent) const
{
    const TreeItem* parentItem = getItemFromIndex(parent);
    
    if (!parentItem)
        return m_treeItems.size();

    if (parent.column() > 0)
        return 0;

    return parentItem->childCount();
}

QModelIndex Model::getIndexFromItem(QWeakPointer<TreeItem> item)
{
    if (item && item.toStrongRef()) {
        return createIndex(item.toStrongRef()->row(), 0, (void*)item.data());
    }
    return QModelIndex();
}

bool Model::canFetchMore(const QModelIndex &parent) const
{
    TreeItem* i = getItemFromIndex(parent);

    return i && i->canFetchMore();
}

void Model::fetchMore(const QModelIndex &parent)
{
    TreeItem* i = getItemFromIndex(parent);

    if (!i)
        return;

    i->fetchMore();
}

void Model::onItemChanged(QWeakPointer<TreeItem> item)
{
    if (!item)
        return;

    auto index = getIndexFromItem(item);

    if (!index.isValid() || item.toStrongRef()->childCount() == 0)
        return;

    emit dataChanged(index, index);
}

void Model::onItemChildsLoaded(QWeakPointer<TreeItem> item)
{
    if (!item)
        return;

    auto index = getIndexFromItem(item);

    if (!index.isValid())
        return;

    QSharedPointer<TreeItem> treeItem = item.toStrongRef();

    emit beginInsertRows(index, 0, treeItem->childCount() - 1);
    emit endInsertRows();

    if (treeItem->getType() == "database") {
        emit expand(index);

        QSettings settings;
        if (settings.value("app/reopenNamespacesOnReload", true).toBool()) {
            restoreOpenedNamespaces(index);
        } else {
            qDebug() << "Namespace reopening is disabled in settings";
            m_expanded.clear();
        }
    }
}

void Model::onItemChildsUnloaded(QWeakPointer<TreeItem> item)
{
    if (!item)
        return;

    auto index = getIndexFromItem(item);

    if (!index.isValid())
        return;

    emit beginRemoveRows(index, 0, item.toStrongRef()->childCount() - 1);
    emit endRemoveRows();
}


QVariant Model::getItemIcon(const QModelIndex &index)
{
    return data(index, Qt::DecorationRole);
}

QVariant Model::getItemType(const QModelIndex &index)
{
    return data(index, itemType);
}

QVariant Model::getMetadata(const QModelIndex &index, const QString &metaKey)
{
    TreeItem *item = getItemFromIndex(index);

    if (item == nullptr)
        return QVariant();

    return item->metadata(metaKey);
}

void Model::setMetadata(const QModelIndex &index, const QString &metaKey, QVariant value)
{
    TreeItem *item = getItemFromIndex(index);

    if (item == nullptr)
        return;

    item->setMetadata(metaKey, value);
}

void Model::sendEvent(const QModelIndex &index, QString event)
{
    qDebug() << "Event recieved:" << event;

    TreeItem * item = getItemFromIndex(index);

    if (item)
        item->handleEvent(event);
}

unsigned int Model::size()
{
    return m_treeItems.size();
}

void Model::setExpanded(const QModelIndex &index)
{
    TreeItem * item = getItemFromIndex(index);

    if (!item || item->getType() != "namespace")
        return;

    m_expanded.insert(item->getName());
}

void Model::setCollapsed(const QModelIndex &index)
{
    TreeItem * item = getItemFromIndex(index);

    if (!item || item->getType() != "namespace")
        return;    

    m_expanded.remove(item->getName());
}

void Model::addRootItem(QSharedPointer<ServerItem> serverItem)
{
    if (serverItem.isNull())
        return;

    int insertIndex = m_treeItems.size();

    emit beginInsertRows(QModelIndex(), insertIndex, insertIndex);

    serverItem->setRow(insertIndex);
    serverItem->setWeakPointer(serverItem.toWeakRef());

    m_treeItems.push_back(serverItem);

    emit endInsertRows();
}

void Model::removeRootItem(QSharedPointer<ServerItem> item)
{
    if (!item)
        return;

    beginRemoveRows(QModelIndex(), item->row(), item->row());
    m_treeItems.removeAll(item);
    endRemoveRows();
}

void Model::restoreOpenedNamespaces(const QModelIndex &dbIndex)
{        
    QSet<QByteArray> expandedCache = m_expanded;
    m_expanded.clear();

    QModelIndex searchFrom = index(0, 0, dbIndex);

    foreach (QByteArray item, expandedCache)
    {        
        QModelIndexList matches = match(searchFrom, itemOriginalName, item, -1,
                                        Qt::MatchFixedString | Qt::MatchCaseSensitive | Qt::MatchRecursive);

        foreach (QModelIndex i, matches)
        {            
            emit expand(i);
        }
    }
}
