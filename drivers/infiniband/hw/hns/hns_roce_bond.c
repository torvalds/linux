// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 Hisilicon Limited.
 */

#include <net/bonding.h>
#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"
#include "hns_roce_bond.h"

static DEFINE_XARRAY(roce_bond_xa);

static struct hns_roce_dev *hns_roce_get_hrdev_by_netdev(struct net_device *net_dev)
{
	struct ib_device *ibdev =
		ib_device_get_by_netdev(net_dev, RDMA_DRIVER_HNS);

	if (!ibdev)
		return NULL;

	return container_of(ibdev, struct hns_roce_dev, ib_dev);
}

static struct net_device *get_upper_dev_from_ndev(struct net_device *net_dev)
{
	struct net_device *upper_dev;

	rcu_read_lock();
	upper_dev = netdev_master_upper_dev_get_rcu(net_dev);
	dev_hold(upper_dev);
	rcu_read_unlock();

	return upper_dev;
}

static int get_netdev_bond_slave_id(struct net_device *net_dev,
				    struct hns_roce_bond_group *bond_grp)
{
	int i;

	for (i = 0; i < ROCE_BOND_FUNC_MAX; i++)
		if (net_dev == bond_grp->bond_func_info[i].net_dev)
			return i;

	return -ENOENT;
}

struct hns_roce_bond_group *hns_roce_get_bond_grp(struct net_device *net_dev,
						  u8 bus_num)
{
	struct hns_roce_die_info *die_info = xa_load(&roce_bond_xa, bus_num);
	struct hns_roce_bond_group *bond_grp;
	struct net_device *upper_dev = NULL;
	int i;

	if (!die_info)
		return NULL;

	for (i = 0; i < ROCE_BOND_NUM_MAX; i++) {
		bond_grp = die_info->bgrps[i];
		if (!bond_grp)
			continue;
		if (get_netdev_bond_slave_id(net_dev, bond_grp) >= 0)
			return bond_grp;
		if (bond_grp->upper_dev) {
			upper_dev = get_upper_dev_from_ndev(net_dev);
			if (bond_grp->upper_dev == upper_dev) {
				dev_put(upper_dev);
				return bond_grp;
			}
			dev_put(upper_dev);
		}
	}

	return NULL;
}

static int hns_roce_set_bond_netdev(struct hns_roce_bond_group *bond_grp,
				    struct hns_roce_dev *hr_dev)
{
	struct net_device *active_dev;
	struct net_device *old_dev;
	int i, ret = 0;

	if (bond_grp->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		rcu_read_lock();
		active_dev =
			bond_option_active_slave_get_rcu(netdev_priv(bond_grp->upper_dev));
		rcu_read_unlock();
	} else {
		for (i = 0; i < ROCE_BOND_FUNC_MAX; i++) {
			active_dev = bond_grp->bond_func_info[i].net_dev;
			if (active_dev &&
			    ib_get_curr_port_state(active_dev) == IB_PORT_ACTIVE)
				break;
		}
	}

	if (!active_dev || i == ROCE_BOND_FUNC_MAX)
		active_dev = get_hr_netdev(hr_dev, 0);

	old_dev = ib_device_get_netdev(&hr_dev->ib_dev, 1);
	if (old_dev == active_dev)
		goto out;

	ret = ib_device_set_netdev(&hr_dev->ib_dev, active_dev, 1);
	if (ret) {
		dev_err(hr_dev->dev, "failed to set netdev for bond.\n");
		goto out;
	}

	if (bond_grp->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		if (old_dev)
			roce_del_all_netdev_gids(&hr_dev->ib_dev, 1, old_dev);
		rdma_roce_rescan_port(&hr_dev->ib_dev, 1);
	}
out:
	dev_put(old_dev);
	return ret;
}

bool hns_roce_bond_is_active(struct hns_roce_dev *hr_dev)
{
	struct net_device *net_dev = get_hr_netdev(hr_dev, 0);
	struct hns_roce_bond_group *bond_grp;
	u8 bus_num = get_hr_bus_num(hr_dev);

	bond_grp = hns_roce_get_bond_grp(net_dev, bus_num);
	if (bond_grp && bond_grp->bond_state != HNS_ROCE_BOND_NOT_BONDED &&
	    bond_grp->bond_state != HNS_ROCE_BOND_NOT_ATTACHED)
		return true;

	return false;
}

static void hns_roce_slave_uninit(struct hns_roce_bond_group *bond_grp,
				  u8 func_idx)
{
	struct hnae3_handle *handle;

	handle = bond_grp->bond_func_info[func_idx].handle;
	if (handle->priv)
		hns_roce_bond_uninit_client(bond_grp, func_idx);
}

