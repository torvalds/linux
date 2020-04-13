/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_TRAP_H
#define _MLXSW_SPECTRUM_TRAP_H

#include <linux/list.h>
#include <net/devlink.h>

struct mlxsw_sp_trap {
	struct devlink_trap_policer *policers_arr; /* Registered policers */
	u64 policers_count; /* Number of registered policers */
	struct list_head policer_item_list;
	u64 max_policers;
	unsigned long policers_usage[]; /* Usage bitmap */
};

struct mlxsw_sp_trap_policer_item {
	u16 hw_id;
	u32 id;
	struct list_head list; /* Member of policer_item_list */
};

#endif
