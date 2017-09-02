/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_ipip.h
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Petr Machata <petrm@mellanox.com>
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

#ifndef _MLXSW_IPIP_H_
#define _MLXSW_IPIP_H_

#include "spectrum_router.h"
#include <net/ip_fib.h>

enum mlxsw_sp_ipip_type {
	MLXSW_SP_IPIP_TYPE_GRE4,
	MLXSW_SP_IPIP_TYPE_MAX,
};

struct mlxsw_sp_ipip_entry {
	enum mlxsw_sp_ipip_type ipipt;
	struct net_device *ol_dev; /* Overlay. */
	struct mlxsw_sp_rif_ipip_lb *ol_lb;
	unsigned int ref_count; /* Number of next hops using the tunnel. */
	struct mlxsw_sp_fib_entry *decap_fib_entry;
	struct list_head ipip_list_node;
};

struct mlxsw_sp_ipip_ops {
	int dev_type;
	enum mlxsw_sp_l3proto ul_proto; /* Underlay. */

	int (*nexthop_update)(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
			      struct mlxsw_sp_ipip_entry *ipip_entry);

	bool (*can_offload)(const struct mlxsw_sp *mlxsw_sp,
			    const struct net_device *ol_dev,
			    enum mlxsw_sp_l3proto ol_proto);

	/* Return a configuration for creating an overlay loopback RIF. */
	struct mlxsw_sp_rif_ipip_lb_config
	(*ol_loopback_config)(struct mlxsw_sp *mlxsw_sp,
			      const struct net_device *ol_dev);

	int (*fib_entry_op)(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_ipip_entry *ipip_entry,
			    enum mlxsw_reg_ralue_op op,
			    u32 tunnel_index);
};

extern const struct mlxsw_sp_ipip_ops *mlxsw_sp_ipip_ops_arr[];

#endif /* _MLXSW_IPIP_H_*/