static struct hns_roce_dev
	*hns_roce_slave_init(struct hns_roce_bond_group *bond_grp,
			     u8 func_idx, bool need_switch);

static int switch_main_dev(struct hns_roce_bond_group *bond_grp,
			   u8 main_func_idx)
{
	struct hns_roce_dev *hr_dev;
	struct net_device *net_dev;
	u8 i;

	bond_grp->main_hr_dev = NULL;
	hns_roce_bond_uninit_client(bond_grp, main_func_idx);

	for (i = 0; i < ROCE_BOND_FUNC_MAX; i++) {
		net_dev = bond_grp->bond_func_info[i].net_dev;
		if ((bond_grp->slave_map & (1U << i)) && net_dev) {
			/* In case this slave is still being registered as
			 * a non-bonded PF, uninit it first and then re-init
			 * it as the main device.
			 */
			hns_roce_slave_uninit(bond_grp, i);
			hr_dev = hns_roce_slave_init(bond_grp, i, false);
			if (hr_dev) {
				bond_grp->main_hr_dev = hr_dev;
				break;
			}
		}
	}

	if (!bond_grp->main_hr_dev)
		return -ENODEV;

	return 0;
}

static struct hns_roce_dev
	*hns_roce_slave_init(struct hns_roce_bond_group *bond_grp,
			     u8 func_idx, bool need_switch)
{
	struct hns_roce_dev *hr_dev = NULL;
	struct hnae3_handle *handle;
	u8 main_func_idx;
	int ret;

	if (need_switch) {
		main_func_idx = PCI_FUNC(bond_grp->main_hr_dev->pci_dev->devfn);
		if (func_idx == main_func_idx) {
			ret = switch_main_dev(bond_grp, main_func_idx);
			if (ret == -ENODEV)
				return NULL;
		}
	}

	handle = bond_grp->bond_func_info[func_idx].handle;
	if (handle) {
		if (handle->priv)
			return handle->priv;
		/* Prevent this device from being initialized as a bond device */
		if (need_switch)
			bond_grp->bond_func_info[func_idx].net_dev = NULL;
		hr_dev = hns_roce_bond_init_client(bond_grp, func_idx);
		if (!hr_dev)
			BOND_ERR_LOG("failed to init slave %u.\n", func_idx);
	}

	return hr_dev;
}

static struct hns_roce_die_info *alloc_die_info(int bus_num)
{
	struct hns_roce_die_info *die_info;
	int ret;

	die_info = kzalloc(sizeof(*die_info), GFP_KERNEL);
	if (!die_info)
		return NULL;

	ret = xa_err(xa_store(&roce_bond_xa, bus_num, die_info, GFP_KERNEL));
	if (ret) {
		kfree(die_info);
		return NULL;
	}

	return die_info;
}

static void dealloc_die_info(struct hns_roce_die_info *die_info, u8 bus_num)
{
	xa_erase(&roce_bond_xa, bus_num);
	kfree(die_info);
}

static int alloc_bond_id(struct hns_roce_bond_group *bond_grp)
{
	u8 bus_num = bond_grp->bus_num;
	struct hns_roce_die_info *die_info = xa_load(&roce_bond_xa, bus_num);
	int i;

	if (!die_info) {
		die_info = alloc_die_info(bus_num);
		if (!die_info)
			return -ENOMEM;
	}

	for (i = 0; i < ROCE_BOND_NUM_MAX; i++) {
		if (die_info->bond_id_mask & BOND_ID(i))
			continue;

		die_info->bond_id_mask |= BOND_ID(i);
		die_info->bgrps[i] = bond_grp;
		bond_grp->bond_id = i;

		return 0;
	}

	return -ENOSPC;
}

static int remove_bond_id(int bus_num, u8 bond_id)
{
	struct hns_roce_die_info *die_info = xa_load(&roce_bond_xa, bus_num);

	if (bond_id >= ROCE_BOND_NUM_MAX)
		return -EINVAL;

	if (!die_info)
		return -ENODEV;

	die_info->bond_id_mask &= ~BOND_ID(bond_id);
	die_info->bgrps[bond_id] = NULL;
	if (!die_info->bond_id_mask)
		dealloc_die_info(die_info, bus_num);

	return 0;
}

static bool is_dev_bond_supported(struct hns_roce_bond_group *bond_grp,
				  struct net_device *net_dev)
{
	struct hns_roce_dev *hr_dev = hns_roce_get_hrdev_by_netdev(net_dev);
	bool ret = true;

