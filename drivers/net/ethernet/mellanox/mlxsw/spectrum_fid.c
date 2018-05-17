/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_fid.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Ido Schimmel <idosch@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

#include "spectrum.h"
#include "reg.h"

struct mlxsw_sp_fid_family;

struct mlxsw_sp_fid_core {
	struct mlxsw_sp_fid_family *fid_family_arr[MLXSW_SP_FID_TYPE_MAX];
	unsigned int *port_fid_mappings;
};

struct mlxsw_sp_fid {
	struct list_head list;
	struct mlxsw_sp_rif *rif;
	unsigned int ref_count;
	u16 fid_index;
	struct mlxsw_sp_fid_family *fid_family;
};

struct mlxsw_sp_fid_8021q {
	struct mlxsw_sp_fid common;
	u16 vid;
};

struct mlxsw_sp_fid_8021d {
	struct mlxsw_sp_fid common;
	int br_ifindex;
};

struct mlxsw_sp_flood_table {
	enum mlxsw_sp_flood_type packet_type;
	enum mlxsw_reg_sfgc_bridge_type bridge_type;
	enum mlxsw_flood_table_type table_type;
	int table_index;
};

struct mlxsw_sp_fid_ops {
	void (*setup)(struct mlxsw_sp_fid *fid, const void *arg);
	int (*configure)(struct mlxsw_sp_fid *fid);
	void (*deconfigure)(struct mlxsw_sp_fid *fid);
	int (*index_alloc)(struct mlxsw_sp_fid *fid, const void *arg,
			   u16 *p_fid_index);
	bool (*compare)(const struct mlxsw_sp_fid *fid,
			const void *arg);
	u16 (*flood_index)(const struct mlxsw_sp_fid *fid);
	int (*port_vid_map)(struct mlxsw_sp_fid *fid,
			    struct mlxsw_sp_port *port, u16 vid);
	void (*port_vid_unmap)(struct mlxsw_sp_fid *fid,
			       struct mlxsw_sp_port *port, u16 vid);
};

struct mlxsw_sp_fid_family {
	enum mlxsw_sp_fid_type type;
	size_t fid_size;
	u16 start_index;
	u16 end_index;
	struct list_head fids_list;
	unsigned long *fids_bitmap;
	const struct mlxsw_sp_flood_table *flood_tables;
	int nr_flood_tables;
	enum mlxsw_sp_rif_type rif_type;
	const struct mlxsw_sp_fid_ops *ops;
	struct mlxsw_sp *mlxsw_sp;
};

static const int mlxsw_sp_sfgc_uc_packet_types[MLXSW_REG_SFGC_TYPE_MAX] = {
	[MLXSW_REG_SFGC_TYPE_UNKNOWN_UNICAST]			= 1,
};

static const int mlxsw_sp_sfgc_bc_packet_types[MLXSW_REG_SFGC_TYPE_MAX] = {
	[MLXSW_REG_SFGC_TYPE_BROADCAST]				= 1,
	[MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_NON_IP]	= 1,
	[MLXSW_REG_SFGC_TYPE_IPV4_LINK_LOCAL]			= 1,
	[MLXSW_REG_SFGC_TYPE_IPV6_ALL_HOST]			= 1,
	[MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_IPV6]	= 1,
};

static const int mlxsw_sp_sfgc_mc_packet_types[MLXSW_REG_SFGC_TYPE_MAX] = {
	[MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_IPV4]	= 1,
};

static const int *mlxsw_sp_packet_type_sfgc_types[] = {
	[MLXSW_SP_FLOOD_TYPE_UC]	= mlxsw_sp_sfgc_uc_packet_types,
	[MLXSW_SP_FLOOD_TYPE_BC]	= mlxsw_sp_sfgc_bc_packet_types,
	[MLXSW_SP_FLOOD_TYPE_MC]	= mlxsw_sp_sfgc_mc_packet_types,
};

