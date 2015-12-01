/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/flow_table.h>
#include "mlx5_core.h"
#include "eswitch.h"

#define UPLINK_VPORT 0xFFFF

#define MLX5_DEBUG_ESWITCH_MASK BIT(3)

#define esw_info(dev, format, ...)				\
	pr_info("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_warn(dev, format, ...)				\
	pr_warn("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_debug(dev, format, ...)				\
	mlx5_core_dbg_mask(dev, MLX5_DEBUG_ESWITCH_MASK, format, ##__VA_ARGS__)

enum {
	MLX5_ACTION_NONE = 0,
	MLX5_ACTION_ADD  = 1,
	MLX5_ACTION_DEL  = 2,
};

/* E-Switch UC L2 table hash node */
struct esw_uc_addr {
	struct l2addr_node node;
	u32                table_index;
	u32                vport;
};

/* E-Switch MC FDB table hash node */
struct esw_mc_addr { /* SRIOV only */
	struct l2addr_node     node;
	struct mlx5_flow_rule *uplink_rule; /* Forward to uplink rule */
	u32                    refcnt;
};

/* Vport UC/MC hash node */
struct vport_addr {
	struct l2addr_node     node;
	u8                     action;
	u32                    vport;
	struct mlx5_flow_rule *flow_rule; /* SRIOV only */
};

enum {
	UC_ADDR_CHANGE = BIT(0),
	MC_ADDR_CHANGE = BIT(1),
};

/* Vport context events */
#define SRIOV_VPORT_EVENTS (UC_ADDR_CHANGE | \
			    MC_ADDR_CHANGE)

static int arm_vport_context_events_cmd(struct mlx5_core_dev *dev, u16 vport,
					u32 events_mask)
{
	int in[MLX5_ST_SZ_DW(modify_nic_vport_context_in)];
	int out[MLX5_ST_SZ_DW(modify_nic_vport_context_out)];
	void *nic_vport_ctx;
	int err;

	memset(out, 0, sizeof(out));
	memset(in, 0, sizeof(in));

	MLX5_SET(modify_nic_vport_context_in, in,
		 opcode, MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT);
	MLX5_SET(modify_nic_vport_context_in, in, field_select.change_event, 1);
	MLX5_SET(modify_nic_vport_context_in, in, vport_number, vport);
	if (vport)
		MLX5_SET(modify_nic_vport_context_in, in, other_vport, 1);
	nic_vport_ctx = MLX5_ADDR_OF(modify_nic_vport_context_in,
				     in, nic_vport_context);

	MLX5_SET(nic_vport_context, nic_vport_ctx, arm_change_event, 1);

	if (events_mask & UC_ADDR_CHANGE)
		MLX5_SET(nic_vport_context, nic_vport_ctx,
			 event_on_uc_address_change, 1);
	if (events_mask & MC_ADDR_CHANGE)
		MLX5_SET(nic_vport_context, nic_vport_ctx,
			 event_on_mc_address_change, 1);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		goto ex;
	err = mlx5_cmd_status_to_err_v2(out);
	if (err)
		goto ex;
	return 0;
ex:
	return err;
}

/* HW L2 Table (MPFS) management */
static int set_l2_table_entry_cmd(struct mlx5_core_dev *dev, u32 index,
				  u8 *mac, u8 vlan_valid, u16 vlan)
{
	u32 in[MLX5_ST_SZ_DW(set_l2_table_entry_in)];
	u32 out[MLX5_ST_SZ_DW(set_l2_table_entry_out)];
	u8 *in_mac_addr;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(set_l2_table_entry_in, in, opcode,
		 MLX5_CMD_OP_SET_L2_TABLE_ENTRY);
	MLX5_SET(set_l2_table_entry_in, in, table_index, index);
	MLX5_SET(set_l2_table_entry_in, in, vlan_valid, vlan_valid);
	MLX5_SET(set_l2_table_entry_in, in, vlan, vlan);

	in_mac_addr = MLX5_ADDR_OF(set_l2_table_entry_in, in, mac_address);
	ether_addr_copy(&in_mac_addr[2], mac);

	return mlx5_cmd_exec_check_status(dev, in, sizeof(in),
					  out, sizeof(out));
}

static int del_l2_table_entry_cmd(struct mlx5_core_dev *dev, u32 index)
{
	u32 in[MLX5_ST_SZ_DW(delete_l2_table_entry_in)];
	u32 out[MLX5_ST_SZ_DW(delete_l2_table_entry_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(delete_l2_table_entry_in, in, opcode,
		 MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY);
	MLX5_SET(delete_l2_table_entry_in, in, table_index, index);
	return mlx5_cmd_exec_check_status(dev, in, sizeof(in),
					  out, sizeof(out));
}

static int alloc_l2_table_index(struct mlx5_l2_table *l2_table, u32 *ix)
{
	int err = 0;

	*ix = find_first_zero_bit(l2_table->bitmap, l2_table->size);
	if (*ix >= l2_table->size)
		err = -ENOSPC;
	else
		__set_bit(*ix, l2_table->bitmap);

	return err;
}

static void free_l2_table_index(struct mlx5_l2_table *l2_table, u32 ix)
{
	__clear_bit(ix, l2_table->bitmap);
}

static int set_l2_table_entry(struct mlx5_core_dev *dev, u8 *mac,
			      u8 vlan_valid, u16 vlan,
			      u32 *index)
{
	struct mlx5_l2_table *l2_table = &dev->priv.eswitch->l2_table;
	int err;

	err = alloc_l2_table_index(l2_table, index);
	if (err)
		return err;

	err = set_l2_table_entry_cmd(dev, *index, mac, vlan_valid, vlan);
	if (err)
		free_l2_table_index(l2_table, *index);

	return err;
}

static void del_l2_table_entry(struct mlx5_core_dev *dev, u32 index)
{
	struct mlx5_l2_table *l2_table = &dev->priv.eswitch->l2_table;

	del_l2_table_entry_cmd(dev, index);
	free_l2_table_index(l2_table, index);
}

/* E-Switch FDB flow steering */
struct dest_node {
	struct list_head list;
	struct mlx5_flow_destination dest;
};

static int _mlx5_flow_rule_apply(struct mlx5_flow_rule *fr)
{
	bool was_valid = fr->valid;
	struct dest_node *dest_n;
	u32 dest_list_size = 0;
	void *in_match_value;
	u32 *flow_context;
	u32 flow_index;
	int err;
	int i;

	if (list_empty(&fr->dest_list)) {
		if (fr->valid)
			mlx5_del_flow_table_entry(fr->ft, fr->fi);
		fr->valid = false;
		return 0;
	}

	list_for_each_entry(dest_n, &fr->dest_list, list)
		dest_list_size++;

	flow_context = mlx5_vzalloc(MLX5_ST_SZ_BYTES(flow_context) +
				    MLX5_ST_SZ_BYTES(dest_format_struct) *
				    dest_list_size);
	if (!flow_context)
		return -ENOMEM;

	MLX5_SET(flow_context, flow_context, flow_tag, fr->flow_tag);
	MLX5_SET(flow_context, flow_context, action, fr->action);
	MLX5_SET(flow_context, flow_context, destination_list_size,
		 dest_list_size);

	i = 0;
	list_for_each_entry(dest_n, &fr->dest_list, list) {
		void *dest_addr = MLX5_ADDR_OF(flow_context, flow_context,
					       destination[i++]);

		MLX5_SET(dest_format_struct, dest_addr, destination_type,
			 dest_n->dest.type);
		MLX5_SET(dest_format_struct, dest_addr, destination_id,
			 dest_n->dest.vport_num);
	}

	in_match_value = MLX5_ADDR_OF(flow_context, flow_context, match_value);
	memcpy(in_match_value, fr->match_value, MLX5_ST_SZ_BYTES(fte_match_param));

	err = mlx5_add_flow_table_entry(fr->ft, fr->match_criteria_enable,
					fr->match_criteria, flow_context,
					&flow_index);
	if (!err) {
		if (was_valid)
			mlx5_del_flow_table_entry(fr->ft, fr->fi);
		fr->fi = flow_index;
		fr->valid = true;
	}
	kfree(flow_context);
	return err;
}

static int mlx5_flow_rule_add_dest(struct mlx5_flow_rule *fr,
				   struct mlx5_flow_destination *new_dest)
{
	struct dest_node *dest_n;
	int err;

	dest_n = kzalloc(sizeof(*dest_n), GFP_KERNEL);
	if (!dest_n)
		return -ENOMEM;

	memcpy(&dest_n->dest, new_dest, sizeof(dest_n->dest));
	mutex_lock(&fr->mutex);
	list_add(&dest_n->list, &fr->dest_list);
	err = _mlx5_flow_rule_apply(fr);
	if (err) {
		list_del(&dest_n->list);
		kfree(dest_n);
	}
	mutex_unlock(&fr->mutex);
	return err;
}

static int mlx5_flow_rule_del_dest(struct mlx5_flow_rule *fr,
				   struct mlx5_flow_destination *dest)
{
	struct dest_node *dest_n;
	struct dest_node *n;
	int err;

	mutex_lock(&fr->mutex);
	list_for_each_entry_safe(dest_n, n, &fr->dest_list, list) {
		if (dest->vport_num == dest_n->dest.vport_num)
			goto found;
	}
	mutex_unlock(&fr->mutex);
	return -ENOENT;

found:
	list_del(&dest_n->list);
	err = _mlx5_flow_rule_apply(fr);
	mutex_unlock(&fr->mutex);
	kfree(dest_n);

	return err;
}

static struct mlx5_flow_rule *find_fr(struct mlx5_eswitch *esw,
				      u8 match_criteria_enable,
				      u32 *match_value)
{
	struct hlist_head *hash = esw->mc_table;
	struct esw_mc_addr *esw_mc;
	u8 *dmac_v;

	dmac_v = MLX5_ADDR_OF(fte_match_param, match_value,
			      outer_headers.dmac_47_16);

	/* UNICAST FULL MATCH */
	if (!is_multicast_ether_addr(dmac_v))
		return NULL;

	/* MULTICAST FULL MATCH */
	esw_mc = l2addr_hash_find(hash, dmac_v, struct esw_mc_addr);

	return esw_mc ? esw_mc->uplink_rule : NULL;
}

static struct mlx5_flow_rule *alloc_fr(void *ft,
				       u8 match_criteria_enable,
				       u32 *match_criteria,
				       u32 *match_value,
				       u32 action,
				       u32 flow_tag)
{
	struct mlx5_flow_rule *fr = kzalloc(sizeof(*fr), GFP_KERNEL);

	if (!fr)
		return NULL;

	fr->match_criteria = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	fr->match_value = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!fr->match_criteria || !fr->match_value) {
		kfree(fr->match_criteria);
		kfree(fr->match_value);
		kfree(fr);
		return NULL;
	}

	memcpy(fr->match_criteria, match_criteria, MLX5_ST_SZ_BYTES(fte_match_param));
	memcpy(fr->match_value, match_value, MLX5_ST_SZ_BYTES(fte_match_param));
	fr->match_criteria_enable = match_criteria_enable;
	fr->flow_tag = flow_tag;
	fr->action = action;

	mutex_init(&fr->mutex);
	INIT_LIST_HEAD(&fr->dest_list);
	atomic_set(&fr->refcount, 0);
	fr->ft = ft;
	return fr;
}

static void deref_fr(struct mlx5_flow_rule *fr)
{
	if (!atomic_dec_and_test(&fr->refcount))
		return;

	kfree(fr->match_criteria);
	kfree(fr->match_value);
	kfree(fr);
}

static struct mlx5_flow_rule *
mlx5_add_flow_rule(struct mlx5_eswitch *esw,
		   u8 match_criteria_enable,
		   u32 *match_criteria,
		   u32 *match_value,
		   u32 action,
		   u32 flow_tag,
		   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_rule *fr;
	int err;

	fr = find_fr(esw, match_criteria_enable, match_value);
	fr = fr ? fr : alloc_fr(esw->fdb_table.fdb, match_criteria_enable, match_criteria,
				match_value, action, flow_tag);
	if (!fr)
		return NULL;

	atomic_inc(&fr->refcount);

	err = mlx5_flow_rule_add_dest(fr, dest);
	if (err) {
		deref_fr(fr);
		return NULL;
	}

	return fr;
}

static void mlx5_del_flow_rule(struct mlx5_flow_rule *fr, u32 vport)
{
	struct mlx5_flow_destination dest;

	dest.vport_num = vport;
	mlx5_flow_rule_del_dest(fr, &dest);
	deref_fr(fr);
}

/* E-Switch FDB */
static struct mlx5_flow_rule *
esw_fdb_set_vport_rule(struct mlx5_eswitch *esw, u8 mac[ETH_ALEN], u32 vport)
{
	int match_header = MLX5_MATCH_OUTER_HEADERS;
	struct mlx5_flow_destination dest;
	struct mlx5_flow_rule *flow_rule = NULL;
	u32 *match_v;
	u32 *match_c;
	u8 *dmac_v;
	u8 *dmac_c;

	match_v = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	match_c = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!match_v || !match_c) {
		pr_warn("FDB: Failed to alloc match parameters\n");
		goto out;
	}
	dmac_v = MLX5_ADDR_OF(fte_match_param, match_v,
			      outer_headers.dmac_47_16);
	dmac_c = MLX5_ADDR_OF(fte_match_param, match_c,
			      outer_headers.dmac_47_16);

	ether_addr_copy(dmac_v, mac);
	/* Match criteria mask */
	memset(dmac_c, 0xff, 6);

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = vport;

	esw_debug(esw->dev,
		  "\tFDB add rule dmac_v(%pM) dmac_c(%pM) -> vport(%d)\n",
		  dmac_v, dmac_c, vport);
	flow_rule =
		mlx5_add_flow_rule(esw,
				   match_header,
				   match_c,
				   match_v,
				   MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				   0, &dest);
	if (IS_ERR_OR_NULL(flow_rule)) {
		pr_warn(
			"FDB: Failed to add flow rule: dmac_v(%pM) dmac_c(%pM) -> vport(%d), err(%ld)\n",
			 dmac_v, dmac_c, vport, PTR_ERR(flow_rule));
		flow_rule = NULL;
	}
out:
	kfree(match_v);
	kfree(match_c);
	return flow_rule;
}

static int esw_create_fdb_table(struct mlx5_eswitch *esw, int nvports)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_table_group g;
	struct mlx5_flow_table *fdb;
	u8 *dmac;

	esw_debug(dev, "Create FDB log_max_size(%d)\n",
		  MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));

	memset(&g, 0, sizeof(g));
	/* UC MC Full match rules*/
	g.log_sz = MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size);
	g.match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g.match_criteria,
			    outer_headers.dmac_47_16);
	/* Match criteria mask */
	memset(dmac, 0xff, 6);

	fdb = mlx5_create_flow_table(dev, 0,
				     MLX5_FLOW_TABLE_TYPE_ESWITCH,
				     1, &g);
	if (fdb)
		esw_debug(dev, "ESW: FDB Table created fdb->id %d\n", mlx5_get_flow_table_id(fdb));
	else
		esw_warn(dev, "ESW: Failed to create FDB Table\n");

	esw->fdb_table.fdb = fdb;
	return fdb ? 0 : -ENOMEM;
}

