/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_TRAP_H
#define _MLXSW_SPECTRUM_TRAP_H

#include <net/devlink.h>

struct mlxsw_sp_trap {
	struct devlink_trap_policer *policers_arr; /* Registered policers */
	u64 policers_count; /* Number of registered policers */
	u64 max_policers;
	unsigned long policers_usage[]; /* Usage bitmap */
};

#endif
