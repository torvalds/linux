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
#include <linux/mlx5/fs.h>
#include "mlx5_core.h"
#include "eswitch.h"

#define UPLINK_VPORT 0xFFFF

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
	struct mlx5_flow_handle *uplink_rule; /* Forward to uplink rule */
	u32                    refcnt;
};

/* Vport UC/MC hash node */
struct vport_addr {
	struct l2addr_node     node;
	u8                     action;
	u32                    vport;
	struct mlx5_flow_handle *flow_rule; /* SRIOV only */
	/* A flag indicating that mac was added due to mc promiscuous vport */
	bool mc_promisc;
};

enum {
	UC_ADDR_CHANGE = BIT(0),
	MC_ADDR_CHANGE = BIT(1),
	PROMISC_CHANGE = BIT(3),
};

/* Vport context events */
#define SRIOV_VPORT_EVENTS (UC_ADDR_CHANGE | \
			    MC_ADDR_CHANGE | \
			    PROMISC_CHANGE)

static int arm_vport_context_events_cmd(struct mlx5_core_dev *dev, u16 vport,
					u32 events_mask)
{
	int in[MLX5_ST_SZ_DW(modify_nic_vport_context_in)]   = {0};
	int out[MLX5_ST_SZ_DW(modify_nic_vport_context_out)] = {0};
	void *nic_vport_ctx;

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
	if (events_mask & PROMISC_CHANGE)
		MLX5_SET(nic_vport_context, nic_vport_ctx,
			 event_on_promisc_change, 1);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/* E-Switch vport context HW commands */
static int modify_esw_vport_context_cmd(struct mlx5_core_dev *dev, u16 vport,
					void *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_esw_vport_context_out)] = {0};

	MLX5_SET(modify_esw_vport_context_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_ESW_VPORT_CONTEXT);
	MLX5_SET(modify_esw_vport_context_in, in, vport_number, vport);
	if (vport)
		MLX5_SET(modify_esw_vport_context_in, in, other_vport, 1);
	return mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
}

static int modify_esw_vport_cvlan(struct mlx5_core_dev *dev, u32 vport,
				  u16 vlan, u8 qos, u8 set_flags)
{
	u32 in[MLX5_ST_SZ_DW(modify_esw_vport_context_in)] = {0};

	if (!MLX5_CAP_ESW(dev, vport_cvlan_strip) ||
	    !MLX5_CAP_ESW(dev, vport_cvlan_insert_if_not_exist))
		return -ENOTSUPP;

	esw_debug(dev, "Set Vport[%d] VLAN %d qos %d set=%x\n",
		  vport, vlan, qos, set_flags);

	if (set_flags & SET_VLAN_STRIP)
		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.vport_cvlan_strip, 1);

	if (set_flags & SET_VLAN_INSERT) {
		/* insert only if no vlan in packet */
		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.vport_cvlan_insert, 1);

		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.cvlan_pcp, qos);
		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.cvlan_id, vlan);
	}

	MLX5_SET(modify_esw_vport_context_in, in,
		 field_select.vport_cvlan_strip, 1);
	MLX5_SET(modify_esw_vport_context_in, in,
		 field_select.vport_cvlan_insert, 1);

	return modify_esw_vport_context_cmd(dev, vport, in, sizeof(in));
}

/* HW L2 Table (MPFS) management */
static int set_l2_table_entry_cmd(struct mlx5_core_dev *dev, u32 index,
				  u8 *mac, u8 vlan_valid, u16 vlan)
{
	u32 in[MLX5_ST_SZ_DW(set_l2_table_entry_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(set_l2_table_entry_out)] = {0};
	u8 *in_mac_addr;

	MLX5_SET(set_l2_table_entry_in, in, opcode,
		 MLX5_CMD_OP_SET_L2_TABLE_ENTRY);
	MLX5_SET(set_l2_table_entry_in, in, table_index, index);
	MLX5_SET(set_l2_table_entry_in, in, vlan_valid, vlan_valid);
	MLX5_SET(set_l2_table_entry_in, in, vlan, vlan);

	in_mac_addr = MLX5_ADDR_OF(set_l2_table_entry_in, in, mac_address);
	ether_addr_copy(&in_mac_addr[2], mac);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int del_l2_table_entry_cmd(struct mlx5_core_dev *dev, u32 index)
{
	u32 in[MLX5_ST_SZ_DW(delete_l2_table_entry_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(delete_l2_table_entry_out)] = {0};

	MLX5_SET(delete_l2_table_entry_in, in, opcode,
		 MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY);
	MLX5_SET(delete_l2_table_entry_in, in, table_index, index);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
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

/* E-Switch FDB */
static struct mlx5_flow_handle *
__esw_fdb_set_vport_rule(struct mlx5_eswitch *esw, u32 vport, bool rx_rule,
			 u8 mac_c[ETH_ALEN], u8 mac_v[ETH_ALEN])
{
	int match_header = (is_zero_ether_addr(mac_c) ? 0 :
			    MLX5_MATCH_OUTER_HEADERS);
	struct mlx5_flow_handle *flow_rule = NULL;
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_destination dest;
	struct mlx5_flow_spec *spec;
	void *mv_misc = NULL;
	void *mc_misc = NULL;
	u8 *dmac_v = NULL;
	u8 *dmac_c = NULL;

	if (rx_rule)
		match_header |= MLX5_MATCH_MISC_PARAMETERS;

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec) {
		esw_warn(esw->dev, "FDB: Failed to alloc match parameters\n");
		return NULL;
	}
	dmac_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			      outer_headers.dmac_47_16);
	dmac_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			      outer_headers.dmac_47_16);

	if (match_header & MLX5_MATCH_OUTER_HEADERS) {
		ether_addr_copy(dmac_v, mac_v);
		ether_addr_copy(dmac_c, mac_c);
	}

	if (match_header & MLX5_MATCH_MISC_PARAMETERS) {
		mv_misc  = MLX5_ADDR_OF(fte_match_param, spec->match_value,
					misc_parameters);
		mc_misc  = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
					misc_parameters);
		MLX5_SET(fte_match_set_misc, mv_misc, source_port, UPLINK_VPORT);
		MLX5_SET_TO_ONES(fte_match_set_misc, mc_misc, source_port);
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = vport;

	esw_debug(esw->dev,
		  "\tFDB add rule dmac_v(%pM) dmac_c(%pM) -> vport(%d)\n",
		  dmac_v, dmac_c, vport);
	spec->match_criteria_enable = match_header;
	flow_act.action =  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule =
		mlx5_add_flow_rules(esw->fdb_table.fdb, spec,
				    &flow_act, &dest, 1);
	if (IS_ERR(flow_rule)) {
		esw_warn(esw->dev,
			 "FDB: Failed to add flow rule: dmac_v(%pM) dmac_c(%pM) -> vport(%d), err(%ld)\n",
			 dmac_v, dmac_c, vport, PTR_ERR(flow_rule));
		flow_rule = NULL;
	}

	kvfree(spec);
	return flow_rule;
}

static struct mlx5_flow_handle *
esw_fdb_set_vport_rule(struct mlx5_eswitch *esw, u8 mac[ETH_ALEN], u32 vport)
{
	u8 mac_c[ETH_ALEN];

	eth_broadcast_addr(mac_c);
	return __esw_fdb_set_vport_rule(esw, vport, false, mac_c, mac);
}

static struct mlx5_flow_handle *
esw_fdb_set_vport_allmulti_rule(struct mlx5_eswitch *esw, u32 vport)
{
	u8 mac_c[ETH_ALEN];
	u8 mac_v[ETH_ALEN];

	eth_zero_addr(mac_c);
	eth_zero_addr(mac_v);
	mac_c[0] = 0x01;
	mac_v[0] = 0x01;
	return __esw_fdb_set_vport_rule(esw, vport, false, mac_c, mac_v);
}

static struct mlx5_flow_handle *
esw_fdb_set_vport_promisc_rule(struct mlx5_eswitch *esw, u32 vport)
{
	u8 mac_c[ETH_ALEN];
	u8 mac_v[ETH_ALEN];

	eth_zero_addr(mac_c);
	eth_zero_addr(mac_v);
	return __esw_fdb_set_vport_rule(esw, vport, true, mac_c, mac_v);
}

static int esw_create_legacy_fdb_table(struct mlx5_eswitch *esw, int nvports)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb;
	struct mlx5_flow_group *g;
	void *match_criteria;
	int table_size;
	u32 *flow_group_in;
	u8 *dmac;
	int err = 0;

	esw_debug(dev, "Create FDB log_max_size(%d)\n",
		  MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		return -ENOMEM;
	}

	flow_group_in = mlx5_vzalloc(inlen);
	if (!flow_group_in)
		return -ENOMEM;
	memset(flow_group_in, 0, inlen);

	table_size = BIT(MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));
	fdb = mlx5_create_flow_table(root_ns, 0, table_size, 0, 0);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create FDB Table err %d\n", err);
		goto out;
	}
	esw->fdb_table.fdb = fdb;

	/* Addresses group : Full match unicast/multicast addresses */
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);
	dmac = MLX5_ADDR_OF(fte_match_param, match_criteria, outer_headers.dmac_47_16);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	/* Preserve 2 entries for allmulti and promisc rules*/
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, table_size - 3);
	eth_broadcast_addr(dmac);
	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create flow group err(%d)\n", err);
		goto out;
	}
	esw->fdb_table.legacy.addr_grp = g;

	/* Allmulti group : One rule that forwards any mcast traffic */
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, table_size - 2);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, table_size - 2);
	eth_zero_addr(dmac);
	dmac[0] = 0x01;
	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create allmulti flow group err(%d)\n", err);
		goto out;
	}
	esw->fdb_table.legacy.allmulti_grp = g;

	/* Promiscuous group :
	 * One rule that forward all unmatched traffic from previous groups
	 */
	eth_zero_addr(dmac);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_port);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, table_size - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, table_size - 1);
	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create promisc flow group err(%d)\n", err);
		goto out;
	}
	esw->fdb_table.legacy.promisc_grp = g;

