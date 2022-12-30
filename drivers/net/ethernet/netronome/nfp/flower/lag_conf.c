// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#include "main.h"

/* LAG group config flags. */
#define NFP_FL_LAG_LAST			BIT(1)
#define NFP_FL_LAG_FIRST		BIT(2)
#define NFP_FL_LAG_DATA			BIT(3)
#define NFP_FL_LAG_XON			BIT(4)
#define NFP_FL_LAG_SYNC			BIT(5)
#define NFP_FL_LAG_SWITCH		BIT(6)
#define NFP_FL_LAG_RESET		BIT(7)

/* LAG port state flags. */
#define NFP_PORT_LAG_LINK_UP		BIT(0)
#define NFP_PORT_LAG_TX_ENABLED		BIT(1)
#define NFP_PORT_LAG_CHANGED		BIT(2)

enum nfp_fl_lag_batch {
	NFP_FL_LAG_BATCH_FIRST,
	NFP_FL_LAG_BATCH_MEMBER,
	NFP_FL_LAG_BATCH_FINISHED
};

/**
 * struct nfp_flower_cmsg_lag_config - control message payload for LAG config
 * @ctrl_flags:	Configuration flags
 * @reserved:	Reserved for future use
 * @ttl:	Time to live of packet - host always sets to 0xff
 * @pkt_number:	Config message packet number - increment for each message
 * @batch_ver:	Batch version of messages - increment for each batch of messages
 * @group_id:	Group ID applicable
 * @group_inst:	Group instance number - increment when group is reused
 * @members:	Array of 32-bit words listing all active group members
 */
struct nfp_flower_cmsg_lag_config {
	u8 ctrl_flags;
	u8 reserved[2];
	u8 ttl;
	__be32 pkt_number;
	__be32 batch_ver;
	__be32 group_id;
	__be32 group_inst;
	__be32 members[];
};

/**
 * struct nfp_fl_lag_group - list entry for each LAG group
 * @group_id:		Assigned group ID for host/kernel sync
 * @group_inst:		Group instance in case of ID reuse
 * @list:		List entry
 * @master_ndev:	Group master Netdev
 * @dirty:		Marked if the group needs synced to HW
 * @offloaded:		Marked if the group is currently offloaded to NIC
 * @to_remove:		Marked if the group should be removed from NIC
 * @to_destroy:		Marked if the group should be removed from driver
 * @slave_cnt:		Number of slaves in group
 */
struct nfp_fl_lag_group {
	unsigned int group_id;
	u8 group_inst;
	struct list_head list;
	struct net_device *master_ndev;
	bool dirty;
	bool offloaded;
	bool to_remove;
	bool to_destroy;
	unsigned int slave_cnt;
};

#define NFP_FL_LAG_PKT_NUMBER_MASK	GENMASK(30, 0)
#define NFP_FL_LAG_VERSION_MASK		GENMASK(22, 0)
#define NFP_FL_LAG_HOST_TTL		0xff

/* Use this ID with zero members to ack a batch config */
#define NFP_FL_LAG_SYNC_ID		0
#define NFP_FL_LAG_GROUP_MIN		1 /* ID 0 reserved */
#define NFP_FL_LAG_GROUP_MAX		32 /* IDs 1 to 31 are valid */

/* wait for more config */
#define NFP_FL_LAG_DELAY		(msecs_to_jiffies(2))

#define NFP_FL_LAG_RETRANS_LIMIT	100 /* max retrans cmsgs to store */

static unsigned int nfp_fl_get_next_pkt_number(struct nfp_fl_lag *lag)
{
	lag->pkt_num++;
	lag->pkt_num &= NFP_FL_LAG_PKT_NUMBER_MASK;

	return lag->pkt_num;
}

static void nfp_fl_increment_version(struct nfp_fl_lag *lag)
{
	/* LSB is not considered by firmware so add 2 for each increment. */
	lag->batch_ver += 2;
	lag->batch_ver &= NFP_FL_LAG_VERSION_MASK;

	/* Zero is reserved by firmware. */
	if (!lag->batch_ver)
		lag->batch_ver += 2;
}

