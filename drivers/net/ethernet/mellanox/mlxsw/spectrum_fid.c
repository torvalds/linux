// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <linux/rtnetlink.h>
#include <linux/refcount.h>

#include "spectrum.h"
#include "reg.h"

struct mlxsw_sp_fid_family;

struct mlxsw_sp_fid_core {
	struct rhashtable fid_ht;
	struct rhashtable vni_ht;
	struct mlxsw_sp_fid_family *fid_family_arr[MLXSW_SP_FID_TYPE_MAX];
	unsigned int *port_fid_mappings;
};

struct mlxsw_sp_fid_port_vid {
	struct list_head list;
	u16 local_port;
	u16 vid;
};

struct mlxsw_sp_fid {
	struct list_head list;
	struct mlxsw_sp_rif *rif;
	refcount_t ref_count;
	u16 fid_index;
	u16 fid_offset;
	struct mlxsw_sp_fid_family *fid_family;
	struct rhash_head ht_node;

	struct rhash_head vni_ht_node;
	enum mlxsw_sp_nve_type nve_type;
	__be32 vni;
	u32 nve_flood_index;
	int nve_ifindex;
	u8 vni_valid:1,
	   nve_flood_index_valid:1;
	struct list_head port_vid_list; /* Ordered by local port. */
};

struct mlxsw_sp_fid_8021q {
	struct mlxsw_sp_fid common;
	u16 vid;
};

struct mlxsw_sp_fid_8021d {
	struct mlxsw_sp_fid common;
	int br_ifindex;
};

static const struct rhashtable_params mlxsw_sp_fid_ht_params = {
	.key_len = sizeof_field(struct mlxsw_sp_fid, fid_index),
	.key_offset = offsetof(struct mlxsw_sp_fid, fid_index),
	.head_offset = offsetof(struct mlxsw_sp_fid, ht_node),
};

static const struct rhashtable_params mlxsw_sp_fid_vni_ht_params = {
	.key_len = sizeof_field(struct mlxsw_sp_fid, vni),
	.key_offset = offsetof(struct mlxsw_sp_fid, vni),
	.head_offset = offsetof(struct mlxsw_sp_fid, vni_ht_node),
};

struct mlxsw_sp_flood_table {
	enum mlxsw_sp_flood_type packet_type;
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
	int (*port_vid_map)(struct mlxsw_sp_fid *fid,
			    struct mlxsw_sp_port *port, u16 vid);
	void (*port_vid_unmap)(struct mlxsw_sp_fid *fid,
			       struct mlxsw_sp_port *port, u16 vid);
	int (*vni_set)(struct mlxsw_sp_fid *fid);
	void (*vni_clear)(struct mlxsw_sp_fid *fid);
	int (*nve_flood_index_set)(struct mlxsw_sp_fid *fid);
	void (*nve_flood_index_clear)(struct mlxsw_sp_fid *fid);
	void (*fdb_clear_offload)(const struct mlxsw_sp_fid *fid,
				  const struct net_device *nve_dev);
	int (*vid_to_fid_rif_update)(const struct mlxsw_sp_fid *fid,
				     const struct mlxsw_sp_rif *rif);
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
	bool flood_rsp;
	enum mlxsw_reg_bridge_type bridge_type;
	u16 pgt_base;
	bool smpe_index_valid;
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

bool mlxsw_sp_fid_is_dummy(struct mlxsw_sp *mlxsw_sp, u16 fid_index)
{
	enum mlxsw_sp_fid_type fid_type = MLXSW_SP_FID_TYPE_DUMMY;
	struct mlxsw_sp_fid_family *fid_family;

	fid_family = mlxsw_sp->fid_core->fid_family_arr[fid_type];

	return fid_family->start_index == fid_index;
}

struct mlxsw_sp_fid *mlxsw_sp_fid_lookup_by_index(struct mlxsw_sp *mlxsw_sp,
						  u16 fid_index)
{
	struct mlxsw_sp_fid *fid;

	fid = rhashtable_lookup_fast(&mlxsw_sp->fid_core->fid_ht, &fid_index,
				     mlxsw_sp_fid_ht_params);
	if (fid)
		refcount_inc(&fid->ref_count);

	return fid;
}

int mlxsw_sp_fid_nve_ifindex(const struct mlxsw_sp_fid *fid, int *nve_ifindex)
{
	if (!fid->vni_valid)
		return -EINVAL;

	*nve_ifindex = fid->nve_ifindex;

	return 0;
}

int mlxsw_sp_fid_nve_type(const struct mlxsw_sp_fid *fid,
			  enum mlxsw_sp_nve_type *p_type)
{
	if (!fid->vni_valid)
		return -EINVAL;

	*p_type = fid->nve_type;

	return 0;
}

struct mlxsw_sp_fid *mlxsw_sp_fid_lookup_by_vni(struct mlxsw_sp *mlxsw_sp,
						__be32 vni)
{
	struct mlxsw_sp_fid *fid;

	fid = rhashtable_lookup_fast(&mlxsw_sp->fid_core->vni_ht, &vni,
				     mlxsw_sp_fid_vni_ht_params);
	if (fid)
		refcount_inc(&fid->ref_count);

	return fid;
}

int mlxsw_sp_fid_vni(const struct mlxsw_sp_fid *fid, __be32 *vni)
{
	if (!fid->vni_valid)
		return -EINVAL;

	*vni = fid->vni;

	return 0;
}

int mlxsw_sp_fid_nve_flood_index_set(struct mlxsw_sp_fid *fid,
				     u32 nve_flood_index)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_fid_ops *ops = fid_family->ops;
	int err;

	if (WARN_ON(fid->nve_flood_index_valid))
		return -EINVAL;

	fid->nve_flood_index = nve_flood_index;
	fid->nve_flood_index_valid = true;
	err = ops->nve_flood_index_set(fid);
	if (err)
		goto err_nve_flood_index_set;

	return 0;

err_nve_flood_index_set:
	fid->nve_flood_index_valid = false;
	return err;
}

void mlxsw_sp_fid_nve_flood_index_clear(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_fid_ops *ops = fid_family->ops;

	if (WARN_ON(!fid->nve_flood_index_valid))
		return;

	fid->nve_flood_index_valid = false;
	ops->nve_flood_index_clear(fid);
}

bool mlxsw_sp_fid_nve_flood_index_is_set(const struct mlxsw_sp_fid *fid)
{
	return fid->nve_flood_index_valid;
}

int mlxsw_sp_fid_vni_set(struct mlxsw_sp_fid *fid, enum mlxsw_sp_nve_type type,
			 __be32 vni, int nve_ifindex)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_fid_ops *ops = fid_family->ops;
	struct mlxsw_sp *mlxsw_sp = fid_family->mlxsw_sp;
	int err;

	if (WARN_ON(fid->vni_valid))
		return -EINVAL;

	fid->nve_type = type;
	fid->nve_ifindex = nve_ifindex;
	fid->vni = vni;
	err = rhashtable_lookup_insert_fast(&mlxsw_sp->fid_core->vni_ht,
					    &fid->vni_ht_node,
					    mlxsw_sp_fid_vni_ht_params);
	if (err)
		return err;

	fid->vni_valid = true;
	err = ops->vni_set(fid);
	if (err)
		goto err_vni_set;

	return 0;

