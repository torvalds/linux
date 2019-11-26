/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_PORT_H
#define _MLXSW_PORT_H

#include <linux/types.h>

#define MLXSW_PORT_MAX_MTU		10000

#define MLXSW_PORT_DEFAULT_VID		1

#define MLXSW_PORT_SWID_DISABLED_PORT	255
#define MLXSW_PORT_SWID_ALL_SWIDS	254
#define MLXSW_PORT_SWID_TYPE_IB		1
#define MLXSW_PORT_SWID_TYPE_ETH	2

#define MLXSW_PORT_MID			0xd000

#define MLXSW_PORT_MAX_IB_PHY_PORTS	36
#define MLXSW_PORT_MAX_IB_PORTS		(MLXSW_PORT_MAX_IB_PHY_PORTS + 1)

#define MLXSW_PORT_CPU_PORT		0x0

#define MLXSW_PORT_DONT_CARE		0xFF

enum mlxsw_port_admin_status {
	MLXSW_PORT_ADMIN_STATUS_UP = 1,
	MLXSW_PORT_ADMIN_STATUS_DOWN = 2,
	MLXSW_PORT_ADMIN_STATUS_UP_ONCE = 3,
	MLXSW_PORT_ADMIN_STATUS_DISABLED = 4,
};

enum mlxsw_reg_pude_oper_status {
	MLXSW_PORT_OPER_STATUS_UP = 1,
	MLXSW_PORT_OPER_STATUS_DOWN = 2,
	MLXSW_PORT_OPER_STATUS_FAILURE = 4,	/* Can be set to up again. */
};

#endif /* _MLXSW_PORT_H */