static const struct mlxsw_sp_flood_table *
mlxsw_sp_fid_flood_table_lookup(const struct mlxsw_sp_fid *fid,
				enum mlxsw_sp_flood_type packet_type)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	int i;

	for (i = 0; i < fid_family->nr_flood_tables; i++) {
		if (fid_family->flood_tables[i].packet_type != packet_type)
			continue;
		return &fid_family->flood_tables[i];
	}

	return NULL;
}

int mlxsw_sp_fid_flood_set(struct mlxsw_sp_fid *fid,
			   enum mlxsw_sp_flood_type packet_type, u8 local_port,
			   bool member)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_fid_ops *ops = fid_family->ops;
	const struct mlxsw_sp_flood_table *flood_table;
	char *sftr_pl;
	int err;

	if (WARN_ON(!fid_family->flood_tables || !ops->flood_index))
		return -EINVAL;

	flood_table = mlxsw_sp_fid_flood_table_lookup(fid, packet_type);
	if (!flood_table)
		return -ESRCH;

	sftr_pl = kmalloc(MLXSW_REG_SFTR_LEN, GFP_KERNEL);
	if (!sftr_pl)
		return -ENOMEM;

	mlxsw_reg_sftr_pack(sftr_pl, flood_table->table_index,
			    ops->flood_index(fid), flood_table->table_type, 1,
			    local_port, member);
	err = mlxsw_reg_write(fid_family->mlxsw_sp->core, MLXSW_REG(sftr),
			      sftr_pl);
	kfree(sftr_pl);
	return err;
}

int mlxsw_sp_fid_port_vid_map(struct mlxsw_sp_fid *fid,
			      struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	if (WARN_ON(!fid->fid_family->ops->port_vid_map))
		return -EINVAL;
	return fid->fid_family->ops->port_vid_map(fid, mlxsw_sp_port, vid);
}

void mlxsw_sp_fid_port_vid_unmap(struct mlxsw_sp_fid *fid,
				 struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	fid->fid_family->ops->port_vid_unmap(fid, mlxsw_sp_port, vid);
}

enum mlxsw_sp_rif_type mlxsw_sp_fid_rif_type(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_family->rif_type;
}

u16 mlxsw_sp_fid_index(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_index;
}

enum mlxsw_sp_fid_type mlxsw_sp_fid_type(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_family->type;
}

void mlxsw_sp_fid_rif_set(struct mlxsw_sp_fid *fid, struct mlxsw_sp_rif *rif)
{
	fid->rif = rif;
}

enum mlxsw_sp_rif_type
mlxsw_sp_fid_type_rif_type(const struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_fid_type type)
{
	struct mlxsw_sp_fid_core *fid_core = mlxsw_sp->fid_core;

	return fid_core->fid_family_arr[type]->rif_type;
}

static struct mlxsw_sp_fid_8021q *
mlxsw_sp_fid_8021q_fid(const struct mlxsw_sp_fid *fid)
{
	return container_of(fid, struct mlxsw_sp_fid_8021q, common);
}

u16 mlxsw_sp_fid_8021q_vid(const struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_8021q_fid(fid)->vid;
}

static void mlxsw_sp_fid_8021q_setup(struct mlxsw_sp_fid *fid, const void *arg)
{
	u16 vid = *(u16 *) arg;

	mlxsw_sp_fid_8021q_fid(fid)->vid = vid;
}

static enum mlxsw_reg_sfmr_op mlxsw_sp_sfmr_op(bool valid)
{
	return valid ? MLXSW_REG_SFMR_OP_CREATE_FID :
		       MLXSW_REG_SFMR_OP_DESTROY_FID;
}

static int mlxsw_sp_fid_op(struct mlxsw_sp *mlxsw_sp, u16 fid_index,
			   u16 fid_offset, bool valid)
{
	char sfmr_pl[MLXSW_REG_SFMR_LEN];

	mlxsw_reg_sfmr_pack(sfmr_pl, mlxsw_sp_sfmr_op(valid), fid_index,
			    fid_offset);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfmr), sfmr_pl);
}