out:
	if (err) {
		if (!IS_ERR_OR_NULL(esw->fdb_table.legacy.allmulti_grp)) {
			mlx5_destroy_flow_group(esw->fdb_table.legacy.allmulti_grp);
			esw->fdb_table.legacy.allmulti_grp = NULL;
		}
		if (!IS_ERR_OR_NULL(esw->fdb_table.legacy.addr_grp)) {
			mlx5_destroy_flow_group(esw->fdb_table.legacy.addr_grp);
			esw->fdb_table.legacy.addr_grp = NULL;
		}
		if (!IS_ERR_OR_NULL(esw->fdb_table.fdb)) {
			mlx5_destroy_flow_table(esw->fdb_table.fdb);
			esw->fdb_table.fdb = NULL;
		}
	}

	kvfree(flow_group_in);
	return err;
}

static void esw_destroy_legacy_fdb_table(struct mlx5_eswitch *esw)
{
	if (!esw->fdb_table.fdb)
		return;

	esw_debug(esw->dev, "Destroy FDB Table\n");
	mlx5_destroy_flow_group(esw->fdb_table.legacy.promisc_grp);
	mlx5_destroy_flow_group(esw->fdb_table.legacy.allmulti_grp);
	mlx5_destroy_flow_group(esw->fdb_table.legacy.addr_grp);
	mlx5_destroy_flow_table(esw->fdb_table.fdb);
	esw->fdb_table.fdb = NULL;
	esw->fdb_table.legacy.addr_grp = NULL;
	esw->fdb_table.legacy.allmulti_grp = NULL;
	esw->fdb_table.legacy.promisc_grp = NULL;
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

	/* SRIOV is enabled: Forward UC MAC to vport */
	if (esw->fdb_table.fdb && esw->mode == SRIOV_LEGACY)
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
		mlx5_del_flow_rules(vaddr->flow_rule);
	vaddr->flow_rule = NULL;

	l2addr_hash_del(esw_uc);
	return 0;
}

static void update_allmulti_vports(struct mlx5_eswitch *esw,
				   struct vport_addr *vaddr,
				   struct esw_mc_addr *esw_mc)
{
	u8 *mac = vaddr->node.addr;
	u32 vport_idx = 0;

	for (vport_idx = 0; vport_idx < esw->total_vports; vport_idx++) {
		struct mlx5_vport *vport = &esw->vports[vport_idx];
		struct hlist_head *vport_hash = vport->mc_list;
		struct vport_addr *iter_vaddr =
					l2addr_hash_find(vport_hash,
							 mac,
							 struct vport_addr);
		if (IS_ERR_OR_NULL(vport->allmulti_rule) ||
		    vaddr->vport == vport_idx)
			continue;
		switch (vaddr->action) {
		case MLX5_ACTION_ADD:
			if (iter_vaddr)
				continue;
			iter_vaddr = l2addr_hash_add(vport_hash, mac,
						     struct vport_addr,
						     GFP_KERNEL);
			if (!iter_vaddr) {
				esw_warn(esw->dev,
					 "ALL-MULTI: Failed to add MAC(%pM) to vport[%d] DB\n",
					 mac, vport_idx);
				continue;
			}
			iter_vaddr->vport = vport_idx;
			iter_vaddr->flow_rule =
					esw_fdb_set_vport_rule(esw,
							       mac,
							       vport_idx);
			iter_vaddr->mc_promisc = true;
			break;
		case MLX5_ACTION_DEL:
			if (!iter_vaddr)
				continue;
			mlx5_del_flow_rules(iter_vaddr->flow_rule);
			l2addr_hash_del(iter_vaddr);
			break;
		}
	}
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