static void esw_destroy_fdb_table(struct mlx5_eswitch *esw)
{
	if (!esw->fdb_table.fdb)
		return;

	esw_debug(esw->dev, "Destroy FDB Table fdb(%d)\n",
		  mlx5_get_flow_table_id(esw->fdb_table.fdb));
	mlx5_destroy_flow_table(esw->fdb_table.fdb);
	esw->fdb_table.fdb = NULL;
}

/* E-Switch vport UC/MC lists management */
typedef int (*vport_addr_action)(struct mlx5_eswitch *esw,
				 struct vport_addr *vaddr);

static int esw_add_uc_addr(struct mlx5_eswitch *esw, struct vport_addr *vaddr)
{
	struct hlist_head *hash = esw->l2_table.l2_hash;
	struct esw_uc_addr *esw_uc;
	u8 *mac = vaddr->node.addr;
	u32 vport = vaddr->vport;
	int err;

	esw_uc = l2addr_hash_find(hash, mac, struct esw_uc_addr);
	if (esw_uc) {
		esw_warn(esw->dev,
			 "Failed to set L2 mac(%pM) for vport(%d), mac is already in use by vport(%d)\n",
			 mac, vport, esw_uc->vport);
		return -EEXIST;
	}

	esw_uc = l2addr_hash_add(hash, mac, struct esw_uc_addr, GFP_KERNEL);
	if (!esw_uc)
		return -ENOMEM;
	esw_uc->vport = vport;

	err = set_l2_table_entry(esw->dev, mac, 0, 0, &esw_uc->table_index);
	if (err)
		goto abort;

	if (esw->fdb_table.fdb) /* SRIOV is enabled: Forward UC MAC to vport */
		vaddr->flow_rule = esw_fdb_set_vport_rule(esw, mac, vport);

	esw_debug(esw->dev, "\tADDED UC MAC: vport[%d] %pM index:%d fr(%p)\n",
		  vport, mac, esw_uc->table_index, vaddr->flow_rule);
	return err;
abort:
	l2addr_hash_del(esw_uc);
	return err;
}