static int mlxsw_sp_fid_vid_map(struct mlxsw_sp *mlxsw_sp, u16 fid_index,
				u16 vid, bool valid)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_VID_TO_FID;
	char svfa_pl[MLXSW_REG_SVFA_LEN];

	mlxsw_reg_svfa_pack(svfa_pl, 0, mt, valid, fid_index, vid);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static int __mlxsw_sp_fid_port_vid_map(struct mlxsw_sp *mlxsw_sp, u16 fid_index,
				       u8 local_port, u16 vid, bool valid)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;
	char svfa_pl[MLXSW_REG_SVFA_LEN];

	mlxsw_reg_svfa_pack(svfa_pl, local_port, mt, valid, fid_index, vid);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static int mlxsw_sp_fid_8021q_configure(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	struct mlxsw_sp_fid_8021q *fid_8021q;
	int err;

	err = mlxsw_sp_fid_op(mlxsw_sp, fid->fid_index, fid->fid_index, true);
	if (err)
		return err;

	fid_8021q = mlxsw_sp_fid_8021q_fid(fid);
	err = mlxsw_sp_fid_vid_map(mlxsw_sp, fid->fid_index, fid_8021q->vid,
				   true);
	if (err)
		goto err_fid_map;

	return 0;

err_fid_map:
	mlxsw_sp_fid_op(mlxsw_sp, fid->fid_index, 0, false);
	return err;
}

static void mlxsw_sp_fid_8021q_deconfigure(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	struct mlxsw_sp_fid_8021q *fid_8021q;

	fid_8021q = mlxsw_sp_fid_8021q_fid(fid);
	mlxsw_sp_fid_vid_map(mlxsw_sp, fid->fid_index, fid_8021q->vid, false);
	mlxsw_sp_fid_op(mlxsw_sp, fid->fid_index, 0, false);
}

static int mlxsw_sp_fid_8021q_index_alloc(struct mlxsw_sp_fid *fid,
					  const void *arg, u16 *p_fid_index)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	u16 vid = *(u16 *) arg;

	/* Use 1:1 mapping for simplicity although not a must */
	if (vid < fid_family->start_index || vid > fid_family->end_index)
		return -EINVAL;
	*p_fid_index = vid;

	return 0;
}

static bool
mlxsw_sp_fid_8021q_compare(const struct mlxsw_sp_fid *fid, const void *arg)
{
	u16 vid = *(u16 *) arg;

	return mlxsw_sp_fid_8021q_fid(fid)->vid == vid;
}

static u16 mlxsw_sp_fid_8021q_flood_index(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_index;
}

static int mlxsw_sp_fid_8021q_port_vid_map(struct mlxsw_sp_fid *fid,
					   struct mlxsw_sp_port *mlxsw_sp_port,
					   u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;

	/* In case there are no {Port, VID} => FID mappings on the port,
	 * we can use the global VID => FID mapping we created when the
	 * FID was configured.
	 */
	if (mlxsw_sp->fid_core->port_fid_mappings[local_port] == 0)
		return 0;
	return __mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index, local_port,
					   vid, true);
}

static void
mlxsw_sp_fid_8021q_port_vid_unmap(struct mlxsw_sp_fid *fid,
				  struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port] == 0)
		return;
	__mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index, local_port, vid,
				    false);
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_8021q_ops = {
	.setup			= mlxsw_sp_fid_8021q_setup,
	.configure		= mlxsw_sp_fid_8021q_configure,
	.deconfigure		= mlxsw_sp_fid_8021q_deconfigure,
	.index_alloc		= mlxsw_sp_fid_8021q_index_alloc,
	.compare		= mlxsw_sp_fid_8021q_compare,
	.flood_index		= mlxsw_sp_fid_8021q_flood_index,
	.port_vid_map		= mlxsw_sp_fid_8021q_port_vid_map,
	.port_vid_unmap		= mlxsw_sp_fid_8021q_port_vid_unmap,
};

static const struct mlxsw_sp_flood_table mlxsw_sp_fid_8021q_flood_tables[] = {
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_UC,
		.bridge_type	= MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET,
		.table_index	= 0,
	},
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_MC,
		.bridge_type	= MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET,
		.table_index	= 1,
	},
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_BC,
		.bridge_type	= MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET,
		.table_index	= 2,
	},
};