err_vni_set:
	fid->vni_valid = false;
	rhashtable_remove_fast(&mlxsw_sp->fid_core->vni_ht, &fid->vni_ht_node,
			       mlxsw_sp_fid_vni_ht_params);
	return err;
}

void mlxsw_sp_fid_vni_clear(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_fid_ops *ops = fid_family->ops;
	struct mlxsw_sp *mlxsw_sp = fid_family->mlxsw_sp;

	if (WARN_ON(!fid->vni_valid))
		return;

	fid->vni_valid = false;
	ops->vni_clear(fid);
	rhashtable_remove_fast(&mlxsw_sp->fid_core->vni_ht, &fid->vni_ht_node,
			       mlxsw_sp_fid_vni_ht_params);
}

bool mlxsw_sp_fid_vni_is_set(const struct mlxsw_sp_fid *fid)
{
	return fid->vni_valid;
}

void mlxsw_sp_fid_fdb_clear_offload(const struct mlxsw_sp_fid *fid,
				    const struct net_device *nve_dev)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_fid_ops *ops = fid_family->ops;

	if (ops->fdb_clear_offload)
		ops->fdb_clear_offload(fid, nve_dev);
}

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

static u16
mlxsw_sp_fid_family_num_fids(const struct mlxsw_sp_fid_family *fid_family)
{
	return fid_family->end_index - fid_family->start_index + 1;
}

static u16
mlxsw_sp_fid_flood_table_mid(const struct mlxsw_sp_fid_family *fid_family,
			     const struct mlxsw_sp_flood_table *flood_table,
			     u16 fid_offset)
{
	u16 num_fids;

	num_fids = mlxsw_sp_fid_family_num_fids(fid_family);
	return fid_family->pgt_base + num_fids * flood_table->table_index +
	       fid_offset;
}

int mlxsw_sp_fid_flood_set(struct mlxsw_sp_fid *fid,
			   enum mlxsw_sp_flood_type packet_type, u16 local_port,
			   bool member)
{
	struct mlxsw_sp_fid_family *fid_family = fid->fid_family;
	const struct mlxsw_sp_flood_table *flood_table;
	u16 mid_index;

	if (WARN_ON(!fid_family->flood_tables))
		return -EINVAL;

	flood_table = mlxsw_sp_fid_flood_table_lookup(fid, packet_type);
	if (!flood_table)
		return -ESRCH;

	mid_index = mlxsw_sp_fid_flood_table_mid(fid_family, flood_table,
						 fid->fid_offset);
	return mlxsw_sp_pgt_entry_port_set(fid_family->mlxsw_sp, mid_index,
					   fid->fid_index, local_port, member);
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

u16 mlxsw_sp_fid_index(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_index;
}

enum mlxsw_sp_fid_type mlxsw_sp_fid_type(const struct mlxsw_sp_fid *fid)
{
	return fid->fid_family->type;
}

struct mlxsw_sp_rif *mlxsw_sp_fid_rif(const struct mlxsw_sp_fid *fid)
{
	return fid->rif;
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
	fid->fid_offset = fid->fid_index - fid->fid_family->start_index;
}

static enum mlxsw_reg_sfmr_op mlxsw_sp_sfmr_op(bool valid)
{
	return valid ? MLXSW_REG_SFMR_OP_CREATE_FID :
		       MLXSW_REG_SFMR_OP_DESTROY_FID;
}

static int mlxsw_sp_fid_op(const struct mlxsw_sp_fid *fid, bool valid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char sfmr_pl[MLXSW_REG_SFMR_LEN];
	u16 smpe;

	smpe = fid->fid_family->smpe_index_valid ? fid->fid_index : 0;

	mlxsw_reg_sfmr_pack(sfmr_pl, mlxsw_sp_sfmr_op(valid), fid->fid_index,
			    fid->fid_offset, fid->fid_family->flood_rsp,
			    fid->fid_family->bridge_type,
			    fid->fid_family->smpe_index_valid, smpe);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfmr), sfmr_pl);
}

static int mlxsw_sp_fid_edit_op(const struct mlxsw_sp_fid *fid,
				const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char sfmr_pl[MLXSW_REG_SFMR_LEN];
	u16 smpe;

	smpe = fid->fid_family->smpe_index_valid ? fid->fid_index : 0;

	mlxsw_reg_sfmr_pack(sfmr_pl, MLXSW_REG_SFMR_OP_CREATE_FID,
			    fid->fid_index, fid->fid_offset,
			    fid->fid_family->flood_rsp,
			    fid->fid_family->bridge_type,
			    fid->fid_family->smpe_index_valid, smpe);
	mlxsw_reg_sfmr_vv_set(sfmr_pl, fid->vni_valid);
	mlxsw_reg_sfmr_vni_set(sfmr_pl, be32_to_cpu(fid->vni));
	mlxsw_reg_sfmr_vtfp_set(sfmr_pl, fid->nve_flood_index_valid);
	mlxsw_reg_sfmr_nve_tunnel_flood_ptr_set(sfmr_pl, fid->nve_flood_index);

	if (rif) {
		mlxsw_reg_sfmr_irif_v_set(sfmr_pl, true);
		mlxsw_reg_sfmr_irif_set(sfmr_pl, mlxsw_sp_rif_index(rif));
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfmr), sfmr_pl);
}