static int esw_del_uc_addr(struct mlx5_eswitch *esw, struct vport_addr *vaddr)
{
	struct hlist_head *hash = esw->l2_table.l2_hash;
	struct esw_uc_addr *esw_uc;
	u8 *mac = vaddr->node.addr;
	u32 vport = vaddr->vport;

	esw_uc = l2addr_hash_find(hash, mac, struct esw_uc_addr);
	if (!esw_uc || esw_uc->vport != vport) {
		esw_debug(esw->dev,
			  "MAC(%pM) doesn't belong to vport (%d)\n",
			  mac, vport);
		return -EINVAL;
	}
	esw_debug(esw->dev, "\tDELETE UC MAC: vport[%d] %pM index:%d fr(%p)\n",
		  vport, mac, esw_uc->table_index, vaddr->flow_rule);

	del_l2_table_entry(esw->dev, esw_uc->table_index);

	if (vaddr->flow_rule)
		mlx5_del_flow_rule(vaddr->flow_rule, vport);
	vaddr->flow_rule = NULL;

	l2addr_hash_del(esw_uc);
	return 0;
}

static int esw_add_mc_addr(struct mlx5_eswitch *esw, struct vport_addr *vaddr)
{
	struct hlist_head *hash = esw->mc_table;
	struct esw_mc_addr *esw_mc;
	u8 *mac = vaddr->node.addr;
	u32 vport = vaddr->vport;

	if (!esw->fdb_table.fdb)
		return 0;

	esw_mc = l2addr_hash_find(hash, mac, struct esw_mc_addr);
	if (esw_mc)
		goto add;

	esw_mc = l2addr_hash_add(hash, mac, struct esw_mc_addr, GFP_KERNEL);
	if (!esw_mc)
		return -ENOMEM;

	esw_mc->uplink_rule = /* Forward MC MAC to Uplink */
		esw_fdb_set_vport_rule(esw, mac, UPLINK_VPORT);
add:
	esw_mc->refcnt++;
	/* Forward MC MAC to vport */
	vaddr->flow_rule = esw_fdb_set_vport_rule(esw, mac, vport);
	esw_debug(esw->dev,
		  "\tADDED MC MAC: vport[%d] %pM fr(%p) refcnt(%d) uplinkfr(%p)\n",
		  vport, mac, vaddr->flow_rule,
		  esw_mc->refcnt, esw_mc->uplink_rule);
	return 0;
}