static struct nfp_fl_lag_group *
nfp_fl_lag_group_create(struct nfp_fl_lag *lag, struct net_device *master)
{
	struct nfp_fl_lag_group *group;
	struct nfp_flower_priv *priv;
	int id;

	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);

	id = ida_simple_get(&lag->ida_handle, NFP_FL_LAG_GROUP_MIN,
			    NFP_FL_LAG_GROUP_MAX, GFP_KERNEL);
	if (id < 0) {
		nfp_flower_cmsg_warn(priv->app,
				     "No more bonding groups available\n");
		return ERR_PTR(id);
	}

	group = kmalloc(sizeof(*group), GFP_KERNEL);
	if (!group) {
		ida_simple_remove(&lag->ida_handle, id);
		return ERR_PTR(-ENOMEM);
	}

	group->group_id = id;
	group->master_ndev = master;
	group->dirty = true;
	group->offloaded = false;
	group->to_remove = false;
	group->to_destroy = false;
	group->slave_cnt = 0;
	group->group_inst = ++lag->global_inst;
	list_add_tail(&group->list, &lag->group_list);

	return group;
}

static struct nfp_fl_lag_group *
nfp_fl_lag_find_group_for_master_with_lag(struct nfp_fl_lag *lag,
					  struct net_device *master)
{
	struct nfp_fl_lag_group *entry;

	if (!master)
		return NULL;

	list_for_each_entry(entry, &lag->group_list, list)
		if (entry->master_ndev == master)
			return entry;

	return NULL;
}

static int nfp_fl_lag_get_group_info(struct nfp_app *app,
				     struct net_device *netdev,
				     __be16 *group_id,
				     u8 *batch_ver,
				     u8 *group_inst)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_lag_group *group = NULL;
	__be32 temp_vers;

	mutex_lock(&priv->nfp_lag.lock);
	group = nfp_fl_lag_find_group_for_master_with_lag(&priv->nfp_lag,
							  netdev);
	if (!group) {
		mutex_unlock(&priv->nfp_lag.lock);
		return -ENOENT;
	}

	if (group_id)
		*group_id = cpu_to_be16(group->group_id);

	if (batch_ver) {
		temp_vers = cpu_to_be32(priv->nfp_lag.batch_ver <<
					NFP_FL_PRE_LAG_VER_OFF);
		memcpy(batch_ver, &temp_vers, 3);
	}

	if (group_inst)
		*group_inst = group->group_inst;

	mutex_unlock(&priv->nfp_lag.lock);

	return 0;
}

int nfp_flower_lag_populate_pre_action(struct nfp_app *app,
				       struct net_device *master,
				       struct nfp_fl_pre_lag *pre_act,
				       struct netlink_ext_ack *extack)
{
	if (nfp_fl_lag_get_group_info(app, master, &pre_act->group_id,
				      pre_act->lag_version,
				      &pre_act->instance)) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: group does not exist for LAG action");
		return -ENOENT;
	}

	return 0;
}

void nfp_flower_lag_get_info_from_netdev(struct nfp_app *app,
					 struct net_device *netdev,
					 struct nfp_tun_neigh_lag *lag)
{
	nfp_fl_lag_get_group_info(app, netdev, NULL,
				  lag->lag_version, &lag->lag_instance);
}

int nfp_flower_lag_get_output_id(struct nfp_app *app, struct net_device *master)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_lag_group *group = NULL;
	int group_id = -ENOENT;

	mutex_lock(&priv->nfp_lag.lock);
	group = nfp_fl_lag_find_group_for_master_with_lag(&priv->nfp_lag,
							  master);
	if (group)
		group_id = group->group_id;
	mutex_unlock(&priv->nfp_lag.lock);

	return group_id;
}

