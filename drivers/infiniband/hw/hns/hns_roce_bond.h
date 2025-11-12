/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 Hisilicon Limited.
 */

#ifndef _HNS_ROCE_BOND_H
#define _HNS_ROCE_BOND_H

#include <linux/netdevice.h>
#include <net/bonding.h>

#define ROCE_BOND_FUNC_MAX 4
#define ROCE_BOND_NUM_MAX 2

#define BOND_ID(id) BIT(id)

enum bond_support_type {
	BOND_NOT_SUPPORT,
	/*
	 * bond_grp already exists, but in the current
	 * conditions it's no longer supported
	 */
	BOND_EXISTING_NOT_SUPPORT,
	BOND_SUPPORT,
};

enum hns_roce_bond_state {
	HNS_ROCE_BOND_NOT_ATTACHED,
	HNS_ROCE_BOND_NOT_BONDED,
	HNS_ROCE_BOND_IS_BONDED,
	HNS_ROCE_BOND_SLAVE_CHANGE_NUM,
	HNS_ROCE_BOND_SLAVE_CHANGESTATE,
};

struct hns_roce_func_info {
	struct net_device *net_dev;
	struct hnae3_handle *handle;
};

struct hns_roce_bond_group {
	struct net_device *upper_dev;
	struct hns_roce_dev *main_hr_dev;
	u8 bond_id;
	u8 bus_num;
	struct hns_roce_func_info bond_func_info[ROCE_BOND_FUNC_MAX];
	bool bond_ready;
	enum hns_roce_bond_state bond_state;
	enum netdev_lag_tx_type tx_type;
	enum netdev_lag_hash hash_type;
	struct mutex bond_mutex;
	struct notifier_block bond_nb;
};

struct hns_roce_die_info {
	u8 bond_id_mask;
	struct hns_roce_bond_group *bgrps[ROCE_BOND_NUM_MAX];
};

struct hns_roce_bond_group *hns_roce_get_bond_grp(struct net_device *net_dev,
						  u8 bus_num);
int hns_roce_alloc_bond_grp(struct hns_roce_dev *hr_dev);
void hns_roce_dealloc_bond_grp(void);

#endif