	if (!hr_dev) {
		if (bond_grp &&
		    get_netdev_bond_slave_id(net_dev, bond_grp) >= 0)
			return true;
		else
			return false;
	}

	if (!(hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_BOND)) {
		ret = false;
		goto out;
	}

	if (hr_dev->is_vf || pci_num_vf(hr_dev->pci_dev) > 0) {
		ret = false;
		goto out;
	}

	if (bond_grp->bus_num != get_hr_bus_num(hr_dev))
		ret = false;

out:
	ib_device_put(&hr_dev->ib_dev);
	return ret;
}

static bool check_slave_support(struct hns_roce_bond_group *bond_grp,
				struct net_device *upper_dev)
{
	struct net_device *net_dev;
	u8 slave_num = 0;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(upper_dev, net_dev) {
		if (is_dev_bond_supported(bond_grp, net_dev)) {
			slave_num++;
			continue;
		}
		rcu_read_unlock();
		return false;
	}
	rcu_read_unlock();

	return (slave_num > 1 && slave_num <= ROCE_BOND_FUNC_MAX);
}

static void hns_roce_attach_bond_grp(struct hns_roce_bond_group *bond_grp,
				     struct hns_roce_dev *hr_dev,
				     struct net_device *upper_dev)
{
	bond_grp->upper_dev = upper_dev;
	bond_grp->main_hr_dev = hr_dev;
	bond_grp->bond_state = HNS_ROCE_BOND_NOT_BONDED;
	bond_grp->bond_ready = false;
}

static void hns_roce_detach_bond_grp(struct hns_roce_bond_group *bond_grp)
{
	mutex_lock(&bond_grp->bond_mutex);

	bond_grp->upper_dev = NULL;
	bond_grp->main_hr_dev = NULL;
	bond_grp->bond_ready = false;
	bond_grp->bond_state = HNS_ROCE_BOND_NOT_ATTACHED;
	bond_grp->slave_map = 0;
	memset(bond_grp->bond_func_info, 0, sizeof(bond_grp->bond_func_info));

	mutex_unlock(&bond_grp->bond_mutex);
}

void hns_roce_cleanup_bond(struct hns_roce_bond_group *bond_grp)
{
	int ret;

	ret = bond_grp->main_hr_dev ?
	      hns_roce_cmd_bond(bond_grp, HNS_ROCE_CLEAR_BOND) : -EIO;
	if (ret)
		BOND_ERR_LOG("failed to clear RoCE bond, ret = %d.\n", ret);
	else
		ibdev_info(&bond_grp->main_hr_dev->ib_dev,
			   "RoCE clear bond finished!\n");

	hns_roce_detach_bond_grp(bond_grp);
}

static bool lowerstate_event_filter(struct hns_roce_bond_group *bond_grp,
				    struct net_device *net_dev)
{
	struct hns_roce_bond_group *bond_grp_tmp;

	bond_grp_tmp = hns_roce_get_bond_grp(net_dev, bond_grp->bus_num);
	return bond_grp_tmp == bond_grp;
}

static void lowerstate_event_setting(struct hns_roce_bond_group *bond_grp,
				     struct netdev_notifier_changelowerstate_info *info)
{
	mutex_lock(&bond_grp->bond_mutex);

	if (bond_grp->bond_ready &&
	    bond_grp->bond_state == HNS_ROCE_BOND_IS_BONDED)
		bond_grp->bond_state = HNS_ROCE_BOND_SLAVE_CHANGESTATE;

	mutex_unlock(&bond_grp->bond_mutex);
}

static bool hns_roce_bond_lowerstate_event(struct hns_roce_bond_group *bond_grp,
					   struct netdev_notifier_changelowerstate_info *info)
{
	struct net_device *net_dev =
		netdev_notifier_info_to_dev((struct netdev_notifier_info *)info);

	if (!netif_is_lag_port(net_dev))
		return false;

	if (!lowerstate_event_filter(bond_grp, net_dev))
		return false;

	lowerstate_event_setting(bond_grp, info);

	return true;
}

static bool is_bond_setting_supported(struct netdev_lag_upper_info *bond_info)
{
	if (!bond_info)
		return false;

	if (bond_info->tx_type != NETDEV_LAG_TX_TYPE_ACTIVEBACKUP &&
	    bond_info->tx_type != NETDEV_LAG_TX_TYPE_HASH)
		return false;

	if (bond_info->tx_type == NETDEV_LAG_TX_TYPE_HASH &&
	    bond_info->hash_type > NETDEV_LAG_HASH_L23)
		return false;

	return true;
}