static int mlxsw_sp_fid_vni_to_fid_map(const struct mlxsw_sp_fid *fid,
				       const struct mlxsw_sp_rif *rif,
				       bool valid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char svfa_pl[MLXSW_REG_SVFA_LEN];
	bool irif_valid;
	u16 irif_index;

	irif_valid = !!rif;
	irif_index = rif ? mlxsw_sp_rif_index(rif) : 0;

	mlxsw_reg_svfa_vni_pack(svfa_pl, valid, fid->fid_index,
				be32_to_cpu(fid->vni), irif_valid, irif_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static int mlxsw_sp_fid_to_fid_rif_update(const struct mlxsw_sp_fid *fid,
					  const struct mlxsw_sp_rif *rif)
{
	return mlxsw_sp_fid_edit_op(fid, rif);
}

static int mlxsw_sp_fid_vni_to_fid_rif_update(const struct mlxsw_sp_fid *fid,
					      const struct mlxsw_sp_rif *rif)
{
	if (!fid->vni_valid)
		return 0;

	return mlxsw_sp_fid_vni_to_fid_map(fid, rif, fid->vni_valid);
}

static int
mlxsw_sp_fid_vid_to_fid_map(const struct mlxsw_sp_fid *fid, u16 vid, bool valid,
			    const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char svfa_pl[MLXSW_REG_SVFA_LEN];
	bool irif_valid;
	u16 irif_index;

	irif_valid = !!rif;
	irif_index = rif ? mlxsw_sp_rif_index(rif) : 0;

	mlxsw_reg_svfa_vid_pack(svfa_pl, valid, fid->fid_index, vid, irif_valid,
				irif_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static int
mlxsw_sp_fid_8021q_vid_to_fid_rif_update(const struct mlxsw_sp_fid *fid,
					 const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_fid_8021q *fid_8021q = mlxsw_sp_fid_8021q_fid(fid);

	/* Update the global VID => FID mapping we created when the FID was
	 * configured.
	 */
	return mlxsw_sp_fid_vid_to_fid_map(fid, fid_8021q->vid, true, rif);
}

static int
mlxsw_sp_fid_port_vid_to_fid_rif_update_one(const struct mlxsw_sp_fid *fid,
					    struct mlxsw_sp_fid_port_vid *pv,
					    bool irif_valid, u16 irif_index)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char svfa_pl[MLXSW_REG_SVFA_LEN];

	mlxsw_reg_svfa_port_vid_pack(svfa_pl, pv->local_port, true,
				     fid->fid_index, pv->vid, irif_valid,
				     irif_index);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static int mlxsw_sp_fid_vid_to_fid_rif_set(const struct mlxsw_sp_fid *fid,
					   const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	struct mlxsw_sp_fid_port_vid *pv;
	u16 irif_index;
	int err;

	err = fid->fid_family->ops->vid_to_fid_rif_update(fid, rif);
	if (err)
		return err;

	irif_index = mlxsw_sp_rif_index(rif);

	list_for_each_entry(pv, &fid->port_vid_list, list) {
		/* If port is not in virtual mode, then it does not have any
		 * {Port, VID}->FID mappings that need to be updated with the
		 * ingress RIF.
		 */
		if (!mlxsw_sp->fid_core->port_fid_mappings[pv->local_port])
			continue;

		err = mlxsw_sp_fid_port_vid_to_fid_rif_update_one(fid, pv,
								  true,
								  irif_index);
		if (err)
			goto err_port_vid_to_fid_rif_update_one;
	}

	return 0;

err_port_vid_to_fid_rif_update_one:
	list_for_each_entry_continue_reverse(pv, &fid->port_vid_list, list) {
		if (!mlxsw_sp->fid_core->port_fid_mappings[pv->local_port])
			continue;

		mlxsw_sp_fid_port_vid_to_fid_rif_update_one(fid, pv, false, 0);
	}

	fid->fid_family->ops->vid_to_fid_rif_update(fid, NULL);
	return err;
}

static void mlxsw_sp_fid_vid_to_fid_rif_unset(const struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	struct mlxsw_sp_fid_port_vid *pv;

	list_for_each_entry(pv, &fid->port_vid_list, list) {
		/* If port is not in virtual mode, then it does not have any
		 * {Port, VID}->FID mappings that need to be updated.
		 */
		if (!mlxsw_sp->fid_core->port_fid_mappings[pv->local_port])
			continue;

		mlxsw_sp_fid_port_vid_to_fid_rif_update_one(fid, pv, false, 0);
	}

	fid->fid_family->ops->vid_to_fid_rif_update(fid, NULL);
}

static int mlxsw_sp_fid_reiv_handle(struct mlxsw_sp_fid *fid, u16 rif_index,
				    bool valid, u8 port_page)
{
	u16 local_port_end = (port_page + 1) * MLXSW_REG_REIV_REC_MAX_COUNT - 1;
	u16 local_port_start = port_page * MLXSW_REG_REIV_REC_MAX_COUNT;
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	struct mlxsw_sp_fid_port_vid *port_vid;
	u8 rec_num, entries_num = 0;
	char *reiv_pl;
	int err;

	reiv_pl = kmalloc(MLXSW_REG_REIV_LEN, GFP_KERNEL);
	if (!reiv_pl)
		return -ENOMEM;

	mlxsw_reg_reiv_pack(reiv_pl, port_page, rif_index);

	list_for_each_entry(port_vid, &fid->port_vid_list, list) {
		/* port_vid_list is sorted by local_port. */
		if (port_vid->local_port < local_port_start)
			continue;

		if (port_vid->local_port > local_port_end)
			break;

		rec_num = port_vid->local_port % MLXSW_REG_REIV_REC_MAX_COUNT;
		mlxsw_reg_reiv_rec_update_set(reiv_pl, rec_num, true);
		mlxsw_reg_reiv_rec_evid_set(reiv_pl, rec_num,
					    valid ? port_vid->vid : 0);
		entries_num++;
	}

	if (!entries_num) {
		kfree(reiv_pl);
		return 0;
	}

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(reiv), reiv_pl);
	if (err)
		goto err_reg_write;

	kfree(reiv_pl);
	return 0;

err_reg_write:
	kfree(reiv_pl);
	return err;
}

static int mlxsw_sp_fid_erif_eport_to_vid_map(struct mlxsw_sp_fid *fid,
					      u16 rif_index, bool valid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	u8 num_port_pages;
	int err, i;

	num_port_pages = mlxsw_core_max_ports(mlxsw_sp->core) /
			 MLXSW_REG_REIV_REC_MAX_COUNT + 1;

	for (i = 0; i < num_port_pages; i++) {
		err = mlxsw_sp_fid_reiv_handle(fid, rif_index, valid, i);
		if (err)
			goto err_reiv_handle;
	}

	return 0;

err_reiv_handle:
	for (; i >= 0; i--)
		mlxsw_sp_fid_reiv_handle(fid, rif_index, !valid, i);
	return err;
}

int mlxsw_sp_fid_rif_set(struct mlxsw_sp_fid *fid, struct mlxsw_sp_rif *rif)
{
	u16 rif_index = mlxsw_sp_rif_index(rif);
	int err;

	err = mlxsw_sp_fid_to_fid_rif_update(fid, rif);
	if (err)
		return err;

	err = mlxsw_sp_fid_vni_to_fid_rif_update(fid, rif);
	if (err)
		goto err_vni_to_fid_rif_update;

	err = mlxsw_sp_fid_vid_to_fid_rif_set(fid, rif);
	if (err)
		goto err_vid_to_fid_rif_set;

	err = mlxsw_sp_fid_erif_eport_to_vid_map(fid, rif_index, true);
	if (err)
		goto err_erif_eport_to_vid_map;

	fid->rif = rif;
	return 0;

err_erif_eport_to_vid_map:
	mlxsw_sp_fid_vid_to_fid_rif_unset(fid);
err_vid_to_fid_rif_set:
	mlxsw_sp_fid_vni_to_fid_rif_update(fid, NULL);
err_vni_to_fid_rif_update:
	mlxsw_sp_fid_to_fid_rif_update(fid, NULL);
	return err;
}

void mlxsw_sp_fid_rif_unset(struct mlxsw_sp_fid *fid)
{
	u16 rif_index;

	if (!fid->rif)
		return;

	rif_index = mlxsw_sp_rif_index(fid->rif);
	fid->rif = NULL;

	mlxsw_sp_fid_erif_eport_to_vid_map(fid, rif_index, false);
	mlxsw_sp_fid_vid_to_fid_rif_unset(fid);
	mlxsw_sp_fid_vni_to_fid_rif_update(fid, NULL);
	mlxsw_sp_fid_to_fid_rif_update(fid, NULL);
}

static int mlxsw_sp_fid_vni_op(const struct mlxsw_sp_fid *fid)
{
	int err;

	err = mlxsw_sp_fid_vni_to_fid_map(fid, fid->rif, fid->vni_valid);
	if (err)
		return err;

	err = mlxsw_sp_fid_edit_op(fid, fid->rif);
	if (err)
		goto err_fid_edit_op;

	return 0;

err_fid_edit_op:
	mlxsw_sp_fid_vni_to_fid_map(fid, fid->rif, !fid->vni_valid);
	return err;
}

static int __mlxsw_sp_fid_port_vid_map(const struct mlxsw_sp_fid *fid,
				       u16 local_port, u16 vid, bool valid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char svfa_pl[MLXSW_REG_SVFA_LEN];
	bool irif_valid = false;
	u16 irif_index = 0;

	if (fid->rif) {
		irif_valid = true;
		irif_index = mlxsw_sp_rif_index(fid->rif);
	}

	mlxsw_reg_svfa_port_vid_pack(svfa_pl, local_port, valid, fid->fid_index,
				     vid, irif_valid, irif_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static struct mlxsw_sp_fid_8021d *
mlxsw_sp_fid_8021d_fid(const struct mlxsw_sp_fid *fid)
{
	return container_of(fid, struct mlxsw_sp_fid_8021d, common);
}

static void mlxsw_sp_fid_8021d_setup(struct mlxsw_sp_fid *fid, const void *arg)
{
	int br_ifindex = *(int *) arg;

	mlxsw_sp_fid_8021d_fid(fid)->br_ifindex = br_ifindex;
	fid->fid_offset = fid->fid_index - fid->fid_family->start_index;
}

static int mlxsw_sp_fid_8021d_configure(struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_op(fid, true);
}

static void mlxsw_sp_fid_8021d_deconfigure(struct mlxsw_sp_fid *fid)
{
	if (fid->vni_valid)
		mlxsw_sp_nve_fid_disable(fid->fid_family->mlxsw_sp, fid);
	mlxsw_sp_fid_op(fid, false);
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

static int mlxsw_sp_port_vp_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	int err;

	list_for_each_entry(mlxsw_sp_port_vlan, &mlxsw_sp_port->vlans_list,
			    list) {
		struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
		u16 vid = mlxsw_sp_port_vlan->vid;

		if (!fid)
			continue;

		err = __mlxsw_sp_fid_port_vid_map(fid,
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

		__mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid,
					    false);
	}
	return err;
}

static void mlxsw_sp_port_vlan_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);

	list_for_each_entry_reverse(mlxsw_sp_port_vlan,
				    &mlxsw_sp_port->vlans_list, list) {
		struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
		u16 vid = mlxsw_sp_port_vlan->vid;

		if (!fid)
			continue;

		__mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid,
					    false);
	}
}

static int
mlxsw_sp_fid_port_vid_list_add(struct mlxsw_sp_fid *fid, u16 local_port,
			       u16 vid)
{
	struct mlxsw_sp_fid_port_vid *port_vid, *tmp_port_vid;

	port_vid = kzalloc(sizeof(*port_vid), GFP_KERNEL);
	if (!port_vid)
		return -ENOMEM;

	port_vid->local_port = local_port;
	port_vid->vid = vid;

	list_for_each_entry(tmp_port_vid, &fid->port_vid_list, list) {
		if (tmp_port_vid->local_port > local_port)
			break;
	}

	list_add_tail(&port_vid->list, &tmp_port_vid->list);
	return 0;
}

static void
mlxsw_sp_fid_port_vid_list_del(struct mlxsw_sp_fid *fid, u16 local_port,
			       u16 vid)
{
	struct mlxsw_sp_fid_port_vid *port_vid, *tmp;

	list_for_each_entry_safe(port_vid, tmp, &fid->port_vid_list, list) {
		if (port_vid->local_port != local_port || port_vid->vid != vid)
			continue;

		list_del(&port_vid->list);
		kfree(port_vid);
		return;
	}
}

static int
mlxsw_sp_fid_mpe_table_map(const struct mlxsw_sp_fid *fid, u16 local_port,
			   u16 vid, bool valid)
{
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	char smpe_pl[MLXSW_REG_SMPE_LEN];

	mlxsw_reg_smpe_pack(smpe_pl, local_port, fid->fid_index,
			    valid ? vid : 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(smpe), smpe_pl);
}

static int
mlxsw_sp_fid_erif_eport_to_vid_map_one(const struct mlxsw_sp_fid *fid,
				       u16 local_port, u16 vid, bool valid)
{
	u8 port_page = local_port / MLXSW_REG_REIV_REC_MAX_COUNT;
	u8 rec_num = local_port % MLXSW_REG_REIV_REC_MAX_COUNT;
	struct mlxsw_sp *mlxsw_sp = fid->fid_family->mlxsw_sp;
	u16 rif_index = mlxsw_sp_rif_index(fid->rif);
	char *reiv_pl;
	int err;

	reiv_pl = kmalloc(MLXSW_REG_REIV_LEN, GFP_KERNEL);
	if (!reiv_pl)
		return -ENOMEM;

	mlxsw_reg_reiv_pack(reiv_pl, port_page, rif_index);
	mlxsw_reg_reiv_rec_update_set(reiv_pl, rec_num, true);
	mlxsw_reg_reiv_rec_evid_set(reiv_pl, rec_num, valid ? vid : 0);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(reiv), reiv_pl);
	kfree(reiv_pl);
	return err;
}

static int mlxsw_sp_fid_evid_map(const struct mlxsw_sp_fid *fid, u16 local_port,
				 u16 vid, bool valid)
{
	int err;

	err = mlxsw_sp_fid_mpe_table_map(fid, local_port, vid, valid);
	if (err)
		return err;

	if (!fid->rif)
		return 0;

	err = mlxsw_sp_fid_erif_eport_to_vid_map_one(fid, local_port, vid,
						     valid);
	if (err)
		goto err_erif_eport_to_vid_map_one;

	return 0;

err_erif_eport_to_vid_map_one:
	mlxsw_sp_fid_mpe_table_map(fid, local_port, vid, !valid);
	return err;
}

static int mlxsw_sp_fid_8021d_port_vid_map(struct mlxsw_sp_fid *fid,
					   struct mlxsw_sp_port *mlxsw_sp_port,
					   u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 local_port = mlxsw_sp_port->local_port;
	int err;

	err = __mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid,
					  true);
	if (err)
		return err;

	err = mlxsw_sp_fid_evid_map(fid, local_port, vid, true);
	if (err)
		goto err_fid_evid_map;

	err = mlxsw_sp_fid_port_vid_list_add(fid, mlxsw_sp_port->local_port,
					     vid);
	if (err)
		goto err_port_vid_list_add;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port]++ == 0) {
		err = mlxsw_sp_port_vp_mode_trans(mlxsw_sp_port);
		if (err)
			goto err_port_vp_mode_trans;
	}

	return 0;

