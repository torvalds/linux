/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_router.h
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Arkadi Sharshevsky <arkadis@mellanox.com>
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

#ifndef _MLXSW_ROUTER_H_
#define _MLXSW_ROUTER_H_

#include "spectrum.h"
#include "reg.h"

enum mlxsw_sp_l3proto {
	MLXSW_SP_L3_PROTO_IPV4,
	MLXSW_SP_L3_PROTO_IPV6,
};

union mlxsw_sp_l3addr {
	__be32 addr4;
	struct in6_addr addr6;
};

struct mlxsw_sp_rif_ipip_lb;
struct mlxsw_sp_rif_ipip_lb_config {
	enum mlxsw_reg_ritr_loopback_ipip_type lb_ipipt;
	u32 okey;
	enum mlxsw_sp_l3proto ul_protocol; /* Underlay. */
	union mlxsw_sp_l3addr saddr;
};

enum mlxsw_sp_rif_counter_dir {
	MLXSW_SP_RIF_COUNTER_INGRESS,
	MLXSW_SP_RIF_COUNTER_EGRESS,
};

struct mlxsw_sp_neigh_entry;

struct mlxsw_sp_rif *mlxsw_sp_rif_by_index(const struct mlxsw_sp *mlxsw_sp,
					   u16 rif_index);
u16 mlxsw_sp_rif_index(const struct mlxsw_sp_rif *rif);
u16 mlxsw_sp_ipip_lb_rif_index(const struct mlxsw_sp_rif_ipip_lb *rif);
u16 mlxsw_sp_ipip_lb_ul_vr_id(const struct mlxsw_sp_rif_ipip_lb *rif);
int mlxsw_sp_rif_dev_ifindex(const struct mlxsw_sp_rif *rif);
int mlxsw_sp_rif_counter_value_get(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_rif *rif,
				   enum mlxsw_sp_rif_counter_dir dir,
				   u64 *cnt);
void mlxsw_sp_rif_counter_free(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir);
int mlxsw_sp_rif_counter_alloc(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir);
struct mlxsw_sp_neigh_entry *
mlxsw_sp_rif_neigh_next(struct mlxsw_sp_rif *rif,
			struct mlxsw_sp_neigh_entry *neigh_entry);
int mlxsw_sp_neigh_entry_type(struct mlxsw_sp_neigh_entry *neigh_entry);
unsigned char *
mlxsw_sp_neigh_entry_ha(struct mlxsw_sp_neigh_entry *neigh_entry);
u32 mlxsw_sp_neigh4_entry_dip(struct mlxsw_sp_neigh_entry *neigh_entry);
struct in6_addr *
mlxsw_sp_neigh6_entry_dip(struct mlxsw_sp_neigh_entry *neigh_entry);

#define mlxsw_sp_rif_neigh_for_each(neigh_entry, rif)				\
	for (neigh_entry = mlxsw_sp_rif_neigh_next(rif, NULL); neigh_entry;	\
	     neigh_entry = mlxsw_sp_rif_neigh_next(rif, neigh_entry))
int mlxsw_sp_neigh_counter_get(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_neigh_entry *neigh_entry,
			       u64 *p_counter);
void
mlxsw_sp_neigh_entry_counter_update(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_neigh_entry *neigh_entry,
				    bool adding);
bool mlxsw_sp_neigh_ipv6_ignore(struct mlxsw_sp_neigh_entry *neigh_entry);
union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_saddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev);
union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_daddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev);
__be32 mlxsw_sp_ipip_netdev_daddr4(const struct net_device *ol_dev);

#endif /* _MLXSW_ROUTER_H_*/