	/* Add this multicast mac to all the mc promiscuous vports */
	update_allmulti_vports(esw, vaddr, esw_mc);

add:
	/* If the multicast mac is added as a result of mc promiscuous vport,
	 * don't increment the multicast ref count
	 */
	if (!vaddr->mc_promisc)
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
		mlx5_del_flow_rules(vaddr->flow_rule);
	vaddr->flow_rule = NULL;

	/* If the multicast mac is added as a result of mc promiscuous vport,
	 * don't decrement the multicast ref count.
	 */
	if (vaddr->mc_promisc || (--esw_mc->refcnt > 0))
		return 0;

	/* Remove this multicast mac from all the mc promiscuous vports */
	update_allmulti_vports(esw, vaddr, esw_mc);

	if (esw_mc->uplink_rule)
		mlx5_del_flow_rules(esw_mc->uplink_rule);

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

	if (!vport->enabled)
		goto out;

	err = mlx5_query_nic_vport_mac_list(esw->dev, vport_num, list_type,
					    mac_list, &size);
	if (err)
		goto out;
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
			/* If this mac was previously added because of allmulti
			 * promiscuous rx mode, its now converted to be original
			 * vport mac.
			 */
			if (addr->mc_promisc) {
				struct esw_mc_addr *esw_mc =
					l2addr_hash_find(esw->mc_table,
							 mac_list[i],
							 struct esw_mc_addr);
				if (!esw_mc) {
					esw_warn(esw->dev,
						 "Failed to MAC(%pM) in mcast DB\n",
						 mac_list[i]);
					continue;
				}
				esw_mc->refcnt++;
				addr->mc_promisc = false;
			}
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
out:
	kfree(mac_list);
}

/* Sync vport UC/MC list from vport context
 * Must be called after esw_update_vport_addr_list
 */
static void esw_update_vport_mc_promisc(struct mlx5_eswitch *esw, u32 vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	struct l2addr_node *node;
	struct vport_addr *addr;
	struct hlist_head *hash;
	struct hlist_node *tmp;
	int hi;

	hash = vport->mc_list;

	for_each_l2hash_node(node, tmp, esw->mc_table, hi) {
		u8 *mac = node->addr;

		addr = l2addr_hash_find(hash, mac, struct vport_addr);
		if (addr) {
			if (addr->action == MLX5_ACTION_DEL)
				addr->action = MLX5_ACTION_NONE;
			continue;
		}
		addr = l2addr_hash_add(hash, mac, struct vport_addr,
				       GFP_KERNEL);
		if (!addr) {
			esw_warn(esw->dev,
				 "Failed to add allmulti MAC(%pM) to vport[%d] DB\n",
				 mac, vport_num);
			continue;
		}
		addr->vport = vport_num;
		addr->action = MLX5_ACTION_ADD;
		addr->mc_promisc = true;
	}
}

/* Apply vport rx mode to HW FDB table */
static void esw_apply_vport_rx_mode(struct mlx5_eswitch *esw, u32 vport_num,
				    bool promisc, bool mc_promisc)
{
	struct esw_mc_addr *allmulti_addr = esw->mc_promisc;
	struct mlx5_vport *vport = &esw->vports[vport_num];

	if (IS_ERR_OR_NULL(vport->allmulti_rule) != mc_promisc)
		goto promisc;

	if (mc_promisc) {
		vport->allmulti_rule =
				esw_fdb_set_vport_allmulti_rule(esw, vport_num);
		if (!allmulti_addr->uplink_rule)
			allmulti_addr->uplink_rule =
				esw_fdb_set_vport_allmulti_rule(esw,
								UPLINK_VPORT);
		allmulti_addr->refcnt++;
	} else if (vport->allmulti_rule) {
		mlx5_del_flow_rules(vport->allmulti_rule);
		vport->allmulti_rule = NULL;

		if (--allmulti_addr->refcnt > 0)
			goto promisc;

		if (allmulti_addr->uplink_rule)
			mlx5_del_flow_rules(allmulti_addr->uplink_rule);
		allmulti_addr->uplink_rule = NULL;
	}

promisc:
	if (IS_ERR_OR_NULL(vport->promisc_rule) != promisc)
		return;

	if (promisc) {
		vport->promisc_rule = esw_fdb_set_vport_promisc_rule(esw,
								     vport_num);
	} else if (vport->promisc_rule) {
		mlx5_del_flow_rules(vport->promisc_rule);
		vport->promisc_rule = NULL;
	}
}

/* Sync vport rx mode from vport context */
static void esw_update_vport_rx_mode(struct mlx5_eswitch *esw, u32 vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	int promisc_all = 0;
	int promisc_uc = 0;
	int promisc_mc = 0;
	int err;

	err = mlx5_query_nic_vport_promisc(esw->dev,
					   vport_num,
					   &promisc_uc,
					   &promisc_mc,
					   &promisc_all);
	if (err)
		return;
	esw_debug(esw->dev, "vport[%d] context update rx mode promisc_all=%d, all_multi=%d\n",
		  vport_num, promisc_all, promisc_mc);

	if (!vport->info.trusted || !vport->enabled) {
		promisc_uc = 0;
		promisc_mc = 0;
		promisc_all = 0;
	}

	esw_apply_vport_rx_mode(esw, vport_num, promisc_all,
				(promisc_all || promisc_mc));
}

static void esw_vport_change_handle_locked(struct mlx5_vport *vport)
{
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
	}

	if (vport->enabled_events & PROMISC_CHANGE) {
		esw_update_vport_rx_mode(esw, vport->vport);
		if (!IS_ERR_OR_NULL(vport->allmulti_rule))
			esw_update_vport_mc_promisc(esw, vport->vport);
	}

	if (vport->enabled_events & (PROMISC_CHANGE | MC_ADDR_CHANGE)) {
		esw_apply_vport_addr_list(esw, vport->vport,
					  MLX5_NVPRT_LIST_TYPE_MC);
	}

	esw_debug(esw->dev, "vport[%d] Context Changed: Done\n", vport->vport);
	if (vport->enabled)
		arm_vport_context_events_cmd(dev, vport->vport,
					     vport->enabled_events);
}

static void esw_vport_change_handler(struct work_struct *work)
{
	struct mlx5_vport *vport =
		container_of(work, struct mlx5_vport, vport_change_handler);
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;

	mutex_lock(&esw->state_lock);
	esw_vport_change_handle_locked(vport);
	mutex_unlock(&esw->state_lock);
}

