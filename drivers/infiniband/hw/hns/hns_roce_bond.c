// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 Hisilicon Limited.
 */

#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"
#include "hns_roce_bond.h"

static DEFINE_XARRAY(roce_bond_xa);

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

		bond_grp->bus_num = bus_num;

		ret = alloc_bond_id(bond_grp);
		if (ret) {
			dev_err(hr_dev->dev,
				"failed to alloc bond ID, ret = %d.\n", ret);
			goto alloc_id_err;
		}

		bgrps[i] = bond_grp;
	}

	return 0;

alloc_id_err:
	kvfree(bond_grp);
mem_err:
	for (i--; i >= 0; i--) {
		remove_bond_id(bgrps[i]->bus_num, bgrps[i]->bond_id);
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
			remove_bond_id(bond_grp->bus_num, bond_grp->bond_id);
			kvfree(bond_grp);
		}
	}
}