err_port_vp_mode_trans:
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
	mlxsw_sp_fid_port_vid_list_del(fid, mlxsw_sp_port->local_port, vid);
err_port_vid_list_add:
	mlxsw_sp_fid_evid_map(fid, local_port, vid, false);
err_fid_evid_map:
	__mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid, false);
	return err;
}

static void
mlxsw_sp_fid_8021d_port_vid_unmap(struct mlxsw_sp_fid *fid,
				  struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 local_port = mlxsw_sp_port->local_port;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port] == 1)
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
	mlxsw_sp_fid_port_vid_list_del(fid, mlxsw_sp_port->local_port, vid);
	mlxsw_sp_fid_evid_map(fid, local_port, vid, false);
	__mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid, false);
}

static int mlxsw_sp_fid_8021d_vni_set(struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_vni_op(fid);
}

static void mlxsw_sp_fid_8021d_vni_clear(struct mlxsw_sp_fid *fid)
{
	mlxsw_sp_fid_vni_op(fid);
}

static int mlxsw_sp_fid_8021d_nve_flood_index_set(struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_edit_op(fid, fid->rif);
}

static void mlxsw_sp_fid_8021d_nve_flood_index_clear(struct mlxsw_sp_fid *fid)
{
	mlxsw_sp_fid_edit_op(fid, fid->rif);
}