static int esw_del_mc_addr(struct mlx5_eswitch *esw, struct vport_addr *vaddr)
{
	struct hlist_head *hash = esw->mc_table;
	struct esw_mc_addr *esw_mc;
	u8 *mac = vaddr->node.addr;
	u32 vport = vaddr->vport;

	if (!esw->fdb_table.fdb)
		return 0;

	esw_mc = l2addr_hash_find(hash, mac, struct esw_mc_addr);
	if (!esw_mc) {
		esw_warn(esw->dev,
			 "Failed to find eswitch MC addr for MAC(%pM) vport(%d)",
			 mac, vport);
		return -EINVAL;
	}
	esw_debug(esw->dev,
		  "\tDELETE MC MAC: vport[%d] %pM fr(%p) refcnt(%d) uplinkfr(%p)\n",
		  vport, mac, vaddr->flow_rule, esw_mc->refcnt,
		  esw_mc->uplink_rule);

	if (vaddr->flow_rule)
		mlx5_del_flow_rule(vaddr->flow_rule, vport);
	vaddr->flow_rule = NULL;

	if (--esw_mc->refcnt)
		return 0;

	if (esw_mc->uplink_rule)
		mlx5_del_flow_rule(esw_mc->uplink_rule, UPLINK_VPORT);

	l2addr_hash_del(esw_mc);
	return 0;
}