static int
nfp_fl_lag_config_group(struct nfp_fl_lag *lag, struct nfp_fl_lag_group *group,
			struct net_device **active_members,
			unsigned int member_cnt, enum nfp_fl_lag_batch *batch)
{
	struct nfp_flower_cmsg_lag_config *cmsg_payload;
	struct nfp_flower_priv *priv;
	unsigned long int flags;
	unsigned int size, i;
	struct sk_buff *skb;

	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);
	size = sizeof(*cmsg_payload) + sizeof(__be32) * member_cnt;
	skb = nfp_flower_cmsg_alloc(priv->app, size,
				    NFP_FLOWER_CMSG_TYPE_LAG_CONFIG,
				    GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	cmsg_payload = nfp_flower_cmsg_get_data(skb);
	flags = 0;

	/* Increment batch version for each new batch of config messages. */
	if (*batch == NFP_FL_LAG_BATCH_FIRST) {
		flags |= NFP_FL_LAG_FIRST;
		nfp_fl_increment_version(lag);
		*batch = NFP_FL_LAG_BATCH_MEMBER;
	}

	/* If it is a reset msg then it is also the end of the batch. */
	if (lag->rst_cfg) {
		flags |= NFP_FL_LAG_RESET;
		*batch = NFP_FL_LAG_BATCH_FINISHED;
	}

	/* To signal the end of a batch, both the switch and last flags are set
	 * and the reserved SYNC group ID is used.
	 */
	if (*batch == NFP_FL_LAG_BATCH_FINISHED) {
		flags |= NFP_FL_LAG_SWITCH | NFP_FL_LAG_LAST;
		lag->rst_cfg = false;
		cmsg_payload->group_id = cpu_to_be32(NFP_FL_LAG_SYNC_ID);
		cmsg_payload->group_inst = 0;
	} else {
		cmsg_payload->group_id = cpu_to_be32(group->group_id);
		cmsg_payload->group_inst = cpu_to_be32(group->group_inst);
	}

	cmsg_payload->reserved[0] = 0;
	cmsg_payload->reserved[1] = 0;
	cmsg_payload->ttl = NFP_FL_LAG_HOST_TTL;
	cmsg_payload->ctrl_flags = flags;
	cmsg_payload->batch_ver = cpu_to_be32(lag->batch_ver);
	cmsg_payload->pkt_number = cpu_to_be32(nfp_fl_get_next_pkt_number(lag));

	for (i = 0; i < member_cnt; i++)
		cmsg_payload->members[i] =
			cpu_to_be32(nfp_repr_get_port_id(active_members[i]));

	nfp_ctrl_tx(priv->app->ctrl, skb);
	return 0;
}