static void
mlxsw_sp_fid_8021d_fdb_clear_offload(const struct mlxsw_sp_fid *fid,
				     const struct net_device *nve_dev)
{
	br_fdb_clear_offload(nve_dev, 0);
}

static int
mlxsw_sp_fid_8021d_vid_to_fid_rif_update(const struct mlxsw_sp_fid *fid,
					 const struct mlxsw_sp_rif *rif)
{
	return 0;
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_8021d_ops = {
	.setup			= mlxsw_sp_fid_8021d_setup,
	.configure		= mlxsw_sp_fid_8021d_configure,
	.deconfigure		= mlxsw_sp_fid_8021d_deconfigure,
	.index_alloc		= mlxsw_sp_fid_8021d_index_alloc,
	.compare		= mlxsw_sp_fid_8021d_compare,
	.port_vid_map		= mlxsw_sp_fid_8021d_port_vid_map,
	.port_vid_unmap		= mlxsw_sp_fid_8021d_port_vid_unmap,
	.vni_set		= mlxsw_sp_fid_8021d_vni_set,
	.vni_clear		= mlxsw_sp_fid_8021d_vni_clear,
	.nve_flood_index_set	= mlxsw_sp_fid_8021d_nve_flood_index_set,
	.nve_flood_index_clear	= mlxsw_sp_fid_8021d_nve_flood_index_clear,
	.fdb_clear_offload	= mlxsw_sp_fid_8021d_fdb_clear_offload,
	.vid_to_fid_rif_update  = mlxsw_sp_fid_8021d_vid_to_fid_rif_update,
};

#define MLXSW_SP_FID_8021Q_MAX (VLAN_N_VID - 2)
#define MLXSW_SP_FID_RFID_MAX (11 * 1024)
#define MLXSW_SP_FID_8021Q_PGT_BASE 0
#define MLXSW_SP_FID_8021D_PGT_BASE (3 * MLXSW_SP_FID_8021Q_MAX)

static const struct mlxsw_sp_flood_table mlxsw_sp_fid_8021d_flood_tables[] = {
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_UC,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET,
		.table_index	= 0,
	},
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_MC,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET,
		.table_index	= 1,
	},
	{
		.packet_type	= MLXSW_SP_FLOOD_TYPE_BC,
		.table_type	= MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFSET,
		.table_index	= 2,
	},
};

static bool
mlxsw_sp_fid_8021q_compare(const struct mlxsw_sp_fid *fid, const void *arg)
{
	u16 vid = *(u16 *) arg;

	return mlxsw_sp_fid_8021q_fid(fid)->vid == vid;
}

static void
mlxsw_sp_fid_8021q_fdb_clear_offload(const struct mlxsw_sp_fid *fid,
				     const struct net_device *nve_dev)
{
	br_fdb_clear_offload(nve_dev, mlxsw_sp_fid_8021q_vid(fid));
}

static void mlxsw_sp_fid_rfid_setup(struct mlxsw_sp_fid *fid, const void *arg)
{
	fid->fid_offset = 0;
}

static int mlxsw_sp_fid_rfid_configure(struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_op(fid, true);
}

static void mlxsw_sp_fid_rfid_deconfigure(struct mlxsw_sp_fid *fid)
{
	mlxsw_sp_fid_op(fid, false);
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
	u16 local_port = mlxsw_sp_port->local_port;
	int err;

	err = mlxsw_sp_fid_port_vid_list_add(fid, mlxsw_sp_port->local_port,
					     vid);
	if (err)
		return err;

	/* Using legacy bridge model, we only need to transition the port to
	 * virtual mode since {Port, VID} => FID is done by the firmware upon
	 * RIF creation. Using unified bridge model, we need to map
	 * {Port, VID} => FID and map egress VID.
	 */
	err = __mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid,
					  true);
	if (err)
		goto err_port_vid_map;

	if (fid->rif) {
		err = mlxsw_sp_fid_erif_eport_to_vid_map_one(fid, local_port,
							     vid, true);
		if (err)
			goto err_erif_eport_to_vid_map_one;
	}

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port]++ == 0) {
		err = mlxsw_sp_port_vp_mode_trans(mlxsw_sp_port);
		if (err)
			goto err_port_vp_mode_trans;
	}

	return 0;

err_port_vp_mode_trans:
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;
	if (fid->rif)
		mlxsw_sp_fid_erif_eport_to_vid_map_one(fid, local_port, vid,
						       false);
err_erif_eport_to_vid_map_one:
	__mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid, false);
err_port_vid_map:
	mlxsw_sp_fid_port_vid_list_del(fid, mlxsw_sp_port->local_port, vid);
	return err;
}

static void
mlxsw_sp_fid_rfid_port_vid_unmap(struct mlxsw_sp_fid *fid,
				 struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 local_port = mlxsw_sp_port->local_port;

	if (mlxsw_sp->fid_core->port_fid_mappings[local_port] == 1)
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
	mlxsw_sp->fid_core->port_fid_mappings[local_port]--;

	if (fid->rif)
		mlxsw_sp_fid_erif_eport_to_vid_map_one(fid, local_port, vid,
						       false);
	__mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port->local_port, vid, false);
	mlxsw_sp_fid_port_vid_list_del(fid, mlxsw_sp_port->local_port, vid);
}

static int mlxsw_sp_fid_rfid_vni_set(struct mlxsw_sp_fid *fid)
{
	return -EOPNOTSUPP;
}

static void mlxsw_sp_fid_rfid_vni_clear(struct mlxsw_sp_fid *fid)
{
	WARN_ON_ONCE(1);
}

static int mlxsw_sp_fid_rfid_nve_flood_index_set(struct mlxsw_sp_fid *fid)
{
	return -EOPNOTSUPP;
}

static void mlxsw_sp_fid_rfid_nve_flood_index_clear(struct mlxsw_sp_fid *fid)
{
	WARN_ON_ONCE(1);
}

