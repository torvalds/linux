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

#define BOND_ERR_LOG(fmt, ...)				\
	pr_err("HNS RoCE Bonding: " fmt, ##__VA_ARGS__)

enum {
	BOND_MODE_1,
	BOND_MODE_2_4,
};

enum hns_roce_bond_hashtype {
	BOND_HASH_L2,
	BOND_HASH_L34,
	BOND_HASH_L23,
};

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

enum hns_roce_bond_cmd_type {
	HNS_ROCE_SET_BOND,
	HNS_ROCE_CHANGE_BOND,
	HNS_ROCE_CLEAR_BOND,
};

struct hns_roce_func_info {
	struct net_device *net_dev;
	struct hnae3_handle *handle;
};

struct hns_roce_bond_group {
	struct net_device *upper_dev;
	struct hns_roce_dev *main_hr_dev;
	u8 active_slave_num;
	u32 slave_map;
	u32 active_slave_map;
	u8 bond_id;
	u8 bus_num;
	struct hns_roce_func_info bond_func_info[ROCE_BOND_FUNC_MAX];
	bool bond_ready;
	enum hns_roce_bond_state bond_state;
	enum netdev_lag_tx_type tx_type;
	enum netdev_lag_hash hash_type;
	struct mutex bond_mutex;
	struct notifier_block bond_nb;
	struct delayed_work bond_work;
};

struct hns_roce_die_info {
	u8 bond_id_mask;
	struct hns_roce_bond_group *bgrps[ROCE_BOND_NUM_MAX];
	struct mutex die_mutex;
	u8 suspend_cnt;
};

struct hns_roce_bond_group *hns_roce_get_bond_grp(struct net_device *net_dev,
						  u8 bus_num);
int hns_roce_alloc_bond_grp(struct hns_roce_dev *hr_dev);
void hns_roce_dealloc_bond_grp(void);
void hns_roce_cleanup_bond(struct hns_roce_bond_group *bond_grp);
bool hns_roce_bond_is_active(struct hns_roce_dev *hr_dev);
int hns_roce_bond_init(struct hns_roce_dev *hr_dev);
void hns_roce_bond_suspend(struct hnae3_handle *handle);
void hns_roce_bond_resume(struct hnae3_handle *handle);

#endif