static void upper_event_setting(struct hns_roce_bond_group *bond_grp,
				struct netdev_notifier_changeupper_info *info)
{
	struct netdev_lag_upper_info *bond_upper_info = NULL;
	bool slave_inc = info->linking;

	if (slave_inc)
		bond_upper_info = info->upper_info;

	if (bond_upper_info) {
		bond_grp->tx_type = bond_upper_info->tx_type;
		bond_grp->hash_type = bond_upper_info->hash_type;
	}
}

static bool check_unlinking_bond_support(struct hns_roce_bond_group *bond_grp)
{
	struct net_device *net_dev;
	u8 slave_num = 0;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(bond_grp->upper_dev, net_dev) {
		if (get_netdev_bond_slave_id(net_dev, bond_grp) >= 0)
			slave_num++;
	}
	rcu_read_unlock();

	return (slave_num > 1);
}

static bool check_linking_bond_support(struct netdev_lag_upper_info *bond_info,
				       struct hns_roce_bond_group *bond_grp,
				       struct net_device *upper_dev)
{
	if (!is_bond_setting_supported(bond_info))
		return false;

	return check_slave_support(bond_grp, upper_dev);
}

static enum bond_support_type
	check_bond_support(struct hns_roce_bond_group *bond_grp,
			   struct net_device *upper_dev,
			   struct netdev_notifier_changeupper_info *info)
{
	bool bond_grp_exist = false;
	bool support;

	if (upper_dev == bond_grp->upper_dev)
		bond_grp_exist = true;

	if (!info->linking && !bond_grp_exist)
		return BOND_NOT_SUPPORT;

	if (info->linking)
		support = check_linking_bond_support(info->upper_info, bond_grp,
						     upper_dev);
	else
		support = check_unlinking_bond_support(bond_grp);

	if (support)
		return BOND_SUPPORT;

	return bond_grp_exist ? BOND_EXISTING_NOT_SUPPORT : BOND_NOT_SUPPORT;
}

static bool upper_event_filter(struct netdev_notifier_changeupper_info *info,
			       struct hns_roce_bond_group *bond_grp,
			       struct net_device *net_dev)
{
	struct net_device *upper_dev = info->upper_dev;
	struct hns_roce_bond_group *bond_grp_tmp;
	struct hns_roce_dev *hr_dev;
	bool ret = true;
	u8 bus_num;

	if (!info->linking ||
	    bond_grp->bond_state != HNS_ROCE_BOND_NOT_ATTACHED)
		return bond_grp->upper_dev == upper_dev;

	hr_dev = hns_roce_get_hrdev_by_netdev(net_dev);
	if (!hr_dev)
		return false;

	bus_num = get_hr_bus_num(hr_dev);
	if (bond_grp->bus_num != bus_num) {
		ret = false;
		goto out;
	}

	bond_grp_tmp = hns_roce_get_bond_grp(net_dev, bus_num);
	if (bond_grp_tmp && bond_grp_tmp != bond_grp)
		ret = false;
out:
	ib_device_put(&hr_dev->ib_dev);
	return ret;
}

static bool hns_roce_bond_upper_event(struct hns_roce_bond_group *bond_grp,
				      struct netdev_notifier_changeupper_info *info)
{
	struct net_device *net_dev =
		netdev_notifier_info_to_dev((struct netdev_notifier_info *)info);
	struct net_device *upper_dev = info->upper_dev;
	enum bond_support_type support = BOND_SUPPORT;
	struct hns_roce_dev *hr_dev;
	int slave_id;

	if (!upper_dev || !netif_is_lag_master(upper_dev))
		return false;

	if (!upper_event_filter(info, bond_grp, net_dev))
		return false;

	mutex_lock(&bond_grp->bond_mutex);
	support = check_bond_support(bond_grp, upper_dev, info);
	if (support == BOND_NOT_SUPPORT) {
		mutex_unlock(&bond_grp->bond_mutex);
		return false;
	}

	if (bond_grp->bond_state == HNS_ROCE_BOND_NOT_ATTACHED) {
		hr_dev = hns_roce_get_hrdev_by_netdev(net_dev);
		if (!hr_dev) {
			mutex_unlock(&bond_grp->bond_mutex);
			return false;
		}
		hns_roce_attach_bond_grp(bond_grp, hr_dev, upper_dev);
		ib_device_put(&hr_dev->ib_dev);
	}

