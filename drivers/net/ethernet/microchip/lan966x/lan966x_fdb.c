// SPDX-License-Identifier: GPL-2.0+

#include <net/switchdev.h>

#include "lan966x_main.h"

struct lan966x_fdb_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	struct lan966x *lan966x;
	unsigned long event;
};

struct lan966x_fdb_entry {
	struct list_head list;
	unsigned char mac[ETH_ALEN] __aligned(2);
	u16 vid;
	u32 references;
};

static struct lan966x_fdb_entry *
lan966x_fdb_find_entry(struct lan966x *lan966x,
		       struct switchdev_notifier_fdb_info *fdb_info)
{
	struct lan966x_fdb_entry *fdb_entry;

	list_for_each_entry(fdb_entry, &lan966x->fdb_entries, list) {
		if (fdb_entry->vid == fdb_info->vid &&
		    ether_addr_equal(fdb_entry->mac, fdb_info->addr))
			return fdb_entry;
	}

	return NULL;
}

static void lan966x_fdb_add_entry(struct lan966x *lan966x,
				  struct switchdev_notifier_fdb_info *fdb_info)
{
	struct lan966x_fdb_entry *fdb_entry;

	fdb_entry = lan966x_fdb_find_entry(lan966x, fdb_info);
	if (fdb_entry) {
		fdb_entry->references++;
		return;
	}

	fdb_entry = kzalloc(sizeof(*fdb_entry), GFP_KERNEL);
	if (!fdb_entry)
		return;

	ether_addr_copy(fdb_entry->mac, fdb_info->addr);
	fdb_entry->vid = fdb_info->vid;
	fdb_entry->references = 1;
	list_add_tail(&fdb_entry->list, &lan966x->fdb_entries);
}

static bool lan966x_fdb_del_entry(struct lan966x *lan966x,
				  struct switchdev_notifier_fdb_info *fdb_info)
{
	struct lan966x_fdb_entry *fdb_entry, *tmp;

	list_for_each_entry_safe(fdb_entry, tmp, &lan966x->fdb_entries,
				 list) {
		if (fdb_entry->vid == fdb_info->vid &&
		    ether_addr_equal(fdb_entry->mac, fdb_info->addr)) {
			fdb_entry->references--;
			if (!fdb_entry->references) {
				list_del(&fdb_entry->list);
				kfree(fdb_entry);
				return true;
			}
			break;
		}
	}

	return false;
}

void lan966x_fdb_write_entries(struct lan966x *lan966x, u16 vid)
{
	struct lan966x_fdb_entry *fdb_entry;

	list_for_each_entry(fdb_entry, &lan966x->fdb_entries, list) {
		if (fdb_entry->vid != vid)
			continue;

		lan966x_mac_cpu_learn(lan966x, fdb_entry->mac, fdb_entry->vid);
	}
}

void lan966x_fdb_erase_entries(struct lan966x *lan966x, u16 vid)
{
	struct lan966x_fdb_entry *fdb_entry;

	list_for_each_entry(fdb_entry, &lan966x->fdb_entries, list) {
		if (fdb_entry->vid != vid)
			continue;

		lan966x_mac_cpu_forget(lan966x, fdb_entry->mac, fdb_entry->vid);
	}
}

static void lan966x_fdb_purge_entries(struct lan966x *lan966x)
{
	struct lan966x_fdb_entry *fdb_entry, *tmp;

	list_for_each_entry_safe(fdb_entry, tmp, &lan966x->fdb_entries, list) {
		list_del(&fdb_entry->list);
		kfree(fdb_entry);
	}
}

int lan966x_fdb_init(struct lan966x *lan966x)
{
	INIT_LIST_HEAD(&lan966x->fdb_entries);
	lan966x->fdb_work = alloc_ordered_workqueue("lan966x_order", 0);
	if (!lan966x->fdb_work)
		return -ENOMEM;

	return 0;
}