/* Range and flood configuration must match mlxsw_config_profile */
static const struct mlxsw_sp_fid_family mlxsw_sp_fid_8021q_family = {
	.type			= MLXSW_SP_FID_TYPE_8021Q,
	.fid_size		= sizeof(struct mlxsw_sp_fid_8021q),
	.start_index		= 1,
	.end_index		= VLAN_VID_MASK,
	.flood_tables		= mlxsw_sp_fid_8021q_flood_tables,
	.nr_flood_tables	= ARRAY_SIZE(mlxsw_sp_fid_8021q_flood_tables),
	.rif_type		= MLXSW_SP_RIF_TYPE_VLAN,
	.ops			= &mlxsw_sp_fid_8021q_ops,
};

static struct mlxsw_sp_fid_8021d *
mlxsw_sp_fid_8021d_fid(const struct mlxsw_sp_fid *fid)
{
	return container_of(fid, struct mlxsw_sp_fid_8021d, common);
}

static void mlxsw_sp_fid_8021d_setup(struct mlxsw_sp_fid *fid, const void *arg)
{
	int br_ifindex = *(int *) arg;

	mlxsw_sp_fid_8021d_fid(fid)->br_ifindex = br_ifindex;
}

static int mlxsw_sp_fid_8021d_configure(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;

	return mlxsw_sp_fid_op(fid_family->mlxsw_sp, fid->fid_index, 0, true);
}

static void mlxsw_sp_fid_8021d_deconfigure(struct mlxsw_sp_fid *fid)
{
	mlxsw_sp_fid_op(fid->fid_family->mlxsw_sp, fid->fid_index, 0, false);
}

static int mlxsw_sp_fid_8021d_index_alloc(struct mlxsw_sp_fid *fid,
					  const void *arg, u16 *p_fid_index)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	u16 nr_fids, fid_index;

	nr_fids = fid_family->end_index - fid_family->start_index + 1;
	fid_index = find_first_zero_bit(fid_family->fids_bitmap, nr_fids);
	if (fid_index == nr_fids)
		return -ENOBUFS;
	*p_fid_index = fid_family->start_index + fid_index;

	return 0;
}

static bool
mlxsw_sp_fid_8021d_compare(const struct mlxsw_sp_fid *fid, const void *arg)
{
	int br_ifindex = *(int *) arg;

	return mlxsw_sp_fid_8021d_fid(fid)->br_ifindex == br_ifindex;
}

static u16 mlxsw_sp_fid_8021d_flood_index(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_index - fid->fid_family->start_index;
}

static int mlxsw_sp_port_vp_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	int err;

	list_for_each_entry(mlxsw_sp_port_vlan, &mlxsw_sp_port->vlans_list,
			    list) {
		struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
		u16 vid = mlxsw_sp_port_vlan->vid;

		if (!fid)
			continue;

		err = __mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index,
						  mlxsw_sp_port->local_port,
						  vid, true);
		if (err)
			goto err_fid_port_vid_map;
	}

	err = mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, true);
	if (err)
		goto err_port_vp_mode_set;

	return 0;

err_port_vp_mode_set:
err_fid_port_vid_map:
	list_for_each_entry_continue_reverse(mlxsw_sp_port_vlan,
					     &mlxsw_sp_port->vlans_list, list) {
		struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
		u16 vid = mlxsw_sp_port_vlan->vid;

		if (!fid)
			continue;

		__mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index,
					    mlxsw_sp_port->local_port, vid,
					    false);
	}
	return err;
}

static void mlxsw_sp_port_vlan_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);

	list_for_each_entry_reverse(mlxsw_sp_port_vlan,
				    &mlxsw_sp_port->vlans_list, list) {
		struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
		u16 vid = mlxsw_sp_port_vlan->vid;

		if (!fid)
			continue;

		__mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index,
					    mlxsw_sp_port->local_port, vid,
					    false);
	}
}

