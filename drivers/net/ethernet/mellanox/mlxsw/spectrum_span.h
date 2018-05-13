/*
 * drivers/net/ethernet/mellanox/mlxsw/mlxsw_span.h
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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

#ifndef _MLXSW_SPECTRUM_SPAN_H
#define _MLXSW_SPECTRUM_SPAN_H

#include <linux/types.h>
#include <linux/if_ether.h>

#include "spectrum_router.h"

struct mlxsw_sp;
struct mlxsw_sp_port;

enum mlxsw_sp_span_type {
	MLXSW_SP_SPAN_EGRESS,
	MLXSW_SP_SPAN_INGRESS
};

struct mlxsw_sp_span_inspected_port {
	struct list_head list;
	enum mlxsw_sp_span_type type;
	u8 local_port;

	/* Whether this is a directly bound mirror (port-to-port) or an ACL. */
	bool bound;
};

struct mlxsw_sp_span_parms {
	struct mlxsw_sp_port *dest_port; /* NULL for unoffloaded SPAN. */
	unsigned int ttl;
	unsigned char dmac[ETH_ALEN];
	unsigned char smac[ETH_ALEN];
	union mlxsw_sp_l3addr daddr;
	union mlxsw_sp_l3addr saddr;
};

struct mlxsw_sp_span_entry_ops;

struct mlxsw_sp_span_entry {
	const struct net_device *to_dev;
	const struct mlxsw_sp_span_entry_ops *ops;
	struct mlxsw_sp_span_parms parms;
	struct list_head bound_ports_list;
	int ref_count;
	int id;
};

struct mlxsw_sp_span_entry_ops {
	bool (*can_handle)(const struct net_device *to_dev);
	int (*parms)(const struct net_device *to_dev,
		     struct mlxsw_sp_span_parms *sparmsp);
	int (*configure)(struct mlxsw_sp_span_entry *span_entry,
			 struct mlxsw_sp_span_parms sparms);
	void (*deconfigure)(struct mlxsw_sp_span_entry *span_entry);
};

int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_span_respin(struct mlxsw_sp *mlxsw_sp);

int mlxsw_sp_span_mirror_add(struct mlxsw_sp_port *from,
			     const struct net_device *to_dev,
			     enum mlxsw_sp_span_type type,
			     bool bind, int *p_span_id);
void mlxsw_sp_span_mirror_del(struct mlxsw_sp_port *from, int span_id,
			      enum mlxsw_sp_span_type type, bool bind);
struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_port(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *to_dev);

void mlxsw_sp_span_entry_invalidate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_span_entry *span_entry);

int mlxsw_sp_span_port_mtu_update(struct mlxsw_sp_port *port, u16 mtu);

#endif
