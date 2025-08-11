// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>

#include "netdevsim.h"

#define NSIM_DEV_HWSTATS_TRAFFIC_MS	100

static struct list_head *
nsim_dev_hwstats_get_list_head(struct nsim_dev_hwstats *hwstats,
			       enum netdev_offload_xstats_type type)
{
	switch (type) {
	case NETDEV_OFFLOAD_XSTATS_TYPE_L3:
		return &hwstats->l3_list;
	}

	WARN_ON_ONCE(1);
	return NULL;
}

static void nsim_dev_hwstats_traffic_bump(struct nsim_dev_hwstats *hwstats,
					  enum netdev_offload_xstats_type type)
{
	struct nsim_dev_hwstats_netdev *hwsdev;
	struct list_head *hwsdev_list;

	hwsdev_list = nsim_dev_hwstats_get_list_head(hwstats, type);
	if (WARN_ON(!hwsdev_list))
		return;

	list_for_each_entry(hwsdev, hwsdev_list, list) {
		if (hwsdev->enabled) {
			hwsdev->stats.rx_packets += 1;
			hwsdev->stats.tx_packets += 2;
			hwsdev->stats.rx_bytes += 100;
			hwsdev->stats.tx_bytes += 300;
		}
	}
}

static void nsim_dev_hwstats_traffic_work(struct work_struct *work)
{
	struct nsim_dev_hwstats *hwstats;

	hwstats = container_of(work, struct nsim_dev_hwstats, traffic_dw.work);
	mutex_lock(&hwstats->hwsdev_list_lock);
	nsim_dev_hwstats_traffic_bump(hwstats, NETDEV_OFFLOAD_XSTATS_TYPE_L3);
	mutex_unlock(&hwstats->hwsdev_list_lock);

	schedule_delayed_work(&hwstats->traffic_dw,
			      msecs_to_jiffies(NSIM_DEV_HWSTATS_TRAFFIC_MS));
}

static struct nsim_dev_hwstats_netdev *
nsim_dev_hwslist_find_hwsdev(struct list_head *hwsdev_list,
			     int ifindex)
{
	struct nsim_dev_hwstats_netdev *hwsdev;

	list_for_each_entry(hwsdev, hwsdev_list, list) {
		if (hwsdev->netdev->ifindex == ifindex)
			return hwsdev;
	}

	return NULL;
}

static int nsim_dev_hwsdev_enable(struct nsim_dev_hwstats_netdev *hwsdev,
				  struct netlink_ext_ack *extack)
{
	if (hwsdev->fail_enable) {
		hwsdev->fail_enable = false;
		NL_SET_ERR_MSG_MOD(extack, "Stats enablement set to fail");
		return -ECANCELED;
	}

	hwsdev->enabled = true;
	return 0;
}

static void nsim_dev_hwsdev_disable(struct nsim_dev_hwstats_netdev *hwsdev)
{
	hwsdev->enabled = false;
	memset(&hwsdev->stats, 0, sizeof(hwsdev->stats));
}

static int
nsim_dev_hwsdev_report_delta(struct nsim_dev_hwstats_netdev *hwsdev,
			     struct netdev_notifier_offload_xstats_info *info)
{
	netdev_offload_xstats_report_delta(info->report_delta, &hwsdev->stats);
	memset(&hwsdev->stats, 0, sizeof(hwsdev->stats));
	return 0;
}

static void
nsim_dev_hwsdev_report_used(struct nsim_dev_hwstats_netdev *hwsdev,
			    struct netdev_notifier_offload_xstats_info *info)
{
	if (hwsdev->enabled)
		netdev_offload_xstats_report_used(info->report_used);
}

static int nsim_dev_hwstats_event_off_xstats(struct nsim_dev_hwstats *hwstats,
					     struct net_device *dev,
					     unsigned long event, void *ptr)
{
	struct netdev_notifier_offload_xstats_info *info;
	struct nsim_dev_hwstats_netdev *hwsdev;
	struct list_head *hwsdev_list;
	int err = 0;

	info = ptr;
	hwsdev_list = nsim_dev_hwstats_get_list_head(hwstats, info->type);
	if (!hwsdev_list)
		return 0;

	mutex_lock(&hwstats->hwsdev_list_lock);

	hwsdev = nsim_dev_hwslist_find_hwsdev(hwsdev_list, dev->ifindex);
	if (!hwsdev)
		goto out;

	switch (event) {
	case NETDEV_OFFLOAD_XSTATS_ENABLE:
		err = nsim_dev_hwsdev_enable(hwsdev, info->info.extack);
		break;
	case NETDEV_OFFLOAD_XSTATS_DISABLE:
		nsim_dev_hwsdev_disable(hwsdev);
		break;
	case NETDEV_OFFLOAD_XSTATS_REPORT_USED:
		nsim_dev_hwsdev_report_used(hwsdev, info);
		break;
	case NETDEV_OFFLOAD_XSTATS_REPORT_DELTA:
		err = nsim_dev_hwsdev_report_delta(hwsdev, info);
		break;
	}

out:
	mutex_unlock(&hwstats->hwsdev_list_lock);
	return err;
}