static int esw_vport_enable_egress_acl(struct mlx5_eswitch *esw,
				       struct mlx5_vport *vport)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *vlan_grp = NULL;
	struct mlx5_flow_group *drop_grp = NULL;
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *acl;
	void *match_criteria;
	u32 *flow_group_in;
	/* The egress acl table contains 2 rules:
	 * 1)Allow traffic with vlan_tag=vst_vlan_id
	 * 2)Drop all other traffic.
	 */
	int table_size = 2;
	int err = 0;

	if (!MLX5_CAP_ESW_EGRESS_ACL(dev, ft_support))
		return -EOPNOTSUPP;

	if (!IS_ERR_OR_NULL(vport->egress.acl))
		return 0;

	esw_debug(dev, "Create vport[%d] egress ACL log_max_size(%d)\n",
		  vport->vport, MLX5_CAP_ESW_EGRESS_ACL(dev, log_max_ft_size));

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_ESW_EGRESS);
	if (!root_ns) {
		esw_warn(dev, "Failed to get E-Switch egress flow namespace\n");
		return -EIO;
	}

	flow_group_in = mlx5_vzalloc(inlen);
	if (!flow_group_in)
		return -ENOMEM;

	acl = mlx5_create_vport_flow_table(root_ns, 0, table_size, 0, vport->vport);
	if (IS_ERR(acl)) {
		err = PTR_ERR(acl);
		esw_warn(dev, "Failed to create E-Switch vport[%d] egress flow Table, err(%d)\n",
			 vport->vport, err);
		goto out;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.vlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.first_vid);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 0);

	vlan_grp = mlx5_create_flow_group(acl, flow_group_in);
	if (IS_ERR(vlan_grp)) {
		err = PTR_ERR(vlan_grp);
		esw_warn(dev, "Failed to create E-Switch vport[%d] egress allowed vlans flow group, err(%d)\n",
			 vport->vport, err);
		goto out;
	}

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);
	drop_grp = mlx5_create_flow_group(acl, flow_group_in);
	if (IS_ERR(drop_grp)) {
		err = PTR_ERR(drop_grp);
		esw_warn(dev, "Failed to create E-Switch vport[%d] egress drop flow group, err(%d)\n",
			 vport->vport, err);
		goto out;
	}

	vport->egress.acl = acl;
	vport->egress.drop_grp = drop_grp;
	vport->egress.allowed_vlans_grp = vlan_grp;
out:
	kvfree(flow_group_in);
	if (err && !IS_ERR_OR_NULL(vlan_grp))
		mlx5_destroy_flow_group(vlan_grp);
	if (err && !IS_ERR_OR_NULL(acl))
		mlx5_destroy_flow_table(acl);
	return err;
}

static void esw_vport_cleanup_egress_rules(struct mlx5_eswitch *esw,
					   struct mlx5_vport *vport)
{
	if (!IS_ERR_OR_NULL(vport->egress.allowed_vlan))
		mlx5_del_flow_rules(vport->egress.allowed_vlan);

	if (!IS_ERR_OR_NULL(vport->egress.drop_rule))
		mlx5_del_flow_rules(vport->egress.drop_rule);

	vport->egress.allowed_vlan = NULL;
	vport->egress.drop_rule = NULL;
}

static void esw_vport_disable_egress_acl(struct mlx5_eswitch *esw,
					 struct mlx5_vport *vport)
{
	if (IS_ERR_OR_NULL(vport->egress.acl))
		return;

	esw_debug(esw->dev, "Destroy vport[%d] E-Switch egress ACL\n", vport->vport);

	esw_vport_cleanup_egress_rules(esw, vport);
	mlx5_destroy_flow_group(vport->egress.allowed_vlans_grp);
	mlx5_destroy_flow_group(vport->egress.drop_grp);
	mlx5_destroy_flow_table(vport->egress.acl);
	vport->egress.allowed_vlans_grp = NULL;
	vport->egress.drop_grp = NULL;
	vport->egress.acl = NULL;
}

static int esw_vport_enable_ingress_acl(struct mlx5_eswitch *esw,
					struct mlx5_vport *vport)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *g;
	void *match_criteria;
	u32 *flow_group_in;
	/* The ingress acl table contains 4 groups
	 * (2 active rules at the same time -
	 *      1 allow rule from one of the first 3 groups.
	 *      1 drop rule from the last group):
	 * 1)Allow untagged traffic with smac=original mac.
	 * 2)Allow untagged traffic.
	 * 3)Allow traffic with smac=original mac.
	 * 4)Drop all other traffic.
	 */
	int table_size = 4;
	int err = 0;

	if (!MLX5_CAP_ESW_INGRESS_ACL(dev, ft_support))
		return -EOPNOTSUPP;

	if (!IS_ERR_OR_NULL(vport->ingress.acl))
		return 0;

	esw_debug(dev, "Create vport[%d] ingress ACL log_max_size(%d)\n",
		  vport->vport, MLX5_CAP_ESW_INGRESS_ACL(dev, log_max_ft_size));

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_ESW_INGRESS);
	if (!root_ns) {
		esw_warn(dev, "Failed to get E-Switch ingress flow namespace\n");
		return -EIO;
	}

	flow_group_in = mlx5_vzalloc(inlen);
	if (!flow_group_in)
		return -ENOMEM;

	acl = mlx5_create_vport_flow_table(root_ns, 0, table_size, 0, vport->vport);
	if (IS_ERR(acl)) {
		err = PTR_ERR(acl);
		esw_warn(dev, "Failed to create E-Switch vport[%d] ingress flow Table, err(%d)\n",
			 vport->vport, err);
		goto out;
	}
	vport->ingress.acl = acl;

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);

	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.vlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_15_0);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 0);

	g = mlx5_create_flow_group(acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create E-Switch vport[%d] ingress untagged spoofchk flow group, err(%d)\n",
			 vport->vport, err);
		goto out;
	}
	vport->ingress.allow_untagged_spoofchk_grp = g;

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.vlan_tag);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);

	g = mlx5_create_flow_group(acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create E-Switch vport[%d] ingress untagged flow group, err(%d)\n",
			 vport->vport, err);
		goto out;
	}
	vport->ingress.allow_untagged_only_grp = g;

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, outer_headers.smac_15_0);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 2);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 2);

	g = mlx5_create_flow_group(acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create E-Switch vport[%d] ingress spoofchk flow group, err(%d)\n",
			 vport->vport, err);
		goto out;
	}
	vport->ingress.allow_spoofchk_only_grp = g;

	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 3);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 3);

	g = mlx5_create_flow_group(acl, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create E-Switch vport[%d] ingress drop flow group, err(%d)\n",
			 vport->vport, err);
		goto out;
	}
	vport->ingress.drop_grp = g;

out:
	if (err) {
		if (!IS_ERR_OR_NULL(vport->ingress.allow_spoofchk_only_grp))
			mlx5_destroy_flow_group(
					vport->ingress.allow_spoofchk_only_grp);
		if (!IS_ERR_OR_NULL(vport->ingress.allow_untagged_only_grp))
			mlx5_destroy_flow_group(
					vport->ingress.allow_untagged_only_grp);
		if (!IS_ERR_OR_NULL(vport->ingress.allow_untagged_spoofchk_grp))
			mlx5_destroy_flow_group(
				vport->ingress.allow_untagged_spoofchk_grp);
		if (!IS_ERR_OR_NULL(vport->ingress.acl))
			mlx5_destroy_flow_table(vport->ingress.acl);
	}

	kvfree(flow_group_in);
	return err;
}

