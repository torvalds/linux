/*
 * drivers/net/ethernet/mellanox/mlxsw/port.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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

#define MLXSW_PORT_MODULE_MAX_WIDTH	4

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