static void nsim_dev_hwsdev_fini(struct nsim_dev_hwstats_netdev *hwsdev)
{
	dev_put(hwsdev->netdev);
	kfree(hwsdev);
}

static void
__nsim_dev_hwstats_event_unregister(struct nsim_dev_hwstats *hwstats,
				    struct net_device *dev,
				    enum netdev_offload_xstats_type type)
{
	struct nsim_dev_hwstats_netdev *hwsdev;
	struct list_head *hwsdev_list;

	hwsdev_list = nsim_dev_hwstats_get_list_head(hwstats, type);
	if (WARN_ON(!hwsdev_list))
		return;

	hwsdev = nsim_dev_hwslist_find_hwsdev(hwsdev_list, dev->ifindex);
	if (!hwsdev)
		return;

	list_del(&hwsdev->list);
	nsim_dev_hwsdev_fini(hwsdev);
}

static void nsim_dev_hwstats_event_unregister(struct nsim_dev_hwstats *hwstats,
					      struct net_device *dev)
{
	mutex_lock(&hwstats->hwsdev_list_lock);
	__nsim_dev_hwstats_event_unregister(hwstats, dev,
					    NETDEV_OFFLOAD_XSTATS_TYPE_L3);
	mutex_unlock(&hwstats->hwsdev_list_lock);
}

static int nsim_dev_hwstats_event(struct nsim_dev_hwstats *hwstats,
				  struct net_device *dev,
				  unsigned long event, void *ptr)
{
	switch (event) {
	case NETDEV_OFFLOAD_XSTATS_ENABLE:
	case NETDEV_OFFLOAD_XSTATS_DISABLE:
	case NETDEV_OFFLOAD_XSTATS_REPORT_USED:
	case NETDEV_OFFLOAD_XSTATS_REPORT_DELTA:
		return nsim_dev_hwstats_event_off_xstats(hwstats, dev,
							 event, ptr);
	case NETDEV_UNREGISTER:
		nsim_dev_hwstats_event_unregister(hwstats, dev);
		break;
	}

	return 0;
}

static int nsim_dev_netdevice_event(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct nsim_dev_hwstats *hwstats;
	int err = 0;

	hwstats = container_of(nb, struct nsim_dev_hwstats, netdevice_nb);
	err = nsim_dev_hwstats_event(hwstats, dev, event, ptr);
	if (err)
		return notifier_from_errno(err);

	return NOTIFY_OK;
}

static int
nsim_dev_hwstats_enable_ifindex(struct nsim_dev_hwstats *hwstats,
				int ifindex,
				enum netdev_offload_xstats_type type,
				struct list_head *hwsdev_list)
{
	struct nsim_dev_hwstats_netdev *hwsdev;
	struct nsim_dev *nsim_dev;
	struct net_device *netdev;
	struct net *net;
	int err = 0;

	nsim_dev = container_of(hwstats, struct nsim_dev, hwstats);
	net = nsim_dev_net(nsim_dev);

	rtnl_lock();
	mutex_lock(&hwstats->hwsdev_list_lock);
	hwsdev = nsim_dev_hwslist_find_hwsdev(hwsdev_list, ifindex);
	if (hwsdev)
		goto out_unlock_list;

	netdev = dev_get_by_index(net, ifindex);
	if (!netdev) {
		err = -ENODEV;
		goto out_unlock_list;
	}

	hwsdev = kzalloc(sizeof(*hwsdev), GFP_KERNEL);
	if (!hwsdev) {
		err = -ENOMEM;
		goto out_put_netdev;
	}

	hwsdev->netdev = netdev;
	list_add_tail(&hwsdev->list, hwsdev_list);
	mutex_unlock(&hwstats->hwsdev_list_lock);