static void esw_vport_cleanup_ingress_rules(struct mlx5_eswitch *esw,
					    struct mlx5_vport *vport)
{
	if (!IS_ERR_OR_NULL(vport->ingress.drop_rule))
		mlx5_del_flow_rules(vport->ingress.drop_rule);

	if (!IS_ERR_OR_NULL(vport->ingress.allow_rule))
		mlx5_del_flow_rules(vport->ingress.allow_rule);

	vport->ingress.drop_rule = NULL;
	vport->ingress.allow_rule = NULL;
}

static void esw_vport_disable_ingress_acl(struct mlx5_eswitch *esw,
					  struct mlx5_vport *vport)
{
	if (IS_ERR_OR_NULL(vport->ingress.acl))
		return;

	esw_debug(esw->dev, "Destroy vport[%d] E-Switch ingress ACL\n", vport->vport);

	esw_vport_cleanup_ingress_rules(esw, vport);
	mlx5_destroy_flow_group(vport->ingress.allow_spoofchk_only_grp);
	mlx5_destroy_flow_group(vport->ingress.allow_untagged_only_grp);
	mlx5_destroy_flow_group(vport->ingress.allow_untagged_spoofchk_grp);
	mlx5_destroy_flow_group(vport->ingress.drop_grp);
	mlx5_destroy_flow_table(vport->ingress.acl);
	vport->ingress.acl = NULL;
	vport->ingress.drop_grp = NULL;
	vport->ingress.allow_spoofchk_only_grp = NULL;
	vport->ingress.allow_untagged_only_grp = NULL;
	vport->ingress.allow_untagged_spoofchk_grp = NULL;
}

static int esw_vport_ingress_config(struct mlx5_eswitch *esw,
				    struct mlx5_vport *vport)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 *smac_v;

	if (vport->info.spoofchk && !is_valid_ether_addr(vport->info.mac)) {
		mlx5_core_warn(esw->dev,
			       "vport[%d] configure ingress rules failed, illegal mac with spoofchk\n",
			       vport->vport);
		return -EPERM;

	}

	esw_vport_cleanup_ingress_rules(esw, vport);

	if (!vport->info.vlan && !vport->info.qos && !vport->info.spoofchk) {
		esw_vport_disable_ingress_acl(esw, vport);
		return 0;
	}

	err = esw_vport_enable_ingress_acl(esw, vport);
	if (err) {
		mlx5_core_warn(esw->dev,
			       "failed to enable ingress acl (%d) on vport[%d]\n",
			       err, vport->vport);
		return err;
	}

	esw_debug(esw->dev,
		  "vport[%d] configure ingress rules, vlan(%d) qos(%d)\n",
		  vport->vport, vport->info.vlan, vport->info.qos);

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec) {
		err = -ENOMEM;
		esw_warn(esw->dev, "vport[%d] configure ingress rules failed, err(%d)\n",
			 vport->vport, err);
		goto out;
	}

	if (vport->info.vlan || vport->info.qos)
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.vlan_tag);

	if (vport->info.spoofchk) {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.smac_47_16);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.smac_15_0);
		smac_v = MLX5_ADDR_OF(fte_match_param,
				      spec->match_value,
				      outer_headers.smac_47_16);
		ether_addr_copy(smac_v, vport->info.mac);
	}

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	vport->ingress.allow_rule =
		mlx5_add_flow_rules(vport->ingress.acl, spec,
				    &flow_act, NULL, 0);
	if (IS_ERR(vport->ingress.allow_rule)) {
		err = PTR_ERR(vport->ingress.allow_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure ingress allow rule, err(%d)\n",
			 vport->vport, err);
		vport->ingress.allow_rule = NULL;
		goto out;
	}

	memset(spec, 0, sizeof(*spec));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	vport->ingress.drop_rule =
		mlx5_add_flow_rules(vport->ingress.acl, spec,
				    &flow_act, NULL, 0);
	if (IS_ERR(vport->ingress.drop_rule)) {
		err = PTR_ERR(vport->ingress.drop_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure ingress drop rule, err(%d)\n",
			 vport->vport, err);
		vport->ingress.drop_rule = NULL;
		goto out;
	}

out:
	if (err)
		esw_vport_cleanup_ingress_rules(esw, vport);
	kvfree(spec);
	return err;
}

static int esw_vport_egress_config(struct mlx5_eswitch *esw,
				   struct mlx5_vport *vport)
{
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_spec *spec;
	int err = 0;

	esw_vport_cleanup_egress_rules(esw, vport);

	if (!vport->info.vlan && !vport->info.qos) {
		esw_vport_disable_egress_acl(esw, vport);
		return 0;
	}

	err = esw_vport_enable_egress_acl(esw, vport);
	if (err) {
		mlx5_core_warn(esw->dev,
			       "failed to enable egress acl (%d) on vport[%d]\n",
			       err, vport->vport);
		return err;
	}

	esw_debug(esw->dev,
		  "vport[%d] configure egress rules, vlan(%d) qos(%d)\n",
		  vport->vport, vport->info.vlan, vport->info.qos);

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec) {
		err = -ENOMEM;
		esw_warn(esw->dev, "vport[%d] configure egress rules failed, err(%d)\n",
			 vport->vport, err);
		goto out;
	}

	/* Allowed vlan rule */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.vlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_value, outer_headers.vlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.first_vid);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.first_vid, vport->info.vlan);

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	vport->egress.allowed_vlan =
		mlx5_add_flow_rules(vport->egress.acl, spec,
				    &flow_act, NULL, 0);
	if (IS_ERR(vport->egress.allowed_vlan)) {
		err = PTR_ERR(vport->egress.allowed_vlan);
		esw_warn(esw->dev,
			 "vport[%d] configure egress allowed vlan rule failed, err(%d)\n",
			 vport->vport, err);
		vport->egress.allowed_vlan = NULL;
		goto out;
	}

	/* Drop others rule (star rule) */
	memset(spec, 0, sizeof(*spec));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	vport->egress.drop_rule =
		mlx5_add_flow_rules(vport->egress.acl, spec,
				    &flow_act, NULL, 0);
	if (IS_ERR(vport->egress.drop_rule)) {
		err = PTR_ERR(vport->egress.drop_rule);
		esw_warn(esw->dev,
			 "vport[%d] configure egress drop rule failed, err(%d)\n",
			 vport->vport, err);
		vport->egress.drop_rule = NULL;
	}
out:
	kvfree(spec);
	return err;
}