static int mlxsw_sp_fid_8021d_port_vid_map(struct mlxsw_sp_fid *fid,
					   struct mlxsw_sp_port *mlxsw_sp_port,
					   u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	int err;

	err = __mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index,
					  mlxsw_sp_port->local_port, vid, true);
	if (err)
		return err;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port]++ == 0) {
		err = mlxsw_sp_port_vp_mode_trans(mlxsw_sp_port);
		if (err)
			goto err_port_vp_mode_trans;
	}

	return 0;

err_port_vp_mode_trans:
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
	__mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index,
				    mlxsw_sp_port->local_port, vid, false);
	return err;
}

static void
mlxsw_sp_fid_8021d_port_vid_unmap(struct mlxsw_sp_fid *fid,
				  struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port] == 1)
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
	__mlxsw_sp_fid_port_vid_map(mlxsw_sp, fid->fid_index,
				    mlxsw_sp_port->local_port, vid, false);
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_8021d_ops = {
	.setup			= mlxsw_sp_fid_8021d_setup,
	.configure		= mlxsw_sp_fid_8021d_configure,
	.deconfigure		= mlxsw_sp_fid_8021d_deconfigure,
	.index_alloc		= mlxsw_sp_fid_8021d_index_alloc,
	.compare		= mlxsw_sp_fid_8021d_compare,
	.flood_index		= mlxsw_sp_fid_8021d_flood_index,
	.port_vid_map		= mlxsw_sp_fid_8021d_port_vid_map,
	.port_vid_unmap		= mlxsw_sp_fid_8021d_port_vid_unmap,
};

static const struct mlxsw_sp_flood_table mlxsw_sp_fid_8021d_flood_tables[] = {
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_UC,
		.bridge_type	= MLXSW_REG_SFGC_BRIDGE_TYPE_VFID,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID,
		.table_index	= 0,
	},
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_MC,
		.bridge_type	= MLXSW_REG_SFGC_BRIDGE_TYPE_VFID,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID,
		.table_index	= 1,
	},
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_BC,
		.bridge_type	= MLXSW_REG_SFGC_BRIDGE_TYPE_VFID,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID,
		.table_index	= 2,
	},
};

/* Range and flood configuration must match mlxsw_config_profile */
static const struct mlxsw_sp_fid_family mlxsw_sp_fid_8021d_family = {
	.type			= MLXSW_SP_FID_TYPE_8021D,
	.fid_size		= sizeof(struct mlxsw_sp_fid_8021d),
	.start_index		= VLAN_N_VID,
	.end_index		= VLAN_N_VID + MLXSW_SP_FID_8021D_MAX - 1,
	.flood_tables		= mlxsw_sp_fid_8021d_flood_tables,
	.nr_flood_tables	= ARRAY_SIZE(mlxsw_sp_fid_8021d_flood_tables),
	.rif_type		= MLXSW_SP_RIF_TYPE_FID,
	.ops			= &mlxsw_sp_fid_8021d_ops,
};

static int mlxsw_sp_fid_rfid_configure(struct mlxsw_sp_fid *fid)
{
	/* rFIDs are allocated by the device during init */
	return 0;
}

static void mlxsw_sp_fid_rfid_deconfigure(struct mlxsw_sp_fid *fid)
{
}

static int mlxsw_sp_fid_rfid_index_alloc(struct mlxsw_sp_fid *fid,
					 const void *arg, u16 *p_fid_index)
{
	u16 rif_index = *(u16 *) arg;

	*p_fid_index = fid->fid_family->start_index + rif_index;

	return 0;
}

static bool mlxsw_sp_fid_rfid_compare(const struct mlxsw_sp_fid *fid,
				      const void *arg)
{
	u16 rif_index = *(u16 *) arg;

	return fid->fid_index == rif_index + fid->fid_family->start_index;
}

static int mlxsw_sp_fid_rfid_port_vid_map(struct mlxsw_sp_fid *fid,
					  struct mlxsw_sp_port *mlxsw_sp_port,
					  u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	int err;

	/* We only need to transition the port to virtual mode since
	 * {Port, VID} => FID is done by the firmware upon RIF creation.
	 */
	if (mlxsw_sp->fid_core->port_fid_mappings[local_port]++ == 0) {
		err = mlxsw_sp_port_vp_mode_trans(mlxsw_sp_port);
		if (err)
			goto err_port_vp_mode_trans;
	}

	return 0;

err_port_vp_mode_trans:
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
	return err;
}