/* Apply vport UC/MC list to HW l2 table and FDB table */
static void esw_apply_vport_addr_list(struct mlx5_eswitch *esw,
				      u32 vport_num, int list_type)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	bool is_uc = list_type == MLX5_NVPRT_LIST_TYPE_UC;
	vport_addr_action vport_addr_add;
	vport_addr_action vport_addr_del;
	struct vport_addr *addr;
	struct l2addr_node *node;
	struct hlist_head *hash;
	struct hlist_node *tmp;
	int hi;

	vport_addr_add = is_uc ? esw_add_uc_addr :
				 esw_add_mc_addr;
	vport_addr_del = is_uc ? esw_del_uc_addr :
				 esw_del_mc_addr;

	hash = is_uc ? vport->uc_list : vport->mc_list;
	for_each_l2hash_node(node, tmp, hash, hi) {
		addr = container_of(node, struct vport_addr, node);
		switch (addr->action) {
		case MLX5_ACTION_ADD:
			vport_addr_add(esw, addr);
			addr->action = MLX5_ACTION_NONE;
			break;
		case MLX5_ACTION_DEL:
			vport_addr_del(esw, addr);
			l2addr_hash_del(addr);
			break;
		}
	}
}

/* Sync vport UC/MC list from vport context */
static void esw_update_vport_addr_list(struct mlx5_eswitch *esw,
				       u32 vport_num, int list_type)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	bool is_uc = list_type == MLX5_NVPRT_LIST_TYPE_UC;
	u8 (*mac_list)[ETH_ALEN];
	struct l2addr_node *node;
	struct vport_addr *addr;
	struct hlist_head *hash;
	struct hlist_node *tmp;
	int size;
	int err;
	int hi;
	int i;

	size = is_uc ? MLX5_MAX_UC_PER_VPORT(esw->dev) :
		       MLX5_MAX_MC_PER_VPORT(esw->dev);

	mac_list = kcalloc(size, ETH_ALEN, GFP_KERNEL);
	if (!mac_list)
		return;

	hash = is_uc ? vport->uc_list : vport->mc_list;

	for_each_l2hash_node(node, tmp, hash, hi) {
		addr = container_of(node, struct vport_addr, node);
		addr->action = MLX5_ACTION_DEL;
	}

	err = mlx5_query_nic_vport_mac_list(esw->dev, vport_num, list_type,
					    mac_list, &size);
	if (err)
		return;
	esw_debug(esw->dev, "vport[%d] context update %s list size (%d)\n",
		  vport_num, is_uc ? "UC" : "MC", size);

	for (i = 0; i < size; i++) {
		if (is_uc && !is_valid_ether_addr(mac_list[i]))
			continue;

		if (!is_uc && !is_multicast_ether_addr(mac_list[i]))
			continue;

		addr = l2addr_hash_find(hash, mac_list[i], struct vport_addr);
		if (addr) {
			addr->action = MLX5_ACTION_NONE;
			continue;
		}

		addr = l2addr_hash_add(hash, mac_list[i], struct vport_addr,
				       GFP_KERNEL);
		if (!addr) {
			esw_warn(esw->dev,
				 "Failed to add MAC(%pM) to vport[%d] DB\n",
				 mac_list[i], vport_num);
			continue;
		}
		addr->vport = vport_num;
		addr->action = MLX5_ACTION_ADD;
	}
	kfree(mac_list);
}