void lan966x_fdb_deinit(struct lan966x *lan966x)
{
	destroy_workqueue(lan966x->fdb_work);
	lan966x_fdb_purge_entries(lan966x);
}

static void lan966x_fdb_event_work(struct work_struct *work)
{
	struct lan966x_fdb_event_work *fdb_work =
		container_of(work, struct lan966x_fdb_event_work, work);
	struct switchdev_notifier_fdb_info *fdb_info;
	struct net_device *dev = fdb_work->dev;
	struct lan966x_port *port;
	struct lan966x *lan966x;
	int ret;

	fdb_info = &fdb_work->fdb_info;
	lan966x = fdb_work->lan966x;

	if (lan966x_netdevice_check(dev)) {
		port = netdev_priv(dev);

		switch (fdb_work->event) {
		case SWITCHDEV_FDB_ADD_TO_DEVICE:
			if (!fdb_info->added_by_user)
				break;
			lan966x_mac_add_entry(lan966x, port, fdb_info->addr,
					      fdb_info->vid);
			break;
		case SWITCHDEV_FDB_DEL_TO_DEVICE:
			if (!fdb_info->added_by_user)
				break;
			lan966x_mac_del_entry(lan966x, fdb_info->addr,
					      fdb_info->vid);
			break;
		}
	} else {
		if (!netif_is_bridge_master(dev))
			goto out;

		/* In case the bridge is called */
		switch (fdb_work->event) {
		case SWITCHDEV_FDB_ADD_TO_DEVICE:
			/* If there is no front port in this vlan, there is no
			 * point to copy the frame to CPU because it would be
			 * just dropped at later point. So add it only if
			 * there is a port but it is required to store the fdb
			 * entry for later point when a port actually gets in
			 * the vlan.
			 */
			lan966x_fdb_add_entry(lan966x, fdb_info);
			if (!lan966x_vlan_cpu_member_cpu_vlan_mask(lan966x,
								   fdb_info->vid))
				break;

			lan966x_mac_cpu_learn(lan966x, fdb_info->addr,
					      fdb_info->vid);
			break;
		case SWITCHDEV_FDB_DEL_TO_DEVICE:
			ret = lan966x_fdb_del_entry(lan966x, fdb_info);
			if (!lan966x_vlan_cpu_member_cpu_vlan_mask(lan966x,
								   fdb_info->vid))
				break;

			if (ret)
				lan966x_mac_cpu_forget(lan966x, fdb_info->addr,
						       fdb_info->vid);
			break;
		}
	}

out:
	kfree(fdb_work->fdb_info.addr);
	kfree(fdb_work);
	dev_put(dev);
}

int lan966x_handle_fdb(struct net_device *dev,
		       struct net_device *orig_dev,
		       unsigned long event, const void *ctx,
		       const struct switchdev_notifier_fdb_info *fdb_info)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_fdb_event_work *fdb_work;

	if (ctx && ctx != port)
		return 0;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (lan966x_netdevice_check(orig_dev) &&
		    !fdb_info->added_by_user)
			break;

		fdb_work = kzalloc(sizeof(*fdb_work), GFP_ATOMIC);
		if (!fdb_work)
			return -ENOMEM;

		fdb_work->dev = orig_dev;
		fdb_work->lan966x = lan966x;
		fdb_work->event = event;
		INIT_WORK(&fdb_work->work, lan966x_fdb_event_work);
		memcpy(&fdb_work->fdb_info, fdb_info, sizeof(fdb_work->fdb_info));
		fdb_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!fdb_work->fdb_info.addr)
			goto err_addr_alloc;

		ether_addr_copy((u8 *)fdb_work->fdb_info.addr, fdb_info->addr);
		dev_hold(orig_dev);

		queue_work(lan966x->fdb_work, &fdb_work->work);
		break;
	}

	return 0;
err_addr_alloc:
	kfree(fdb_work);
	return -ENOMEM;
}