static void
mlxsw_sp_fid_rfid_port_vid_unmap(struct mlxsw_sp_fid *fid,
				 struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port] == 1)
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_rfid_ops = {
	.configure		= mlxsw_sp_fid_rfid_configure,
	.deconfigure		= mlxsw_sp_fid_rfid_deconfigure,
	.index_alloc		= mlxsw_sp_fid_rfid_index_alloc,
	.compare		= mlxsw_sp_fid_rfid_compare,
	.port_vid_map		= mlxsw_sp_fid_rfid_port_vid_map,
	.port_vid_unmap		= mlxsw_sp_fid_rfid_port_vid_unmap,
};

#define MLXSW_SP_RFID_BASE	(15 * 1024)
#define MLXSW_SP_RFID_MAX	1024

static const struct mlxsw_sp_fid_family mlxsw_sp_fid_rfid_family = {
	.type			= MLXSW_SP_FID_TYPE_RFID,
	.fid_size		= sizeof(struct mlxsw_sp_fid),
	.start_index		= MLXSW_SP_RFID_BASE,
	.end_index		= MLXSW_SP_RFID_BASE + MLXSW_SP_RFID_MAX - 1,
	.rif_type		= MLXSW_SP_RIF_TYPE_SUBPORT,
	.ops			= &mlxsw_sp_fid_rfid_ops,
};

static int mlxsw_sp_fid_dummy_configure(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;

	return mlxsw_sp_fid_op(mlxsw_sp, fid->fid_index, 0, true);
}

static void mlxsw_sp_fid_dummy_deconfigure(struct mlxsw_sp_fid *fid)
{
	mlxsw_sp_fid_op(fid->fid_family->mlxsw_sp, fid->fid_index, 0, false);
}

static int mlxsw_sp_fid_dummy_index_alloc(struct mlxsw_sp_fid *fid,
					  const void *arg, u16 *p_fid_index)
{
	*p_fid_index = fid->fid_family->start_index;

	return 0;
}