	if (netdev_offload_xstats_enabled(netdev, type)) {
		nsim_dev_hwsdev_enable(hwsdev, NULL);
		rtnl_offload_xstats_notify(netdev);
	}

	rtnl_unlock();
	return err;

out_put_netdev:
	dev_put(netdev);
out_unlock_list:
	mutex_unlock(&hwstats->hwsdev_list_lock);
	rtnl_unlock();
	return err;
}

static int
nsim_dev_hwstats_disable_ifindex(struct nsim_dev_hwstats *hwstats,
				 int ifindex,
				 enum netdev_offload_xstats_type type,
				 struct list_head *hwsdev_list)
{
	struct nsim_dev_hwstats_netdev *hwsdev;
	int err = 0;

	rtnl_lock();
	mutex_lock(&hwstats->hwsdev_list_lock);
	hwsdev = nsim_dev_hwslist_find_hwsdev(hwsdev_list, ifindex);
	if (hwsdev)
		list_del(&hwsdev->list);
	mutex_unlock(&hwstats->hwsdev_list_lock);

	if (!hwsdev) {
		err = -ENOENT;
		goto unlock_out;
	}

	if (netdev_offload_xstats_enabled(hwsdev->netdev, type)) {
		netdev_offload_xstats_push_delta(hwsdev->netdev, type,
						 &hwsdev->stats);
		rtnl_offload_xstats_notify(hwsdev->netdev);
	}
	nsim_dev_hwsdev_fini(hwsdev);

unlock_out:
	rtnl_unlock();
	return err;
}

static int
nsim_dev_hwstats_fail_ifindex(struct nsim_dev_hwstats *hwstats,
			      int ifindex,
			      enum netdev_offload_xstats_type type,
			      struct list_head *hwsdev_list)
{
	struct nsim_dev_hwstats_netdev *hwsdev;
	int err = 0;

	mutex_lock(&hwstats->hwsdev_list_lock);

	hwsdev = nsim_dev_hwslist_find_hwsdev(hwsdev_list, ifindex);
	if (!hwsdev) {
		err = -ENOENT;
		goto err_hwsdev_list_unlock;
	}

	hwsdev->fail_enable = true;

err_hwsdev_list_unlock:
	mutex_unlock(&hwstats->hwsdev_list_lock);
	return err;
}

enum nsim_dev_hwstats_do {
	NSIM_DEV_HWSTATS_DO_DISABLE,
	NSIM_DEV_HWSTATS_DO_ENABLE,
	NSIM_DEV_HWSTATS_DO_FAIL,
};

struct nsim_dev_hwstats_fops {
	enum nsim_dev_hwstats_do action;
	enum netdev_offload_xstats_type type;
};

static ssize_t
nsim_dev_hwstats_do_write(struct file *file,
			  const char __user *data,
			  size_t count, loff_t *ppos)
{
	struct nsim_dev_hwstats *hwstats = file->private_data;
	const struct nsim_dev_hwstats_fops *hwsfops;
	struct list_head *hwsdev_list;
	int ifindex;
	int err;

	hwsfops = debugfs_get_aux(file);

	err = kstrtoint_from_user(data, count, 0, &ifindex);
	if (err)
		return err;

	hwsdev_list = nsim_dev_hwstats_get_list_head(hwstats, hwsfops->type);
	if (WARN_ON(!hwsdev_list))
		return -EINVAL;

	switch (hwsfops->action) {
	case NSIM_DEV_HWSTATS_DO_DISABLE:
		err = nsim_dev_hwstats_disable_ifindex(hwstats, ifindex,
						       hwsfops->type,
						       hwsdev_list);
		break;
	case NSIM_DEV_HWSTATS_DO_ENABLE:
		err = nsim_dev_hwstats_enable_ifindex(hwstats, ifindex,
						      hwsfops->type,
						      hwsdev_list);
		break;
	case NSIM_DEV_HWSTATS_DO_FAIL:
		err = nsim_dev_hwstats_fail_ifindex(hwstats, ifindex,
						    hwsfops->type,
						    hwsdev_list);
		break;
	}
	if (err)
		return err;

	return count;
}

static struct debugfs_short_fops debugfs_ops = {
	.write = nsim_dev_hwstats_do_write,
	.llseek = generic_file_llseek,
};

#define NSIM_DEV_HWSTATS_FOPS(ACTION, TYPE)			\
	{							\
		.action = ACTION,				\
		.type = TYPE,					\
	}