static void esw_vport_change_handler(struct work_struct *work)
{
	struct mlx5_vport *vport =
		container_of(work, struct mlx5_vport, vport_change_handler);
	struct mlx5_core_dev *dev = vport->dev;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	u8 mac[ETH_ALEN];

	mlx5_query_nic_vport_mac_address(dev, vport->vport, mac);
	esw_debug(dev, "vport[%d] Context Changed: perm mac: %pM\n",
		  vport->vport, mac);

	if (vport->enabled_events & UC_ADDR_CHANGE) {
		esw_update_vport_addr_list(esw, vport->vport,
					   MLX5_NVPRT_LIST_TYPE_UC);
		esw_apply_vport_addr_list(esw, vport->vport,
					  MLX5_NVPRT_LIST_TYPE_UC);
	}

	if (vport->enabled_events & MC_ADDR_CHANGE) {
		esw_update_vport_addr_list(esw, vport->vport,
					   MLX5_NVPRT_LIST_TYPE_MC);
		esw_apply_vport_addr_list(esw, vport->vport,
					  MLX5_NVPRT_LIST_TYPE_MC);
	}

	esw_debug(esw->dev, "vport[%d] Context Changed: Done\n", vport->vport);
	if (vport->enabled)
		arm_vport_context_events_cmd(dev, vport->vport,
					     vport->enabled_events);
}