/* Vport QoS management */
static int esw_create_tsar(struct mlx5_eswitch *esw)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {0};
	struct mlx5_core_dev *dev = esw->dev;
	int err;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return 0;

	if (esw->qos.enabled)
		return -EEXIST;

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 &tsar_ctx,
						 &esw->qos.root_tsar_id);
	if (err) {
		esw_warn(esw->dev, "E-Switch create TSAR failed (%d)\n", err);
		return err;
	}

	esw->qos.enabled = true;
	return 0;
}

static void esw_destroy_tsar(struct mlx5_eswitch *esw)
{
	int err;

	if (!esw->qos.enabled)
		return;

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  esw->qos.root_tsar_id);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy TSAR failed (%d)\n", err);

	esw->qos.enabled = false;
}

static int esw_vport_enable_qos(struct mlx5_eswitch *esw, int vport_num,
				u32 initial_max_rate)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {0};
	struct mlx5_vport *vport = &esw->vports[vport_num];
	struct mlx5_core_dev *dev = esw->dev;
	void *vport_elem;
	int err = 0;

	if (!esw->qos.enabled || !MLX5_CAP_GEN(dev, qos) ||
	    !MLX5_CAP_QOS(dev, esw_scheduling))
		return 0;

	if (vport->qos.enabled)
		return -EEXIST;

	MLX5_SET(scheduling_context, &sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	vport_elem = MLX5_ADDR_OF(scheduling_context, &sched_ctx,
				  element_attributes);
	MLX5_SET(vport_element, vport_elem, vport_number, vport_num);
	MLX5_SET(scheduling_context, &sched_ctx, parent_element_id,
		 esw->qos.root_tsar_id);
	MLX5_SET(scheduling_context, &sched_ctx, max_average_bw,
		 initial_max_rate);

	err = mlx5_create_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 &sched_ctx,
						 &vport->qos.esw_tsar_ix);
	if (err) {
		esw_warn(esw->dev, "E-Switch create TSAR vport element failed (vport=%d,err=%d)\n",
			 vport_num, err);
		return err;
	}

	vport->qos.enabled = true;
	return 0;
}

static void esw_vport_disable_qos(struct mlx5_eswitch *esw, int vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	int err = 0;

	if (!vport->qos.enabled)
		return;

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  vport->qos.esw_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy TSAR vport element failed (vport=%d,err=%d)\n",
			 vport_num, err);

	vport->qos.enabled = false;
}

static int esw_vport_qos_config(struct mlx5_eswitch *esw, int vport_num,
				u32 max_rate)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {0};
	struct mlx5_vport *vport = &esw->vports[vport_num];
	struct mlx5_core_dev *dev = esw->dev;
	void *vport_elem;
	u32 bitmask = 0;
	int err = 0;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	if (!vport->qos.enabled)
		return -EIO;

	MLX5_SET(scheduling_context, &sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	vport_elem = MLX5_ADDR_OF(scheduling_context, &sched_ctx,
				  element_attributes);
	MLX5_SET(vport_element, vport_elem, vport_number, vport_num);
	MLX5_SET(scheduling_context, &sched_ctx, parent_element_id,
		 esw->qos.root_tsar_id);
	MLX5_SET(scheduling_context, &sched_ctx, max_average_bw,
		 max_rate);
	bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;

	err = mlx5_modify_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 &sched_ctx,
						 vport->qos.esw_tsar_ix,
						 bitmask);
	if (err) {
		esw_warn(esw->dev, "E-Switch modify TSAR vport element failed (vport=%d,err=%d)\n",
			 vport_num, err);
		return err;
	}

	return 0;
}

static void node_guid_gen_from_mac(u64 *node_guid, u8 mac[ETH_ALEN])
{
	((u8 *)node_guid)[7] = mac[0];
	((u8 *)node_guid)[6] = mac[1];
	((u8 *)node_guid)[5] = mac[2];
	((u8 *)node_guid)[4] = 0xff;
	((u8 *)node_guid)[3] = 0xfe;
	((u8 *)node_guid)[2] = mac[3];
	((u8 *)node_guid)[1] = mac[4];
	((u8 *)node_guid)[0] = mac[5];
}

static void esw_apply_vport_conf(struct mlx5_eswitch *esw,
				 struct mlx5_vport *vport)
{
	int vport_num = vport->vport;

	if (!vport_num)
		return;

	mlx5_modify_vport_admin_state(esw->dev,
				      MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
				      vport_num,
				      vport->info.link_state);
	mlx5_modify_nic_vport_mac_address(esw->dev, vport_num, vport->info.mac);
	mlx5_modify_nic_vport_node_guid(esw->dev, vport_num, vport->info.node_guid);
	modify_esw_vport_cvlan(esw->dev, vport_num, vport->info.vlan, vport->info.qos,
			       (vport->info.vlan || vport->info.qos));

	/* Only legacy mode needs ACLs */
	if (esw->mode == SRIOV_LEGACY) {
		esw_vport_ingress_config(esw, vport);
		esw_vport_egress_config(esw, vport);
	}
}

static void esw_enable_vport(struct mlx5_eswitch *esw, int vport_num,
			     int enable_events)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];

	mutex_lock(&esw->state_lock);
	WARN_ON(vport->enabled);

	esw_debug(esw->dev, "Enabling VPORT(%d)\n", vport_num);

	/* Restore old vport configuration */
	esw_apply_vport_conf(esw, vport);

	/* Attach vport to the eswitch rate limiter */
	if (esw_vport_enable_qos(esw, vport_num, vport->info.max_rate))
		esw_warn(esw->dev, "Failed to attach vport %d to eswitch rate limiter", vport_num);

	/* Sync with current vport context */
	vport->enabled_events = enable_events;
	vport->enabled = true;

	/* only PF is trusted by default */
	if (!vport_num)
		vport->info.trusted = true;

	esw_vport_change_handle_locked(vport);

	esw->enabled_vports++;
	esw_debug(esw->dev, "Enabled VPORT(%d)\n", vport_num);
	mutex_unlock(&esw->state_lock);
}

static void esw_disable_vport(struct mlx5_eswitch *esw, int vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];

	if (!vport->enabled)
		return;

	esw_debug(esw->dev, "Disabling vport(%d)\n", vport_num);
	/* Mark this vport as disabled to discard new events */
	vport->enabled = false;

	synchronize_irq(mlx5_get_msix_vec(esw->dev, MLX5_EQ_VEC_ASYNC));
	/* Wait for current already scheduled events to complete */
	flush_workqueue(esw->work_queue);
	/* Disable events from this vport */
	arm_vport_context_events_cmd(esw->dev, vport->vport, 0);
	mutex_lock(&esw->state_lock);
	/* We don't assume VFs will cleanup after themselves.
	 * Calling vport change handler while vport is disabled will cleanup
	 * the vport resources.
	 */
	esw_vport_change_handle_locked(vport);
	vport->enabled_events = 0;
	esw_vport_disable_qos(esw, vport_num);
	if (vport_num && esw->mode == SRIOV_LEGACY) {
		mlx5_modify_vport_admin_state(esw->dev,
					      MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
					      vport_num,
					      MLX5_ESW_VPORT_ADMIN_STATE_DOWN);
		esw_vport_disable_egress_acl(esw, vport);
		esw_vport_disable_ingress_acl(esw, vport);
	}
	esw->enabled_vports--;
	mutex_unlock(&esw->state_lock);
}