static int
mlxsw_sp_fid_rfid_vid_to_fid_rif_update(const struct mlxsw_sp_fid *fid,
					const struct mlxsw_sp_rif *rif)
{
	return 0;
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_rfid_ops = {
	.setup			= mlxsw_sp_fid_rfid_setup,
	.configure		= mlxsw_sp_fid_rfid_configure,
	.deconfigure		= mlxsw_sp_fid_rfid_deconfigure,
	.index_alloc		= mlxsw_sp_fid_rfid_index_alloc,
	.compare		= mlxsw_sp_fid_rfid_compare,
	.port_vid_map		= mlxsw_sp_fid_rfid_port_vid_map,
	.port_vid_unmap		= mlxsw_sp_fid_rfid_port_vid_unmap,
	.vni_set                = mlxsw_sp_fid_rfid_vni_set,
	.vni_clear		= mlxsw_sp_fid_rfid_vni_clear,
	.nve_flood_index_set	= mlxsw_sp_fid_rfid_nve_flood_index_set,
	.nve_flood_index_clear	= mlxsw_sp_fid_rfid_nve_flood_index_clear,
	.vid_to_fid_rif_update  = mlxsw_sp_fid_rfid_vid_to_fid_rif_update,
};

static void mlxsw_sp_fid_dummy_setup(struct mlxsw_sp_fid *fid, const void *arg)
{
	fid->fid_offset = 0;
}

static int mlxsw_sp_fid_dummy_configure(struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_op(fid, true);
}

static void mlxsw_sp_fid_dummy_deconfigure(struct mlxsw_sp_fid *fid)
{
	mlxsw_sp_fid_op(fid, false);
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

static int mlxsw_sp_fid_dummy_vni_set(struct mlxsw_sp_fid *fid)
{
	return -EOPNOTSUPP;
}

static void mlxsw_sp_fid_dummy_vni_clear(struct mlxsw_sp_fid *fid)
{
	WARN_ON_ONCE(1);
}

static int mlxsw_sp_fid_dummy_nve_flood_index_set(struct mlxsw_sp_fid *fid)
{
	return -EOPNOTSUPP;
}

static void mlxsw_sp_fid_dummy_nve_flood_index_clear(struct mlxsw_sp_fid *fid)
{
	WARN_ON_ONCE(1);
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_dummy_ops = {
	.setup			= mlxsw_sp_fid_dummy_setup,
	.configure		= mlxsw_sp_fid_dummy_configure,
	.deconfigure		= mlxsw_sp_fid_dummy_deconfigure,
	.index_alloc		= mlxsw_sp_fid_dummy_index_alloc,
	.compare		= mlxsw_sp_fid_dummy_compare,
	.vni_set                = mlxsw_sp_fid_dummy_vni_set,
	.vni_clear		= mlxsw_sp_fid_dummy_vni_clear,
	.nve_flood_index_set	= mlxsw_sp_fid_dummy_nve_flood_index_set,
	.nve_flood_index_clear	= mlxsw_sp_fid_dummy_nve_flood_index_clear,
};

static int mlxsw_sp_fid_8021q_configure(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_fid_8021q *fid_8021q = mlxsw_sp_fid_8021q_fid(fid);
	int err;

	err = mlxsw_sp_fid_op(fid, true);
	if (err)
		return err;

	err = mlxsw_sp_fid_vid_to_fid_map(fid, fid_8021q->vid, true, fid->rif);
	if (err)
		goto err_vid_to_fid_map;

	return 0;

err_vid_to_fid_map:
	mlxsw_sp_fid_op(fid, false);
	return err;
}

static void mlxsw_sp_fid_8021q_deconfigure(struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_fid_8021q *fid_8021q = mlxsw_sp_fid_8021q_fid(fid);

	if (fid->vni_valid)
		mlxsw_sp_nve_fid_disable(fid->fid_family->mlxsw_sp, fid);

	mlxsw_sp_fid_vid_to_fid_map(fid, fid_8021q->vid, false, NULL);
	mlxsw_sp_fid_op(fid, false);
}

static int mlxsw_sp_fid_8021q_port_vid_map(struct mlxsw_sp_fid *fid,
					   struct mlxsw_sp_port *mlxsw_sp_port,
					   u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 local_port = mlxsw_sp_port->local_port;
	int err;

	/* In case there are no {Port, VID} => FID mappings on the port,
	 * we can use the global VID => FID mapping we created when the
	 * FID was configured, otherwise, configure new mapping.
	 */
	if (mlxsw_sp->fid_core->port_fid_mappings[local_port]) {
		err =  __mlxsw_sp_fid_port_vid_map(fid, local_port, vid, true);
		if (err)
			return err;
	}

	err = mlxsw_sp_fid_evid_map(fid, local_port, vid, true);
	if (err)
		goto err_fid_evid_map;

	err = mlxsw_sp_fid_port_vid_list_add(fid, mlxsw_sp_port->local_port,
					     vid);
	if (err)
		goto err_port_vid_list_add;

	return 0;

err_port_vid_list_add:
	 mlxsw_sp_fid_evid_map(fid, local_port, vid, false);
err_fid_evid_map:
	if (mlxsw_sp->fid_core->port_fid_mappings[local_port])
		__mlxsw_sp_fid_port_vid_map(fid, local_port, vid, false);
	return err;
}

static void
mlxsw_sp_fid_8021q_port_vid_unmap(struct mlxsw_sp_fid *fid,
				  struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 local_port = mlxsw_sp_port->local_port;

	mlxsw_sp_fid_port_vid_list_del(fid, mlxsw_sp_port->local_port, vid);
	mlxsw_sp_fid_evid_map(fid, local_port, vid, false);
	if (mlxsw_sp->fid_core->port_fid_mappings[local_port])
		__mlxsw_sp_fid_port_vid_map(fid, local_port, vid, false);
}

static const struct mlxsw_sp_fid_ops mlxsw_sp_fid_8021q_ops = {
	.setup			= mlxsw_sp_fid_8021q_setup,
	.configure		= mlxsw_sp_fid_8021q_configure,
	.deconfigure		= mlxsw_sp_fid_8021q_deconfigure,
	.index_alloc		= mlxsw_sp_fid_8021d_index_alloc,
	.compare		= mlxsw_sp_fid_8021q_compare,
	.port_vid_map		= mlxsw_sp_fid_8021q_port_vid_map,
	.port_vid_unmap		= mlxsw_sp_fid_8021q_port_vid_unmap,
	.vni_set		= mlxsw_sp_fid_8021d_vni_set,
	.vni_clear		= mlxsw_sp_fid_8021d_vni_clear,
	.nve_flood_index_set	= mlxsw_sp_fid_8021d_nve_flood_index_set,
	.nve_flood_index_clear	= mlxsw_sp_fid_8021d_nve_flood_index_clear,
	.fdb_clear_offload	= mlxsw_sp_fid_8021q_fdb_clear_offload,
	.vid_to_fid_rif_update  = mlxsw_sp_fid_8021q_vid_to_fid_rif_update,
};

/* There are 4K-2 802.1Q FIDs */
#define MLXSW_SP_FID_8021Q_START	1 /* FID 0 is reserved. */
#define MLXSW_SP_FID_8021Q_END		(MLXSW_SP_FID_8021Q_START + \
					 MLXSW_SP_FID_8021Q_MAX - 1)

/* There are 1K 802.1D FIDs */
#define MLXSW_SP_FID_8021D_START	(MLXSW_SP_FID_8021Q_END + 1)
#define MLXSW_SP_FID_8021D_END		(MLXSW_SP_FID_8021D_START + \
					 MLXSW_SP_FID_8021D_MAX - 1)

/* There is one dummy FID */
#define MLXSW_SP_FID_DUMMY		(MLXSW_SP_FID_8021D_END + 1)

/* There are 11K rFIDs */
#define MLXSW_SP_RFID_START		(MLXSW_SP_FID_DUMMY + 1)
#define MLXSW_SP_RFID_END		(MLXSW_SP_RFID_START + \
					 MLXSW_SP_FID_RFID_MAX - 1)