static void esw_enable_vport(struct mlx5_eswitch *esw, int vport_num,
			     int enable_events)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	unsigned long flags;

	WARN_ON(vport->enabled);

	esw_debug(esw->dev, "Enabling VPORT(%d)\n", vport_num);
	mlx5_modify_vport_admin_state(esw->dev,
				      MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
				      vport_num,
				      MLX5_ESW_VPORT_ADMIN_STATE_AUTO);

	/* Sync with current vport context */
	vport->enabled_events = enable_events;
	esw_vport_change_handler(&vport->vport_change_handler);

	spin_lock_irqsave(&vport->lock, flags);
	vport->enabled = true;
	spin_unlock_irqrestore(&vport->lock, flags);

	arm_vport_context_events_cmd(esw->dev, vport_num, enable_events);

	esw->enabled_vports++;
	esw_debug(esw->dev, "Enabled VPORT(%d)\n", vport_num);
}

static void esw_cleanup_vport(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	struct l2addr_node *node;
	struct vport_addr *addr;
	struct hlist_node *tmp;
	int hi;

	for_each_l2hash_node(node, tmp, vport->uc_list, hi) {
		addr = container_of(node, struct vport_addr, node);
		addr->action = MLX5_ACTION_DEL;
	}
	esw_apply_vport_addr_list(esw, vport_num, MLX5_NVPRT_LIST_TYPE_UC);

	for_each_l2hash_node(node, tmp, vport->mc_list, hi) {
		addr = container_of(node, struct vport_addr, node);
		addr->action = MLX5_ACTION_DEL;
	}
	esw_apply_vport_addr_list(esw, vport_num, MLX5_NVPRT_LIST_TYPE_MC);
}

static void esw_disable_vport(struct mlx5_eswitch *esw, int vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	unsigned long flags;

	if (!vport->enabled)
		return;

	esw_debug(esw->dev, "Disabling vport(%d)\n", vport_num);
	/* Mark this vport as disabled to discard new events */
	spin_lock_irqsave(&vport->lock, flags);
	vport->enabled = false;
	vport->enabled_events = 0;
	spin_unlock_irqrestore(&vport->lock, flags);

	mlx5_modify_vport_admin_state(esw->dev,
				      MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
				      vport_num,
				      MLX5_ESW_VPORT_ADMIN_STATE_DOWN);
	/* Wait for current already scheduled events to complete */
	flush_workqueue(esw->work_queue);
	/* Disable events from this vport */
	arm_vport_context_events_cmd(esw->dev, vport->vport, 0);
	/* We don't assume VFs will cleanup after themselves */
	esw_cleanup_vport(esw, vport_num);
	esw->enabled_vports--;
}

/* Public E-Switch API */
int mlx5_eswitch_enable_sriov(struct mlx5_eswitch *esw, int nvfs)
{
	int err;
	int i;

	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return 0;

	if (!MLX5_CAP_GEN(esw->dev, eswitch_flow_table) ||
	    !MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, ft_support)) {
		esw_warn(esw->dev, "E-Switch FDB is not supported, aborting ...\n");
		return -ENOTSUPP;
	}

	esw_info(esw->dev, "E-Switch enable SRIOV: nvfs(%d)\n", nvfs);

	esw_disable_vport(esw, 0);

	err = esw_create_fdb_table(esw, nvfs + 1);
	if (err)
		goto abort;

	for (i = 0; i <= nvfs; i++)
		esw_enable_vport(esw, i, SRIOV_VPORT_EVENTS);

	esw_info(esw->dev, "SRIOV enabled: active vports(%d)\n",
		 esw->enabled_vports);
	return 0;

abort:
	esw_enable_vport(esw, 0, UC_ADDR_CHANGE);
	return err;
}

void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw)
{
	int i;

	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return;

	esw_info(esw->dev, "disable SRIOV: active vports(%d)\n",
		 esw->enabled_vports);

	for (i = 0; i < esw->total_vports; i++)
		esw_disable_vport(esw, i);

	esw_destroy_fdb_table(esw);

	/* VPORT 0 (PF) must be enabled back with non-sriov configuration */
	esw_enable_vport(esw, 0, UC_ADDR_CHANGE);
}