static bool mlxsw_sp_fid_dummy_compare(const struct mlxsw_sp_fid *fid,
				       const void *arg)
{
	return true;
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_dummy_ops = {
	.configure		= mlxsw_sp_fid_dummy_configure,
	.deconfigure		= mlxsw_sp_fid_dummy_deconfigure,
	.index_alloc		= mlxsw_sp_fid_dummy_index_alloc,
	.compare		= mlxsw_sp_fid_dummy_compare,
};

static const struct mlxsw_sp_fid_family mlxsw_sp_fid_dummy_family = {
	.type			= MLXSW_SP_FID_TYPE_DUMMY,
	.fid_size		= sizeof(struct mlxsw_sp_fid),
	.start_index		= MLXSW_SP_RFID_BASE - 1,
	.end_index		= MLXSW_SP_RFID_BASE - 1,
	.ops			= &mlxsw_sp_fid_dummy_ops,
};

static const struct mlxsw_sp_fid_family *mlxsw_sp_fid_family_arr[] = {
	[MLXSW_SP_FID_TYPE_8021Q]	= &mlxsw_sp_fid_8021q_family,
	[MLXSW_SP_FID_TYPE_8021D]	= &mlxsw_sp_fid_8021d_family,
	[MLXSW_SP_FID_TYPE_RFID]	= &mlxsw_sp_fid_rfid_family,
	[MLXSW_SP_FID_TYPE_DUMMY]	= &mlxsw_sp_fid_dummy_family,
};

static struct mlxsw_sp_fid *mlxsw_sp_fid_get(struct mlxsw_sp *mlxsw_sp,
					     enum mlxsw_sp_fid_type type,
					     const void *arg)
{
	struct mlxsw_sp_fid_family *fid_family;
	struct mlxsw_sp_fid *fid;
	u16 fid_index;
	int err;

	fid_family = mlxsw_sp->fid_core->fid_family_arr[type];
	list_for_each_entry(fid, &fid_family->fids_list, list) {
		if (!fid->fid_family->ops->compare(fid, arg))
			continue;
		fid->ref_count++;
		return fid;
	}

	fid = kzalloc(fid_family->fid_size, GFP_KERNEL);
	if (!fid)
		return ERR_PTR(-ENOMEM);
	fid->fid_family = fid_family;

	err = fid->fid_family->ops->index_alloc(fid, arg, &fid_index);
	if (err)
		goto err_index_alloc;
	fid->fid_index = fid_index;
	__set_bit(fid_index - fid_family->start_index, fid_family->fids_bitmap);

	if (fid->fid_family->ops->setup)
		fid->fid_family->ops->setup(fid, arg);

	err = fid->fid_family->ops->configure(fid);
	if (err)
		goto err_configure;

	list_add(&fid->list, &fid_family->fids_list);
	fid->ref_count++;
	return fid;

err_configure:
	__clear_bit(fid_index - fid_family->start_index,
		    fid_family->fids_bitmap);
err_index_alloc:
	kfree(fid);
	return ERR_PTR(err);
}

void mlxsw_sp_fid_put(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;

	if (--fid->ref_count == 1 && fid->rif) {
		/* Destroy the associated RIF and let it drop the last
		 * reference on the FID.
		 */
		return mlxsw_sp_rif_destroy(fid->rif);
	} else if (fid->ref_count == 0) {
		list_del(&fid->list);
		fid->fid_family->ops->deconfigure(fid);
		__clear_bit(fid->fid_index - fid_family->start_index,
			    fid_family->fids_bitmap);
		kfree(fid);
	}
}

struct mlxsw_sp_fid *mlxsw_sp_fid_8021q_get(struct mlxsw_sp *mlxsw_sp, u16 vid)
{
	return mlxsw_sp_fid_get(mlxsw_sp, MLXSW_SP_FID_TYPE_8021Q, &vid);
}

struct mlxsw_sp_fid *mlxsw_sp_fid_8021d_get(struct mlxsw_sp *mlxsw_sp,
					    int br_ifindex)
{
	return mlxsw_sp_fid_get(mlxsw_sp, MLXSW_SP_FID_TYPE_8021D, &br_ifindex);
}

struct mlxsw_sp_fid *mlxsw_sp_fid_rfid_get(struct mlxsw_sp *mlxsw_sp,
					   u16 rif_index)
{
	return mlxsw_sp_fid_get(mlxsw_sp, MLXSW_SP_FID_TYPE_RFID, &rif_index);
}

struct mlxsw_sp_fid *mlxsw_sp_fid_dummy_get(struct mlxsw_sp *mlxsw_sp)
{
	return mlxsw_sp_fid_get(mlxsw_sp, MLXSW_SP_FID_TYPE_DUMMY, NULL);
}

static int
mlxsw_sp_fid_flood_table_init(struct mlxsw_sp_fid_family *fid_family,
			      const struct mlxsw_sp_flood_table *flood_table)
{
	enum mlxsw_sp_flood_type packet_type = flood_table->packet_type;
	const int *sfgc_packet_types;
	int i;

	sfgc_packet_types = mlxsw_sp_packet_type_sfgc_types[packet_type];
	for (i = 0; i < MLXSW_REG_SFGC_TYPE_MAX; i++) {
		struct mlxsw_sp *mlxsw_sp = fid_family->mlxsw_sp;
		char sfgc_pl[MLXSW_REG_SFGC_LEN];
		int err;

		if (!sfgc_packet_types[i])
			continue;
		mlxsw_reg_sfgc_pack(sfgc_pl, i, flood_table->bridge_type,
				    flood_table->table_type,
				    flood_table->table_index);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfgc), sfgc_pl);
		if (err)
			return err;
	}

	return 0;
}