static void nfp_fl_lag_do_work(struct work_struct *work)
{
	enum nfp_fl_lag_batch batch = NFP_FL_LAG_BATCH_FIRST;
	struct nfp_fl_lag_group *entry, *storage;
	struct delayed_work *delayed_work;
	struct nfp_flower_priv *priv;
	struct nfp_fl_lag *lag;
	int err;

	delayed_work = to_delayed_work(work);
	lag = container_of(delayed_work, struct nfp_fl_lag, work);
	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);

	mutex_lock(&lag->lock);
	list_for_each_entry_safe(entry, storage, &lag->group_list, list) {
		struct net_device *iter_netdev, **acti_netdevs;
		struct nfp_flower_repr_priv *repr_priv;
		int active_count = 0, slaves = 0;
		struct nfp_repr *repr;
		unsigned long *flags;

		if (entry->to_remove) {
			/* Active count of 0 deletes group on hw. */
			err = nfp_fl_lag_config_group(lag, entry, NULL, 0,
						      &batch);
			if (!err) {
				entry->to_remove = false;
				entry->offloaded = false;
			} else {
				nfp_flower_cmsg_warn(priv->app,
						     "group delete failed\n");
				schedule_delayed_work(&lag->work,
						      NFP_FL_LAG_DELAY);
				continue;
			}

			if (entry->to_destroy) {
				ida_simple_remove(&lag->ida_handle,
						  entry->group_id);
				list_del(&entry->list);
				kfree(entry);
			}
			continue;
		}

		acti_netdevs = kmalloc_array(entry->slave_cnt,
					     sizeof(*acti_netdevs), GFP_KERNEL);

		/* Include sanity check in the loop. It may be that a bond has
		 * changed between processing the last notification and the
		 * work queue triggering. If the number of slaves has changed
		 * or it now contains netdevs that cannot be offloaded, ignore
		 * the group until pending notifications are processed.
		 */
		rcu_read_lock();
		for_each_netdev_in_bond_rcu(entry->master_ndev, iter_netdev) {
			if (!nfp_netdev_is_nfp_repr(iter_netdev)) {
				slaves = 0;
				break;
			}

			repr = netdev_priv(iter_netdev);

			if (repr->app != priv->app) {
				slaves = 0;
				break;
			}

			slaves++;
			if (slaves > entry->slave_cnt)
				break;

			/* Check the ports for state changes. */
			repr_priv = repr->app_priv;
			flags = &repr_priv->lag_port_flags;

			if (*flags & NFP_PORT_LAG_CHANGED) {
				*flags &= ~NFP_PORT_LAG_CHANGED;
				entry->dirty = true;
			}

			if ((*flags & NFP_PORT_LAG_TX_ENABLED) &&
			    (*flags & NFP_PORT_LAG_LINK_UP))
				acti_netdevs[active_count++] = iter_netdev;
		}
		rcu_read_unlock();

		if (slaves != entry->slave_cnt || !entry->dirty) {
			kfree(acti_netdevs);
			continue;
		}

		err = nfp_fl_lag_config_group(lag, entry, acti_netdevs,
					      active_count, &batch);
		if (!err) {
			entry->offloaded = true;
			entry->dirty = false;
		} else {
			nfp_flower_cmsg_warn(priv->app,
					     "group offload failed\n");
			schedule_delayed_work(&lag->work, NFP_FL_LAG_DELAY);
		}

		kfree(acti_netdevs);
	}

	/* End the config batch if at least one packet has been batched. */
	if (batch == NFP_FL_LAG_BATCH_MEMBER) {
		batch = NFP_FL_LAG_BATCH_FINISHED;
		err = nfp_fl_lag_config_group(lag, NULL, NULL, 0, &batch);
		if (err)
			nfp_flower_cmsg_warn(priv->app,
					     "group batch end cmsg failed\n");
	}

	mutex_unlock(&lag->lock);
}

static int
nfp_fl_lag_put_unprocessed(struct nfp_fl_lag *lag, struct sk_buff *skb)
{
	struct nfp_flower_cmsg_lag_config *cmsg_payload;

	cmsg_payload = nfp_flower_cmsg_get_data(skb);
	if (be32_to_cpu(cmsg_payload->group_id) >= NFP_FL_LAG_GROUP_MAX)
		return -EINVAL;

	/* Drop cmsg retrans if storage limit is exceeded to prevent
	 * overloading. If the fw notices that expected messages have not been
	 * received in a given time block, it will request a full resync.
	 */
	if (skb_queue_len(&lag->retrans_skbs) >= NFP_FL_LAG_RETRANS_LIMIT)
		return -ENOSPC;

	__skb_queue_tail(&lag->retrans_skbs, skb);

	return 0;
}

static void nfp_fl_send_unprocessed(struct nfp_fl_lag *lag)
{
	struct nfp_flower_priv *priv;
	struct sk_buff *skb;

	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);

	while ((skb = __skb_dequeue(&lag->retrans_skbs)))
		nfp_ctrl_tx(priv->app->ctrl, skb);
}