int mlx5_eswitch_init(struct mlx5_core_dev *dev)
{
	int l2_table_size = 1 << MLX5_CAP_GEN(dev, log_max_l2_table);
	int total_vports = 1 + pci_sriov_get_totalvfs(dev->pdev);
	struct mlx5_eswitch *esw;
	int vport_num;
	int err;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    MLX5_CAP_GEN(dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return 0;

	esw_info(dev,
		 "Total vports %d, l2 table size(%d), per vport: max uc(%d) max mc(%d)\n",
		 total_vports, l2_table_size,
		 MLX5_MAX_UC_PER_VPORT(dev),
		 MLX5_MAX_MC_PER_VPORT(dev));

	esw = kzalloc(sizeof(*esw), GFP_KERNEL);
	if (!esw)
		return -ENOMEM;

	esw->dev = dev;

	esw->l2_table.bitmap = kcalloc(BITS_TO_LONGS(l2_table_size),
				   sizeof(uintptr_t), GFP_KERNEL);
	if (!esw->l2_table.bitmap) {
		err = -ENOMEM;
		goto abort;
	}
	esw->l2_table.size = l2_table_size;

	esw->work_queue = create_singlethread_workqueue("mlx5_esw_wq");
	if (!esw->work_queue) {
		err = -ENOMEM;
		goto abort;
	}

	esw->vports = kcalloc(total_vports, sizeof(struct mlx5_vport),
			      GFP_KERNEL);
	if (!esw->vports) {
		err = -ENOMEM;
		goto abort;
	}

	for (vport_num = 0; vport_num < total_vports; vport_num++) {
		struct mlx5_vport *vport = &esw->vports[vport_num];

		vport->vport = vport_num;
		vport->dev = dev;
		INIT_WORK(&vport->vport_change_handler,
			  esw_vport_change_handler);
		spin_lock_init(&vport->lock);
	}

	esw->total_vports = total_vports;
	esw->enabled_vports = 0;

	dev->priv.eswitch = esw;
	esw_enable_vport(esw, 0, UC_ADDR_CHANGE);
	/* VF Vports will be enabled when SRIOV is enabled */
	return 0;
abort:
	if (esw->work_queue)
		destroy_workqueue(esw->work_queue);
	kfree(esw->l2_table.bitmap);
	kfree(esw->vports);
	kfree(esw);
	return err;
}

void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw)
{
	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return;

	esw_info(esw->dev, "cleanup\n");
	esw_disable_vport(esw, 0);

	esw->dev->priv.eswitch = NULL;
	destroy_workqueue(esw->work_queue);
	kfree(esw->l2_table.bitmap);
	kfree(esw->vports);
	kfree(esw);
}

void mlx5_eswitch_vport_event(struct mlx5_eswitch *esw, struct mlx5_eqe *eqe)
{
	struct mlx5_eqe_vport_change *vc_eqe = &eqe->data.vport_change;
	u16 vport_num = be16_to_cpu(vc_eqe->vport_num);
	struct mlx5_vport *vport;

	if (!esw) {
		pr_warn("MLX5 E-Switch: vport %d got an event while eswitch is not initialized\n",
			vport_num);
		return;
	}

	vport = &esw->vports[vport_num];
	spin_lock(&vport->lock);
	if (vport->enabled)
		queue_work(esw->work_queue, &vport->vport_change_handler);
	spin_unlock(&vport->lock);
}

/* Vport Administration */
#define ESW_ALLOWED(esw) \
	(esw && MLX5_CAP_GEN(esw->dev, vport_group_manager) && mlx5_core_is_pf(esw->dev))
#define LEGAL_VPORT(esw, vport) (vport >= 0 && vport < esw->total_vports)

int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       int vport, u8 mac[ETH_ALEN])
{
	int err = 0;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	err = mlx5_modify_nic_vport_mac_address(esw->dev, vport, mac);
	if (err) {
		mlx5_core_warn(esw->dev,
			       "Failed to mlx5_modify_nic_vport_mac vport(%d) err=(%d)\n",
			       vport, err);
		return err;
	}

	return err;
}

int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 int vport, int link_state)
{
	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	return mlx5_modify_vport_admin_state(esw->dev,
					     MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
					     vport, link_state);
}

int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  int vport, struct ifla_vf_info *ivi)
{
	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	memset(ivi, 0, sizeof(*ivi));
	ivi->vf = vport - 1;

	mlx5_query_nic_vport_mac_address(esw->dev, vport, ivi->mac);
	ivi->linkstate = mlx5_query_vport_admin_state(esw->dev,
						      MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
						      vport);
	ivi->vlan = 0;
	ivi->qos = 0;
	ivi->spoofchk = 0;

	return 0;
}