static const struct nsim_dev_hwstats_fops nsim_dev_hwstats_l3_disable_fops =
	NSIM_DEV_HWSTATS_FOPS(NSIM_DEV_HWSTATS_DO_DISABLE,
			      NETDEV_OFFLOAD_XSTATS_TYPE_L3);

static const struct nsim_dev_hwstats_fops nsim_dev_hwstats_l3_enable_fops =
	NSIM_DEV_HWSTATS_FOPS(NSIM_DEV_HWSTATS_DO_ENABLE,
			      NETDEV_OFFLOAD_XSTATS_TYPE_L3);

static const struct nsim_dev_hwstats_fops nsim_dev_hwstats_l3_fail_fops =
	NSIM_DEV_HWSTATS_FOPS(NSIM_DEV_HWSTATS_DO_FAIL,
			      NETDEV_OFFLOAD_XSTATS_TYPE_L3);

#undef NSIM_DEV_HWSTATS_FOPS

int nsim_dev_hwstats_init(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_hwstats *hwstats = &nsim_dev->hwstats;
	struct net *net = nsim_dev_net(nsim_dev);
	int err;

	mutex_init(&hwstats->hwsdev_list_lock);
	INIT_LIST_HEAD(&hwstats->l3_list);

	hwstats->netdevice_nb.notifier_call = nsim_dev_netdevice_event;
	err = register_netdevice_notifier_net(net, &hwstats->netdevice_nb);
	if (err)
		goto err_mutex_destroy;

	hwstats->ddir = debugfs_create_dir("hwstats", nsim_dev->ddir);
	if (IS_ERR(hwstats->ddir)) {
		err = PTR_ERR(hwstats->ddir);
		goto err_unregister_notifier;
	}

	hwstats->l3_ddir = debugfs_create_dir("l3", hwstats->ddir);
	if (IS_ERR(hwstats->l3_ddir)) {
		err = PTR_ERR(hwstats->l3_ddir);
		goto err_remove_hwstats_recursive;
	}

	debugfs_create_file_aux("enable_ifindex", 0200, hwstats->l3_ddir, hwstats,
			    &nsim_dev_hwstats_l3_enable_fops, &debugfs_ops);
	debugfs_create_file_aux("disable_ifindex", 0200, hwstats->l3_ddir, hwstats,
			    &nsim_dev_hwstats_l3_disable_fops, &debugfs_ops);
	debugfs_create_file_aux("fail_next_enable", 0200, hwstats->l3_ddir, hwstats,
			    &nsim_dev_hwstats_l3_fail_fops, &debugfs_ops);

	INIT_DELAYED_WORK(&hwstats->traffic_dw,
			  &nsim_dev_hwstats_traffic_work);
	schedule_delayed_work(&hwstats->traffic_dw,
			      msecs_to_jiffies(NSIM_DEV_HWSTATS_TRAFFIC_MS));
	return 0;

err_remove_hwstats_recursive:
	debugfs_remove_recursive(hwstats->ddir);
err_unregister_notifier:
	unregister_netdevice_notifier_net(net, &hwstats->netdevice_nb);
err_mutex_destroy:
	mutex_destroy(&hwstats->hwsdev_list_lock);
	return err;
}

static void nsim_dev_hwsdev_list_wipe(struct nsim_dev_hwstats *hwstats,
				      enum netdev_offload_xstats_type type)
{
	struct nsim_dev_hwstats_netdev *hwsdev, *tmp;
	struct list_head *hwsdev_list;

	hwsdev_list = nsim_dev_hwstats_get_list_head(hwstats, type);
	if (WARN_ON(!hwsdev_list))
		return;

	mutex_lock(&hwstats->hwsdev_list_lock);
	list_for_each_entry_safe(hwsdev, tmp, hwsdev_list, list) {
		list_del(&hwsdev->list);
		nsim_dev_hwsdev_fini(hwsdev);
	}
	mutex_unlock(&hwstats->hwsdev_list_lock);
}

void nsim_dev_hwstats_exit(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_hwstats *hwstats = &nsim_dev->hwstats;
	struct net *net = nsim_dev_net(nsim_dev);

	cancel_delayed_work_sync(&hwstats->traffic_dw);
	debugfs_remove_recursive(hwstats->ddir);
	unregister_netdevice_notifier_net(net, &hwstats->netdevice_nb);
	nsim_dev_hwsdev_list_wipe(hwstats, NETDEV_OFFLOAD_XSTATS_TYPE_L3);
	mutex_destroy(&hwstats->hwsdev_list_lock);
}