static const struct mlxsw_sp_fid_family mlxsw_sp1_fid_8021q_family = {
	.type			= MLXSW_SP_FID_TYPE_8021Q,
	.fid_size		= sizeof(struct mlxsw_sp_fid_8021q),
	.start_index		= MLXSW_SP_FID_8021Q_START,
	.end_index		= MLXSW_SP_FID_8021Q_END,
	.flood_tables		= mlxsw_sp_fid_8021d_flood_tables,
	.nr_flood_tables	= ARRAY_SIZE(mlxsw_sp_fid_8021d_flood_tables),
	.rif_type		= MLXSW_SP_RIF_TYPE_VLAN,
	.ops			= &mlxsw_sp_fid_8021q_ops,
	.flood_rsp              = false,
	.bridge_type            = MLXSW_REG_BRIDGE_TYPE_0,
	.pgt_base		= MLXSW_SP_FID_8021Q_PGT_BASE,
	.smpe_index_valid	= false,
};

static const struct mlxsw_sp_fid_family mlxsw_sp1_fid_8021d_family = {
	.type			= MLXSW_SP_FID_TYPE_8021D,
	.fid_size		= sizeof(struct mlxsw_sp_fid_8021d),
	.start_index		= MLXSW_SP_FID_8021D_START,
	.end_index		= MLXSW_SP_FID_8021D_END,
	.flood_tables		= mlxsw_sp_fid_8021d_flood_tables,
	.nr_flood_tables	= ARRAY_SIZE(mlxsw_sp_fid_8021d_flood_tables),
	.rif_type		= MLXSW_SP_RIF_TYPE_FID,
	.ops			= &mlxsw_sp_fid_8021d_ops,
	.bridge_type            = MLXSW_REG_BRIDGE_TYPE_1,
	.pgt_base		= MLXSW_SP_FID_8021D_PGT_BASE,
	.smpe_index_valid       = false,
};

static const struct mlxsw_sp_fid_family mlxsw_sp1_fid_dummy_family = {
	.type			= MLXSW_SP_FID_TYPE_DUMMY,
	.fid_size		= sizeof(struct mlxsw_sp_fid),
	.start_index		= MLXSW_SP_FID_DUMMY,
	.end_index		= MLXSW_SP_FID_DUMMY,
	.ops			= &mlxsw_sp_fid_dummy_ops,
	.smpe_index_valid       = false,
};

static const struct mlxsw_sp_fid_family mlxsw_sp_fid_rfid_family = {
	.type			= MLXSW_SP_FID_TYPE_RFID,
	.fid_size		= sizeof(struct mlxsw_sp_fid),
	.start_index		= MLXSW_SP_RFID_START,
	.end_index		= MLXSW_SP_RFID_END,
	.rif_type		= MLXSW_SP_RIF_TYPE_SUBPORT,
	.ops			= &mlxsw_sp_fid_rfid_ops,
	.flood_rsp              = true,
	.smpe_index_valid       = false,
};

const struct mlxsw_sp_fid_family *mlxsw_sp1_fid_family_arr[] = {
	[MLXSW_SP_FID_TYPE_8021Q]	= &mlxsw_sp1_fid_8021q_family,
	[MLXSW_SP_FID_TYPE_8021D]	= &mlxsw_sp1_fid_8021d_family,
	[MLXSW_SP_FID_TYPE_DUMMY]	= &mlxsw_sp1_fid_dummy_family,
	[MLXSW_SP_FID_TYPE_RFID]	= &mlxsw_sp_fid_rfid_family,
};

static const struct mlxsw_sp_fid_family mlxsw_sp2_fid_8021q_family = {
	.type			= MLXSW_SP_FID_TYPE_8021Q,
	.fid_size		= sizeof(struct mlxsw_sp_fid_8021q),
	.start_index		= MLXSW_SP_FID_8021Q_START,
	.end_index		= MLXSW_SP_FID_8021Q_END,
	.flood_tables		= mlxsw_sp_fid_8021d_flood_tables,
	.nr_flood_tables	= ARRAY_SIZE(mlxsw_sp_fid_8021d_flood_tables),
	.rif_type		= MLXSW_SP_RIF_TYPE_VLAN,
	.ops			= &mlxsw_sp_fid_8021q_ops,
	.flood_rsp              = false,
	.bridge_type            = MLXSW_REG_BRIDGE_TYPE_0,
	.pgt_base		= MLXSW_SP_FID_8021Q_PGT_BASE,
	.smpe_index_valid	= true,
};

static const struct mlxsw_sp_fid_family mlxsw_sp2_fid_8021d_family = {
	.type			= MLXSW_SP_FID_TYPE_8021D,
	.fid_size		= sizeof(struct mlxsw_sp_fid_8021d),
	.start_index		= MLXSW_SP_FID_8021D_START,
	.end_index		= MLXSW_SP_FID_8021D_END,
	.flood_tables		= mlxsw_sp_fid_8021d_flood_tables,
	.nr_flood_tables	= ARRAY_SIZE(mlxsw_sp_fid_8021d_flood_tables),
	.rif_type		= MLXSW_SP_RIF_TYPE_FID,
	.ops			= &mlxsw_sp_fid_8021d_ops,
	.bridge_type            = MLXSW_REG_BRIDGE_TYPE_1,
	.pgt_base		= MLXSW_SP_FID_8021D_PGT_BASE,
	.smpe_index_valid       = true,
};

static const struct mlxsw_sp_fid_family mlxsw_sp2_fid_dummy_family = {
	.type			= MLXSW_SP_FID_TYPE_DUMMY,
	.fid_size		= sizeof(struct mlxsw_sp_fid),
	.start_index		= MLXSW_SP_FID_DUMMY,
	.end_index		= MLXSW_SP_FID_DUMMY,
	.ops			= &mlxsw_sp_fid_dummy_ops,
	.smpe_index_valid       = false,
};

const struct mlxsw_sp_fid_family *mlxsw_sp2_fid_family_arr[] = {
	[MLXSW_SP_FID_TYPE_8021Q]	= &mlxsw_sp2_fid_8021q_family,
	[MLXSW_SP_FID_TYPE_8021D]	= &mlxsw_sp2_fid_8021d_family,
	[MLXSW_SP_FID_TYPE_DUMMY]	= &mlxsw_sp2_fid_dummy_family,
	[MLXSW_SP_FID_TYPE_RFID]	= &mlxsw_sp_fid_rfid_family,
};