bool nfp_flower_lag_unprocessed_msg(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_cmsg_lag_config *cmsg_payload;
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_lag_group *group_entry;
	unsigned long int flags;
	bool store_skb = false;
	int err;

	cmsg_payload = nfp_flower_cmsg_get_data(skb);
	flags = cmsg_payload->ctrl_flags;

	/* Note the intentional fall through below. If DATA and XON are both
	 * set, the message will stored and sent again with the rest of the
	 * unprocessed messages list.
	 */

	/* Store */
	if (flags & NFP_FL_LAG_DATA)
		if (!nfp_fl_lag_put_unprocessed(&priv->nfp_lag, skb))
			store_skb = true;

	/* Send stored */
	if (flags & NFP_FL_LAG_XON)
		nfp_fl_send_unprocessed(&priv->nfp_lag);

	/* Resend all */
	if (flags & NFP_FL_LAG_SYNC) {
		/* To resend all config:
		 * 1) Clear all unprocessed messages
		 * 2) Mark all groups dirty
		 * 3) Reset NFP group config
		 * 4) Schedule a LAG config update
		 */

		__skb_queue_purge(&priv->nfp_lag.retrans_skbs);

		mutex_lock(&priv->nfp_lag.lock);
		list_for_each_entry(group_entry, &priv->nfp_lag.group_list,
				    list)
			group_entry->dirty = true;

		err = nfp_flower_lag_reset(&priv->nfp_lag);
		if (err)
			nfp_flower_cmsg_warn(priv->app,
					     "mem err in group reset msg\n");
		mutex_unlock(&priv->nfp_lag.lock);

		schedule_delayed_work(&priv->nfp_lag.work, 0);
	}

	return store_skb;
}

static void
nfp_fl_lag_schedule_group_remove(struct nfp_fl_lag *lag,
				 struct nfp_fl_lag_group *group)
{
	group->to_remove = true;

	schedule_delayed_work(&lag->work, NFP_FL_LAG_DELAY);
}

static void
nfp_fl_lag_schedule_group_delete(struct nfp_fl_lag *lag,
				 struct net_device *master)
{
	struct nfp_fl_lag_group *group;
	struct nfp_flower_priv *priv;

	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);

	if (!netif_is_bond_master(master))
		return;

	mutex_lock(&lag->lock);
	group = nfp_fl_lag_find_group_for_master_with_lag(lag, master);
	if (!group) {
		mutex_unlock(&lag->lock);
		nfp_warn(priv->app->cpp, "untracked bond got unregistered %s\n",
			 netdev_name(master));
		return;
	}

	group->to_remove = true;
	group->to_destroy = true;
	mutex_unlock(&lag->lock);

	schedule_delayed_work(&lag->work, NFP_FL_LAG_DELAY);
}

static int
nfp_fl_lag_changeupper_event(struct nfp_fl_lag *lag,
			     struct netdev_notifier_changeupper_info *info)
{
	struct net_device *upper = info->upper_dev, *iter_netdev;
	struct netdev_lag_upper_info *lag_upper_info;
	struct nfp_fl_lag_group *group;
	struct nfp_flower_priv *priv;
	unsigned int slave_count = 0;
	bool can_offload = true;
	struct nfp_repr *repr;

	if (!netif_is_lag_master(upper))
		return 0;

	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(upper, iter_netdev) {
		if (!nfp_netdev_is_nfp_repr(iter_netdev)) {
			can_offload = false;
			break;
		}
		repr = netdev_priv(iter_netdev);

		/* Ensure all ports are created by the same app/on same card. */
		if (repr->app != priv->app) {
			can_offload = false;
			break;
		}

		slave_count++;
	}
	rcu_read_unlock();

	lag_upper_info = info->upper_info;

	/* Firmware supports active/backup and L3/L4 hash bonds. */
	if (lag_upper_info &&
	    lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_ACTIVEBACKUP &&
	    (lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH ||
	     (lag_upper_info->hash_type != NETDEV_LAG_HASH_L34 &&
	      lag_upper_info->hash_type != NETDEV_LAG_HASH_E34 &&
	      lag_upper_info->hash_type != NETDEV_LAG_HASH_UNKNOWN))) {
		can_offload = false;
		nfp_flower_cmsg_warn(priv->app,
				     "Unable to offload tx_type %u hash %u\n",
				     lag_upper_info->tx_type,
				     lag_upper_info->hash_type);
	}

	mutex_lock(&lag->lock);
	group = nfp_fl_lag_find_group_for_master_with_lag(lag, upper);

	if (slave_count == 0 || !can_offload) {
		/* Cannot offload the group - remove if previously offloaded. */
		if (group && group->offloaded)
			nfp_fl_lag_schedule_group_remove(lag, group);

		mutex_unlock(&lag->lock);
		return 0;
	}