static int
mlxsw_sp_fid_flood_tables_init(struct mlxsw_sp_fid_family *fid_family)
{
	int i;

	for (i = 0; i < fid_family->nr_flood_tables; i++) {
		const struct mlxsw_sp_flood_table *flood_table;
		int err;

		flood_table = &fid_family->flood_tables[i];
		err = mlxsw_sp_fid_flood_table_init(fid_family, flood_table);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_fid_family_register(struct mlxsw_sp *mlxsw_sp,
					const struct mlxsw_sp_fid_family *tmpl)
{
	u16 nr_fids = tmpl->end_index - tmpl->start_index + 1;
	struct mlxsw_sp_fid_family *fid_family;
	int err;

	fid_family = kmemdup(tmpl, sizeof(*fid_family), GFP_KERNEL);
	if (!fid_family)
		return -ENOMEM;

	fid_family->mlxsw_sp = mlxsw_sp;
	INIT_LIST_HEAD(&fid_family->fids_list);
	fid_family->fids_bitmap = kcalloc(BITS_TO_LONGS(nr_fids),
					  sizeof(unsigned long), GFP_KERNEL);
	if (!fid_family->fids_bitmap) {
		err = -ENOMEM;
		goto err_alloc_fids_bitmap;
	}

	if (fid_family->flood_tables) {
		err = mlxsw_sp_fid_flood_tables_init(fid_family);
		if (err)
			goto err_fid_flood_tables_init;
	}

	mlxsw_sp->fid_core->fid_family_arr[tmpl->type] = fid_family;

	return 0;

err_fid_flood_tables_init:
	kfree(fid_family->fids_bitmap);
err_alloc_fids_bitmap:
	kfree(fid_family);
	return err;
}

static void
mlxsw_sp_fid_family_unregister(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fid_family *fid_family)
{
	mlxsw_sp->fid_core->fid_family_arr[fid_family->type] = NULL;
	kfree(fid_family->fids_bitmap);
	WARN_ON_ONCE(!list_empty(&fid_family->fids_list));
	kfree(fid_family);
}

int mlxsw_sp_port_fids_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	/* Track number of FIDs configured on the port with mapping type
	 * PORT_VID_TO_FID, so that we know when to transition the port
	 * back to non-virtual (VLAN) mode.
	 */
	mlxsw_sp->fid_core->port_fid_mappings[mlxsw_sp_port->local_port] = 0;

	return mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);
}

void mlxsw_sp_port_fids_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	mlxsw_sp->fid_core->port_fid_mappings[mlxsw_sp_port->local_port] = 0;
}

int mlxsw_sp_fids_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_sp->core);
	struct mlxsw_sp_fid_core *fid_core;
	int err, i;

	fid_core = kzalloc(sizeof(*mlxsw_sp->fid_core), GFP_KERNEL);
	if (!fid_core)
		return -ENOMEM;
	mlxsw_sp->fid_core = fid_core;

	fid_core->port_fid_mappings = kcalloc(max_ports, sizeof(unsigned int),
					      GFP_KERNEL);
	if (!fid_core->port_fid_mappings) {
		err = -ENOMEM;
		goto err_alloc_port_fid_mappings;
	}

	for (i = 0; i < MLXSW_SP_FID_TYPE_MAX; i++) {
		err = mlxsw_sp_fid_family_register(mlxsw_sp,
						   mlxsw_sp_fid_family_arr[i]);

		if (err)
			goto err_fid_ops_register;
	}

	return 0;

err_fid_ops_register:
	for (i--; i >= 0; i--) {
		struct mlxsw_sp_fid_family *fid_family;

		fid_family = fid_core->fid_family_arr[i];
		mlxsw_sp_fid_family_unregister(mlxsw_sp, fid_family);
	}
	kfree(fid_core->port_fid_mappings);
err_alloc_port_fid_mappings:
	kfree(fid_core);
	return err;
}

void mlxsw_sp_fids_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_fid_core *fid_core = mlxsw_sp->fid_core;
	int i;

	for (i = 0; i < MLXSW_SP_FID_TYPE_MAX; i++)
		mlxsw_sp_fid_family_unregister(mlxsw_sp,
					       fid_core->fid_family_arr[i]);
	kfree(fid_core->port_fid_mappings);
	kfree(fid_core);
}