static struct mlxsw_sp_fid *mlxsw_sp_fid_lookup(struct mlxsw_sp *mlxsw_sp,
						enum mlxsw_sp_fid_type type,
						const void *arg)
{
	struct mlxsw_sp_fid_family *fid_family;
	struct mlxsw_sp_fid *fid;

	fid_family = mlxsw_sp->fid_core->fid_family_arr[type];
	list_for_each_entry(fid, &fid_family->fids_list, list) {
		if (!fid->fid_family->ops->compare(fid, arg))
			continue;
		refcount_inc(&fid->ref_count);
		return fid;
	}

	return NULL;
}

static struct mlxsw_sp_fid *mlxsw_sp_fid_get(struct mlxsw_sp *mlxsw_sp,
					     enum mlxsw_sp_fid_type type,
					     const void *arg)
{
	struct mlxsw_sp_fid_family *fid_family;
	struct mlxsw_sp_fid *fid;
	u16 fid_index;
	int err;

	fid = mlxsw_sp_fid_lookup(mlxsw_sp, type, arg);
	if (fid)
		return fid;

	fid_family = mlxsw_sp->fid_core->fid_family_arr[type];
	fid = kzalloc(fid_family->fid_size, GFP_KERNEL);
	if (!fid)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&fid->port_vid_list);
	fid->fid_family = fid_family;

	err = fid->fid_family->ops->index_alloc(fid, arg, &fid_index);
	if (err)
		goto err_index_alloc;
	fid->fid_index = fid_index;
	__set_bit(fid_index - fid_family->start_index, fid_family->fids_bitmap);

	fid->fid_family->ops->setup(fid, arg);

	err = fid->fid_family->ops->configure(fid);
	if (err)
		goto err_configure;

	err = rhashtable_insert_fast(&mlxsw_sp->fid_core->fid_ht, &fid->ht_node,
				     mlxsw_sp_fid_ht_params);
	if (err)
		goto err_rhashtable_insert;

	list_add(&fid->list, &fid_family->fids_list);
	refcount_set(&fid->ref_count, 1);
	return fid;

err_rhashtable_insert:
	fid->fid_family->ops->deconfigure(fid);
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
	struct mlxsw_sp *mlxsw_sp = fid_family->mlxsw_sp;

	if (!refcount_dec_and_test(&fid->ref_count))
		return;

	list_del(&fid->list);
	rhashtable_remove_fast(&mlxsw_sp->fid_core->fid_ht,
			       &fid->ht_node, mlxsw_sp_fid_ht_params);
	fid->fid_family->ops->deconfigure(fid);
	__clear_bit(fid->fid_index - fid_family->start_index,
		    fid_family->fids_bitmap);
	WARN_ON_ONCE(!list_empty(&fid->port_vid_list));
	kfree(fid);
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

struct mlxsw_sp_fid *mlxsw_sp_fid_8021q_lookup(struct mlxsw_sp *mlxsw_sp,
					       u16 vid)
{
	return mlxsw_sp_fid_lookup(mlxsw_sp, MLXSW_SP_FID_TYPE_8021Q, &vid);
}

struct mlxsw_sp_fid *mlxsw_sp_fid_8021d_lookup(struct mlxsw_sp *mlxsw_sp,
					       int br_ifindex)
{
	return mlxsw_sp_fid_lookup(mlxsw_sp, MLXSW_SP_FID_TYPE_8021D,
				   &br_ifindex);
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
	struct mlxsw_sp *mlxsw_sp = fid_family->mlxsw_sp;
	const int *sfgc_packet_types;
	u16 num_fids, mid_base;
	int err, i;

	mid_base = mlxsw_sp_fid_flood_table_mid(fid_family, flood_table, 0);
	num_fids = mlxsw_sp_fid_family_num_fids(fid_family);
	err = mlxsw_sp_pgt_mid_alloc_range(mlxsw_sp, mid_base, num_fids);
	if (err)
		return err;

	sfgc_packet_types = mlxsw_sp_packet_type_sfgc_types[packet_type];
	for (i = 0; i < MLXSW_REG_SFGC_TYPE_MAX; i++) {
		char sfgc_pl[MLXSW_REG_SFGC_LEN];

		if (!sfgc_packet_types[i])
			continue;

		mlxsw_reg_sfgc_pack(sfgc_pl, i, fid_family->bridge_type,
				    flood_table->table_type, 0, mid_base);

		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfgc), sfgc_pl);
		if (err)
			goto err_reg_write;
	}

	return 0;

err_reg_write:
	mlxsw_sp_pgt_mid_free_range(mlxsw_sp, mid_base, num_fids);
	return err;
}

static void
mlxsw_sp_fid_flood_table_fini(struct mlxsw_sp_fid_family *fid_family,
			      const struct mlxsw_sp_flood_table *flood_table)
{
	struct mlxsw_sp *mlxsw_sp = fid_family->mlxsw_sp;
	u16 num_fids, mid_base;

	mid_base = mlxsw_sp_fid_flood_table_mid(fid_family, flood_table, 0);
	num_fids = mlxsw_sp_fid_family_num_fids(fid_family);
	mlxsw_sp_pgt_mid_free_range(mlxsw_sp, mid_base, num_fids);
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

static void
mlxsw_sp_fid_flood_tables_fini(struct mlxsw_sp_fid_family *fid_family)
{
	int i;

	for (i = 0; i < fid_family->nr_flood_tables; i++) {
		const struct mlxsw_sp_flood_table *flood_table;

		flood_table = &fid_family->flood_tables[i];
		mlxsw_sp_fid_flood_table_fini(fid_family, flood_table);
	}
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
	fid_family->fids_bitmap = bitmap_zalloc(nr_fids, GFP_KERNEL);
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
	bitmap_free(fid_family->fids_bitmap);
err_alloc_fids_bitmap:
	kfree(fid_family);
	return err;
}

static void
mlxsw_sp_fid_family_unregister(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fid_family *fid_family)
{
	mlxsw_sp->fid_core->fid_family_arr[fid_family->type] = NULL;

	if (fid_family->flood_tables)
		mlxsw_sp_fid_flood_tables_fini(fid_family);

	bitmap_free(fid_family->fids_bitmap);
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

	err = rhashtable_init(&fid_core->fid_ht, &mlxsw_sp_fid_ht_params);
	if (err)
		goto err_rhashtable_fid_init;

	err = rhashtable_init(&fid_core->vni_ht, &mlxsw_sp_fid_vni_ht_params);
	if (err)
		goto err_rhashtable_vni_init;

	fid_core->port_fid_mappings = kcalloc(max_ports, sizeof(unsigned int),
					      GFP_KERNEL);
	if (!fid_core->port_fid_mappings) {
		err = -ENOMEM;
		goto err_alloc_port_fid_mappings;
	}

	for (i = 0; i < MLXSW_SP_FID_TYPE_MAX; i++) {
		err = mlxsw_sp_fid_family_register(mlxsw_sp,
						   mlxsw_sp->fid_family_arr[i]);

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
	rhashtable_destroy(&fid_core->vni_ht);
err_rhashtable_vni_init:
	rhashtable_destroy(&fid_core->fid_ht);
err_rhashtable_fid_init:
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
	rhashtable_destroy(&fid_core->vni_ht);
	rhashtable_destroy(&fid_core->fid_ht);
	kfree(fid_core);
}
