/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_TRAP_H
#define _MLXSW_SPECTRUM_TRAP_H

#include <linux/list.h>
#include <net/devlink.h>

struct mlxsw_sp_trap {
	struct mlxsw_sp_trap_policer_item *policer_items_arr;
	u64 policers_count; /* Number of registered policers */

	struct mlxsw_sp_trap_group_item *group_items_arr;
	u64 groups_count; /* Number of registered groups */

	struct mlxsw_sp_trap_item *trap_items_arr;
	u64 traps_count; /* Number of registered traps */

	u16 thin_policer_hw_id;

	u64 max_policers;
	unsigned long policers_usage[]; /* Usage bitmap */
};

#endif