	if (!group) {
		group = nfp_fl_lag_group_create(lag, upper);
		if (IS_ERR(group)) {
			mutex_unlock(&lag->lock);
			return PTR_ERR(group);
		}
	}

	group->dirty = true;
	group->slave_cnt = slave_count;

	/* Group may have been on queue for removal but is now offloadable. */
	group->to_remove = false;
	mutex_unlock(&lag->lock);

	schedule_delayed_work(&lag->work, NFP_FL_LAG_DELAY);
	return 0;
}

static void
nfp_fl_lag_changels_event(struct nfp_fl_lag *lag, struct net_device *netdev,
			  struct netdev_notifier_changelowerstate_info *info)
{
	struct netdev_lag_lower_state_info *lag_lower_info;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_flower_priv *priv;
	struct nfp_repr *repr;
	unsigned long *flags;

	if (!netif_is_lag_port(netdev) || !nfp_netdev_is_nfp_repr(netdev))
		return;

	lag_lower_info = info->lower_state_info;
	if (!lag_lower_info)
		return;

	priv = container_of(lag, struct nfp_flower_priv, nfp_lag);
	repr = netdev_priv(netdev);

	/* Verify that the repr is associated with this app. */
	if (repr->app != priv->app)
		return;

	repr_priv = repr->app_priv;
	flags = &repr_priv->lag_port_flags;

	mutex_lock(&lag->lock);
	if (lag_lower_info->link_up)
		*flags |= NFP_PORT_LAG_LINK_UP;
	else
		*flags &= ~NFP_PORT_LAG_LINK_UP;

	if (lag_lower_info->tx_enabled)
		*flags |= NFP_PORT_LAG_TX_ENABLED;
	else
		*flags &= ~NFP_PORT_LAG_TX_ENABLED;

	*flags |= NFP_PORT_LAG_CHANGED;
	mutex_unlock(&lag->lock);

	schedule_delayed_work(&lag->work, NFP_FL_LAG_DELAY);
}

int nfp_flower_lag_netdev_event(struct nfp_flower_priv *priv,
				struct net_device *netdev,
				unsigned long event, void *ptr)
{
	struct nfp_fl_lag *lag = &priv->nfp_lag;
	int err;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		err = nfp_fl_lag_changeupper_event(lag, ptr);
		if (err)
			return NOTIFY_BAD;
		return NOTIFY_OK;
	case NETDEV_CHANGELOWERSTATE:
		nfp_fl_lag_changels_event(lag, netdev, ptr);
		return NOTIFY_OK;
	case NETDEV_UNREGISTER:
		nfp_fl_lag_schedule_group_delete(lag, netdev);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

int nfp_flower_lag_reset(struct nfp_fl_lag *lag)
{
	enum nfp_fl_lag_batch batch = NFP_FL_LAG_BATCH_FIRST;

	lag->rst_cfg = true;
	return nfp_fl_lag_config_group(lag, NULL, NULL, 0, &batch);
}

void nfp_flower_lag_init(struct nfp_fl_lag *lag)
{
	INIT_DELAYED_WORK(&lag->work, nfp_fl_lag_do_work);
	INIT_LIST_HEAD(&lag->group_list);
	mutex_init(&lag->lock);
	ida_init(&lag->ida_handle);

	__skb_queue_head_init(&lag->retrans_skbs);

	/* 0 is a reserved batch version so increment to first valid value. */
	nfp_fl_increment_version(lag);
}

void nfp_flower_lag_cleanup(struct nfp_fl_lag *lag)
{
	struct nfp_fl_lag_group *entry, *storage;

	cancel_delayed_work_sync(&lag->work);

	__skb_queue_purge(&lag->retrans_skbs);

	/* Remove all groups. */
	mutex_lock(&lag->lock);
	list_for_each_entry_safe(entry, storage, &lag->group_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&lag->lock);
	mutex_destroy(&lag->lock);
	ida_destroy(&lag->ida_handle);
}