/* Public E-Switch API */
int mlx5_eswitch_enable_sriov(struct mlx5_eswitch *esw, int nvfs, int mode)
{
	int err;
	int i, enabled_events;

	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return 0;

	if (!MLX5_CAP_GEN(esw->dev, eswitch_flow_table) ||
	    !MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, ft_support)) {
		esw_warn(esw->dev, "E-Switch FDB is not supported, aborting ...\n");
		return -ENOTSUPP;
	}

	if (!MLX5_CAP_ESW_INGRESS_ACL(esw->dev, ft_support))
		esw_warn(esw->dev, "E-Switch ingress ACL is not supported by FW\n");

	if (!MLX5_CAP_ESW_EGRESS_ACL(esw->dev, ft_support))
		esw_warn(esw->dev, "E-Switch engress ACL is not supported by FW\n");

	esw_info(esw->dev, "E-Switch enable SRIOV: nvfs(%d) mode (%d)\n", nvfs, mode);
	esw->mode = mode;
	esw_disable_vport(esw, 0);

	if (mode == SRIOV_LEGACY)
		err = esw_create_legacy_fdb_table(esw, nvfs + 1);
	else
		err = esw_offloads_init(esw, nvfs + 1);
	if (err)
		goto abort;

	err = esw_create_tsar(esw);
	if (err)
		esw_warn(esw->dev, "Failed to create eswitch TSAR");

	enabled_events = (mode == SRIOV_LEGACY) ? SRIOV_VPORT_EVENTS : UC_ADDR_CHANGE;
	for (i = 0; i <= nvfs; i++)
		esw_enable_vport(esw, i, enabled_events);

	esw_info(esw->dev, "SRIOV enabled: active vports(%d)\n",
		 esw->enabled_vports);
	return 0;

abort:
	esw_enable_vport(esw, 0, UC_ADDR_CHANGE);
	esw->mode = SRIOV_NONE;
	return err;
}

void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw)
{
	struct esw_mc_addr *mc_promisc;
	int nvports;
	int i;

	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return;

	esw_info(esw->dev, "disable SRIOV: active vports(%d) mode(%d)\n",
		 esw->enabled_vports, esw->mode);

	mc_promisc = esw->mc_promisc;
	nvports = esw->enabled_vports;

	for (i = 0; i < esw->total_vports; i++)
		esw_disable_vport(esw, i);

	if (mc_promisc && mc_promisc->uplink_rule)
		mlx5_del_flow_rules(mc_promisc->uplink_rule);

	esw_destroy_tsar(esw);

	if (esw->mode == SRIOV_LEGACY)
		esw_destroy_legacy_fdb_table(esw);
	else if (esw->mode == SRIOV_OFFLOADS)
		esw_offloads_cleanup(esw, nvports);

	esw->mode = SRIOV_NONE;
	/* VPORT 0 (PF) must be enabled back with non-sriov configuration */
	esw_enable_vport(esw, 0, UC_ADDR_CHANGE);
}

void mlx5_eswitch_attach(struct mlx5_eswitch *esw)
{
	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return;

	esw_enable_vport(esw, 0, UC_ADDR_CHANGE);
	/* VF Vports will be enabled when SRIOV is enabled */
}

void mlx5_eswitch_detach(struct mlx5_eswitch *esw)
{
	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return;

	esw_disable_vport(esw, 0);
}

int mlx5_eswitch_init(struct mlx5_core_dev *dev)
{
	int l2_table_size = 1 << MLX5_CAP_GEN(dev, log_max_l2_table);
	int total_vports = MLX5_TOTAL_VPORTS(dev);
	struct esw_mc_addr *mc_promisc;
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

	mc_promisc = kzalloc(sizeof(*mc_promisc), GFP_KERNEL);
	if (!mc_promisc) {
		err = -ENOMEM;
		goto abort;
	}
	esw->mc_promisc = mc_promisc;

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

	esw->offloads.vport_reps =
		kzalloc(total_vports * sizeof(struct mlx5_eswitch_rep),
			GFP_KERNEL);
	if (!esw->offloads.vport_reps) {
		err = -ENOMEM;
		goto abort;
	}

	hash_init(esw->offloads.encap_tbl);
	mutex_init(&esw->state_lock);

	for (vport_num = 0; vport_num < total_vports; vport_num++) {
		struct mlx5_vport *vport = &esw->vports[vport_num];

		vport->vport = vport_num;
		vport->info.link_state = MLX5_ESW_VPORT_ADMIN_STATE_AUTO;
		vport->dev = dev;
		INIT_WORK(&vport->vport_change_handler,
			  esw_vport_change_handler);
	}

	esw->total_vports = total_vports;
	esw->enabled_vports = 0;
	esw->mode = SRIOV_NONE;
	esw->offloads.inline_mode = MLX5_INLINE_MODE_NONE;

	dev->priv.eswitch = esw;
	return 0;
abort:
	if (esw->work_queue)
		destroy_workqueue(esw->work_queue);
	kfree(esw->l2_table.bitmap);
	kfree(esw->vports);
	kfree(esw->offloads.vport_reps);
	kfree(esw);
	return err;
}

void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw)
{
	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager) ||
	    MLX5_CAP_GEN(esw->dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return;

	esw_info(esw->dev, "cleanup\n");

	esw->dev->priv.eswitch = NULL;
	destroy_workqueue(esw->work_queue);
	kfree(esw->l2_table.bitmap);
	kfree(esw->mc_promisc);
	kfree(esw->offloads.vport_reps);
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
	if (vport->enabled)
		queue_work(esw->work_queue, &vport->vport_change_handler);
}

/* Vport Administration */
#define ESW_ALLOWED(esw) \
	(esw && MLX5_CAP_GEN(esw->dev, vport_group_manager) && mlx5_core_is_pf(esw->dev))
#define LEGAL_VPORT(esw, vport) (vport >= 0 && vport < esw->total_vports)