	/* In the case of netdev being unregistered, the roce
	 * instance shouldn't be inited.
	 */
	if (net_dev->reg_state >= NETREG_UNREGISTERING) {
		slave_id = get_netdev_bond_slave_id(net_dev, bond_grp);
		if (slave_id >= 0) {
			bond_grp->bond_func_info[slave_id].net_dev = NULL;
			bond_grp->bond_func_info[slave_id].handle = NULL;
		}
	}

	if (support == BOND_SUPPORT) {
		bond_grp->bond_ready = true;
		if (bond_grp->bond_state != HNS_ROCE_BOND_NOT_BONDED)
			bond_grp->bond_state = HNS_ROCE_BOND_SLAVE_CHANGE_NUM;
	}
	mutex_unlock(&bond_grp->bond_mutex);
	if (support == BOND_SUPPORT)
		upper_event_setting(bond_grp, info);

	return true;
}

static int hns_roce_bond_event(struct notifier_block *self,
			       unsigned long event, void *ptr)
{
	struct hns_roce_bond_group *bond_grp =
		container_of(self, struct hns_roce_bond_group, bond_nb);
	bool changed = false;

	if (event == NETDEV_CHANGEUPPER)
		changed = hns_roce_bond_upper_event(bond_grp, ptr);
	if (event == NETDEV_CHANGELOWERSTATE)
		changed = hns_roce_bond_lowerstate_event(bond_grp, ptr);

	return NOTIFY_DONE;
}

int hns_roce_alloc_bond_grp(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_bond_group *bgrps[ROCE_BOND_NUM_MAX];
	struct hns_roce_bond_group *bond_grp;
	u8 bus_num = get_hr_bus_num(hr_dev);
	int ret;
	int i;

	if (xa_load(&roce_bond_xa, bus_num))
		return 0;

	for (i = 0; i < ROCE_BOND_NUM_MAX; i++) {
		bond_grp = kvzalloc(sizeof(*bond_grp), GFP_KERNEL);
		if (!bond_grp) {
			ret = -ENOMEM;
			goto mem_err;
		}

		mutex_init(&bond_grp->bond_mutex);

		bond_grp->bond_ready = false;
		bond_grp->bond_state = HNS_ROCE_BOND_NOT_ATTACHED;
		bond_grp->bus_num = bus_num;

		ret = alloc_bond_id(bond_grp);
		if (ret) {
			dev_err(hr_dev->dev,
				"failed to alloc bond ID, ret = %d.\n", ret);
			goto alloc_id_err;
		}

		bond_grp->bond_nb.notifier_call = hns_roce_bond_event;
		ret = register_netdevice_notifier(&bond_grp->bond_nb);
		if (ret) {
			ibdev_err(&hr_dev->ib_dev,
				  "failed to register bond nb, ret = %d.\n", ret);
			goto register_nb_err;
		}
		bgrps[i] = bond_grp;
	}

	return 0;

register_nb_err:
	remove_bond_id(bond_grp->bus_num, bond_grp->bond_id);
alloc_id_err:
	mutex_destroy(&bond_grp->bond_mutex);
	kvfree(bond_grp);
mem_err:
	for (i--; i >= 0; i--) {
		unregister_netdevice_notifier(&bgrps[i]->bond_nb);
		remove_bond_id(bgrps[i]->bus_num, bgrps[i]->bond_id);
		mutex_destroy(&bgrps[i]->bond_mutex);
		kvfree(bgrps[i]);
	}
	return ret;
}

void hns_roce_dealloc_bond_grp(void)
{
	struct hns_roce_bond_group *bond_grp;
	struct hns_roce_die_info *die_info;
	unsigned long id;
	int i;

	xa_for_each(&roce_bond_xa, id, die_info) {
		for (i = 0; i < ROCE_BOND_NUM_MAX; i++) {
			bond_grp = die_info->bgrps[i];
			if (!bond_grp)
				continue;
			unregister_netdevice_notifier(&bond_grp->bond_nb);
			remove_bond_id(bond_grp->bus_num, bond_grp->bond_id);
			mutex_destroy(&bond_grp->bond_mutex);
			kvfree(bond_grp);
		}
	}
}

int hns_roce_bond_init(struct hns_roce_dev *hr_dev)
{
	struct net_device *net_dev = get_hr_netdev(hr_dev, 0);
	struct hns_roce_bond_group *bond_grp;
	u8 bus_num = get_hr_bus_num(hr_dev);

	bond_grp = hns_roce_get_bond_grp(net_dev, bus_num);

	return hns_roce_set_bond_netdev(bond_grp, hr_dev);
}