int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       int vport, u8 mac[ETH_ALEN])
{
	struct mlx5_vport *evport;
	u64 node_guid;
	int err = 0;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport) || is_multicast_ether_addr(mac))
		return -EINVAL;

	mutex_lock(&esw->state_lock);
	evport = &esw->vports[vport];

	if (evport->info.spoofchk && !is_valid_ether_addr(mac)) {
		mlx5_core_warn(esw->dev,
			       "MAC invalidation is not allowed when spoofchk is on, vport(%d)\n",
			       vport);
		err = -EPERM;
		goto unlock;
	}

	err = mlx5_modify_nic_vport_mac_address(esw->dev, vport, mac);
	if (err) {
		mlx5_core_warn(esw->dev,
			       "Failed to mlx5_modify_nic_vport_mac vport(%d) err=(%d)\n",
			       vport, err);
		goto unlock;
	}

	node_guid_gen_from_mac(&node_guid, mac);
	err = mlx5_modify_nic_vport_node_guid(esw->dev, vport, node_guid);
	if (err)
		mlx5_core_warn(esw->dev,
			       "Failed to set vport %d node guid, err = %d. RDMA_CM will not function properly for this VF.\n",
			       vport, err);

	ether_addr_copy(evport->info.mac, mac);
	evport->info.node_guid = node_guid;
	if (evport->enabled && esw->mode == SRIOV_LEGACY)
		err = esw_vport_ingress_config(esw, evport);

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 int vport, int link_state)
{
	struct mlx5_vport *evport;
	int err = 0;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	mutex_lock(&esw->state_lock);
	evport = &esw->vports[vport];

	err = mlx5_modify_vport_admin_state(esw->dev,
					    MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
					    vport, link_state);
	if (err) {
		mlx5_core_warn(esw->dev,
			       "Failed to set vport %d link state, err = %d",
			       vport, err);
		goto unlock;
	}

	evport->info.link_state = link_state;

unlock:
	mutex_unlock(&esw->state_lock);
	return 0;
}

int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  int vport, struct ifla_vf_info *ivi)
{
	struct mlx5_vport *evport;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	evport = &esw->vports[vport];

	memset(ivi, 0, sizeof(*ivi));
	ivi->vf = vport - 1;

	mutex_lock(&esw->state_lock);
	ether_addr_copy(ivi->mac, evport->info.mac);
	ivi->linkstate = evport->info.link_state;
	ivi->vlan = evport->info.vlan;
	ivi->qos = evport->info.qos;
	ivi->spoofchk = evport->info.spoofchk;
	ivi->trusted = evport->info.trusted;
	ivi->max_tx_rate = evport->info.max_rate;
	mutex_unlock(&esw->state_lock);

	return 0;
}

int __mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				  int vport, u16 vlan, u8 qos, u8 set_flags)
{
	struct mlx5_vport *evport;
	int err = 0;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport) || (vlan > 4095) || (qos > 7))
		return -EINVAL;

	mutex_lock(&esw->state_lock);
	evport = &esw->vports[vport];

	err = modify_esw_vport_cvlan(esw->dev, vport, vlan, qos, set_flags);
	if (err)
		goto unlock;

	evport->info.vlan = vlan;
	evport->info.qos = qos;
	if (evport->enabled && esw->mode == SRIOV_LEGACY) {
		err = esw_vport_ingress_config(esw, evport);
		if (err)
			goto unlock;
		err = esw_vport_egress_config(esw, evport);
	}

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				int vport, u16 vlan, u8 qos)
{
	u8 set_flags = 0;

	if (vlan || qos)
		set_flags = SET_VLAN_STRIP | SET_VLAN_INSERT;

	return __mlx5_eswitch_set_vport_vlan(esw, vport, vlan, qos, set_flags);
}

int mlx5_eswitch_set_vport_spoofchk(struct mlx5_eswitch *esw,
				    int vport, bool spoofchk)
{
	struct mlx5_vport *evport;
	bool pschk;
	int err = 0;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	mutex_lock(&esw->state_lock);
	evport = &esw->vports[vport];
	pschk = evport->info.spoofchk;
	evport->info.spoofchk = spoofchk;
	if (evport->enabled && esw->mode == SRIOV_LEGACY)
		err = esw_vport_ingress_config(esw, evport);
	if (err)
		evport->info.spoofchk = pschk;
	mutex_unlock(&esw->state_lock);

	return err;
}

int mlx5_eswitch_set_vport_trust(struct mlx5_eswitch *esw,
				 int vport, bool setting)
{
	struct mlx5_vport *evport;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	mutex_lock(&esw->state_lock);
	evport = &esw->vports[vport];
	evport->info.trusted = setting;
	if (evport->enabled)
		esw_vport_change_handle_locked(evport);
	mutex_unlock(&esw->state_lock);

	return 0;
}

int mlx5_eswitch_set_vport_rate(struct mlx5_eswitch *esw,
				int vport, u32 max_rate)
{
	struct mlx5_vport *evport;
	int err = 0;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	mutex_lock(&esw->state_lock);
	evport = &esw->vports[vport];
	err = esw_vport_qos_config(esw, vport, max_rate);
	if (!err)
		evport->info.max_rate = max_rate;

	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 int vport,
				 struct ifla_vf_stats *vf_stats)
{
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)] = {0};
	int err = 0;
	u32 *out;

	if (!ESW_ALLOWED(esw))
		return -EPERM;
	if (!LEGAL_VPORT(esw, vport))
		return -EINVAL;

	out = mlx5_vzalloc(outlen);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_vport_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	MLX5_SET(query_vport_counter_in, in, op_mod, 0);
	MLX5_SET(query_vport_counter_in, in, vport_number, vport);
	if (vport)
		MLX5_SET(query_vport_counter_in, in, other_vport, 1);

	memset(out, 0, outlen);
	err = mlx5_cmd_exec(esw->dev, in, sizeof(in), out, outlen);
	if (err)
		goto free_out;

	#define MLX5_GET_CTR(p, x) \
		MLX5_GET64(query_vport_counter_out, p, x)

	memset(vf_stats, 0, sizeof(*vf_stats));
	vf_stats->rx_packets =
		MLX5_GET_CTR(out, received_eth_unicast.packets) +
		MLX5_GET_CTR(out, received_eth_multicast.packets) +
		MLX5_GET_CTR(out, received_eth_broadcast.packets);

	vf_stats->rx_bytes =
		MLX5_GET_CTR(out, received_eth_unicast.octets) +
		MLX5_GET_CTR(out, received_eth_multicast.octets) +
		MLX5_GET_CTR(out, received_eth_broadcast.octets);

	vf_stats->tx_packets =
		MLX5_GET_CTR(out, transmitted_eth_unicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_multicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_broadcast.packets);

	vf_stats->tx_bytes =
		MLX5_GET_CTR(out, transmitted_eth_unicast.octets) +
		MLX5_GET_CTR(out, transmitted_eth_multicast.octets) +
		MLX5_GET_CTR(out, transmitted_eth_broadcast.octets);

	vf_stats->multicast =
		MLX5_GET_CTR(out, received_eth_multicast.packets);

	vf_stats->broadcast =
		MLX5_GET_CTR(out, received_eth_broadcast.packets);

free_out:
	kvfree(out);
	return err;
}
