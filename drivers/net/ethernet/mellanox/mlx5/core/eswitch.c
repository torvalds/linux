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
#include <linux/mlx5/mpfs.h>
#include <linux/debugfs.h>
#include "esw/acl/lgcy.h"
#include "esw/legacy.h"
#include "esw/qos.h"
#include "mlx5_core.h"
#include "lib/eq.h"
#include "eswitch.h"
#include "fs_core.h"
#include "devlink.h"
#include "ecpf.h"
#include "en/mod_hdr.h"

enum {
	MLX5_ACTION_NONE = 0,
	MLX5_ACTION_ADD  = 1,
	MLX5_ACTION_DEL  = 2,
};

/* Vport UC/MC hash node */
struct vport_addr {
	struct l2addr_node     node;
	u8                     action;
	u16                    vport;
	struct mlx5_flow_handle *flow_rule;
	bool mpfs; /* UC MAC was added to MPFs */
	/* A flag indicating that mac was added due to mc promiscuous vport */
	bool mc_promisc;
};

static int mlx5_eswitch_check(const struct mlx5_core_dev *dev)
{
	if (MLX5_CAP_GEN(dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return -EOPNOTSUPP;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return -EOPNOTSUPP;

	return 0;
}

struct mlx5_eswitch *mlx5_devlink_eswitch_get(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	int err;

	err = mlx5_eswitch_check(dev);
	if (err)
		return ERR_PTR(err);

	return dev->priv.eswitch;
}

struct mlx5_vport *__must_check
mlx5_eswitch_get_vport(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport;

	if (!esw || !MLX5_CAP_GEN(esw->dev, vport_group_manager))
		return ERR_PTR(-EPERM);

	vport = xa_load(&esw->vports, vport_num);
	if (!vport) {
		esw_debug(esw->dev, "vport out of range: num(0x%x)\n", vport_num);
		return ERR_PTR(-EINVAL);
	}
	return vport;
}

static int arm_vport_context_events_cmd(struct mlx5_core_dev *dev, u16 vport,
					u32 events_mask)
{
	u32 in[MLX5_ST_SZ_DW(modify_nic_vport_context_in)] = {};
	void *nic_vport_ctx;

	MLX5_SET(modify_nic_vport_context_in, in,
		 opcode, MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT);
	MLX5_SET(modify_nic_vport_context_in, in, field_select.change_event, 1);
	MLX5_SET(modify_nic_vport_context_in, in, vport_number, vport);
	MLX5_SET(modify_nic_vport_context_in, in, other_vport, 1);
	nic_vport_ctx = MLX5_ADDR_OF(modify_nic_vport_context_in,
				     in, nic_vport_context);

	MLX5_SET(nic_vport_context, nic_vport_ctx, arm_change_event, 1);

	if (events_mask & MLX5_VPORT_UC_ADDR_CHANGE)
		MLX5_SET(nic_vport_context, nic_vport_ctx,
			 event_on_uc_address_change, 1);
	if (events_mask & MLX5_VPORT_MC_ADDR_CHANGE)
		MLX5_SET(nic_vport_context, nic_vport_ctx,
			 event_on_mc_address_change, 1);
	if (events_mask & MLX5_VPORT_PROMISC_CHANGE)
		MLX5_SET(nic_vport_context, nic_vport_ctx,
			 event_on_promisc_change, 1);

	return mlx5_cmd_exec_in(dev, modify_nic_vport_context, in);
}

/* E-Switch vport context HW commands */
int mlx5_eswitch_modify_esw_vport_context(struct mlx5_core_dev *dev, u16 vport,
					  bool other_vport, void *in)
{
	MLX5_SET(modify_esw_vport_context_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_ESW_VPORT_CONTEXT);
	MLX5_SET(modify_esw_vport_context_in, in, vport_number, vport);
	MLX5_SET(modify_esw_vport_context_in, in, other_vport, other_vport);
	return mlx5_cmd_exec_in(dev, modify_esw_vport_context, in);
}

static int modify_esw_vport_cvlan(struct mlx5_core_dev *dev, u16 vport,
				  u16 vlan, u8 qos, u8 set_flags)
{
	u32 in[MLX5_ST_SZ_DW(modify_esw_vport_context_in)] = {};

	if (!MLX5_CAP_ESW(dev, vport_cvlan_strip) ||
	    !MLX5_CAP_ESW(dev, vport_cvlan_insert_if_not_exist))
		return -EOPNOTSUPP;

	esw_debug(dev, "Set Vport[%d] VLAN %d qos %d set=%x\n",
		  vport, vlan, qos, set_flags);

	if (set_flags & SET_VLAN_STRIP)
		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.vport_cvlan_strip, 1);

	if (set_flags & SET_VLAN_INSERT) {
		if (MLX5_CAP_ESW(dev, vport_cvlan_insert_always)) {
			/* insert either if vlan exist in packet or not */
			MLX5_SET(modify_esw_vport_context_in, in,
				 esw_vport_context.vport_cvlan_insert,
				 MLX5_VPORT_CVLAN_INSERT_ALWAYS);
		} else {
			/* insert only if no vlan in packet */
			MLX5_SET(modify_esw_vport_context_in, in,
				 esw_vport_context.vport_cvlan_insert,
				 MLX5_VPORT_CVLAN_INSERT_WHEN_NO_CVLAN);
		}
		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.cvlan_pcp, qos);
		MLX5_SET(modify_esw_vport_context_in, in,
			 esw_vport_context.cvlan_id, vlan);
	}

	MLX5_SET(modify_esw_vport_context_in, in,
		 field_select.vport_cvlan_strip, 1);
	MLX5_SET(modify_esw_vport_context_in, in,
		 field_select.vport_cvlan_insert, 1);

	return mlx5_eswitch_modify_esw_vport_context(dev, vport, true, in);
}

/* E-Switch FDB */
static struct mlx5_flow_handle *
__esw_fdb_set_vport_rule(struct mlx5_eswitch *esw, u16 vport, bool rx_rule,
			 u8 mac_c[ETH_ALEN], u8 mac_v[ETH_ALEN])
{
	int match_header = (is_zero_ether_addr(mac_c) ? 0 :
			    MLX5_MATCH_OUTER_HEADERS);
	struct mlx5_flow_handle *flow_rule = NULL;
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_spec *spec;
	void *mv_misc = NULL;
	void *mc_misc = NULL;
	u8 *dmac_v = NULL;
	u8 *dmac_c = NULL;

	if (rx_rule)
		match_header |= MLX5_MATCH_MISC_PARAMETERS;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;

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
		MLX5_SET(fte_match_set_misc, mv_misc, source_port, MLX5_VPORT_UPLINK);
		MLX5_SET_TO_ONES(fte_match_set_misc, mc_misc, source_port);
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport.num = vport;

	esw_debug(esw->dev,
		  "\tFDB add rule dmac_v(%pM) dmac_c(%pM) -> vport(%d)\n",
		  dmac_v, dmac_c, vport);
	spec->match_criteria_enable = match_header;
	flow_act.action =  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule =
		mlx5_add_flow_rules(esw->fdb_table.legacy.fdb, spec,
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
esw_fdb_set_vport_rule(struct mlx5_eswitch *esw, u8 mac[ETH_ALEN], u16 vport)
{
	u8 mac_c[ETH_ALEN];

	eth_broadcast_addr(mac_c);
	return __esw_fdb_set_vport_rule(esw, vport, false, mac_c, mac);
}

static struct mlx5_flow_handle *
esw_fdb_set_vport_allmulti_rule(struct mlx5_eswitch *esw, u16 vport)
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
esw_fdb_set_vport_promisc_rule(struct mlx5_eswitch *esw, u16 vport)
{
	u8 mac_c[ETH_ALEN];
	u8 mac_v[ETH_ALEN];

	eth_zero_addr(mac_c);
	eth_zero_addr(mac_v);
	return __esw_fdb_set_vport_rule(esw, vport, true, mac_c, mac_v);
}

/* E-Switch vport UC/MC lists management */
typedef int (*vport_addr_action)(struct mlx5_eswitch *esw,
				 struct vport_addr *vaddr);

static int esw_add_uc_addr(struct mlx5_eswitch *esw, struct vport_addr *vaddr)
{
	u8 *mac = vaddr->node.addr;
	u16 vport = vaddr->vport;
	int err;

	/* Skip mlx5_mpfs_add_mac for eswitch_managers,
	 * it is already done by its netdev in mlx5e_execute_l2_action
	 */
	if (mlx5_esw_is_manager_vport(esw, vport))
		goto fdb_add;

	err = mlx5_mpfs_add_mac(esw->dev, mac);
	if (err) {
		esw_warn(esw->dev,
			 "Failed to add L2 table mac(%pM) for vport(0x%x), err(%d)\n",
			 mac, vport, err);
		return err;
	}
	vaddr->mpfs = true;

fdb_add:
	/* SRIOV is enabled: Forward UC MAC to vport */
	if (esw->fdb_table.legacy.fdb && esw->mode == MLX5_ESWITCH_LEGACY)
		vaddr->flow_rule = esw_fdb_set_vport_rule(esw, mac, vport);

	esw_debug(esw->dev, "\tADDED UC MAC: vport[%d] %pM fr(%p)\n",
		  vport, mac, vaddr->flow_rule);

	return 0;
}

static int esw_del_uc_addr(struct mlx5_eswitch *esw, struct vport_addr *vaddr)
{
	u8 *mac = vaddr->node.addr;
	u16 vport = vaddr->vport;
	int err = 0;

	/* Skip mlx5_mpfs_del_mac for eswitch managers,
	 * it is already done by its netdev in mlx5e_execute_l2_action
	 */
	if (!vaddr->mpfs || mlx5_esw_is_manager_vport(esw, vport))
		goto fdb_del;

	err = mlx5_mpfs_del_mac(esw->dev, mac);
	if (err)
		esw_warn(esw->dev,
			 "Failed to del L2 table mac(%pM) for vport(%d), err(%d)\n",
			 mac, vport, err);
	vaddr->mpfs = false;

fdb_del:
	if (vaddr->flow_rule)
		mlx5_del_flow_rules(vaddr->flow_rule);
	vaddr->flow_rule = NULL;

	return 0;
}

static void update_allmulti_vports(struct mlx5_eswitch *esw,
				   struct vport_addr *vaddr,
				   struct esw_mc_addr *esw_mc)
{
	u8 *mac = vaddr->node.addr;
	struct mlx5_vport *vport;
	unsigned long i;
	u16 vport_num;

	mlx5_esw_for_each_vport(esw, i, vport) {
		struct hlist_head *vport_hash = vport->mc_list;
		struct vport_addr *iter_vaddr =
					l2addr_hash_find(vport_hash,
							 mac,
							 struct vport_addr);
		vport_num = vport->vport;
		if (IS_ERR_OR_NULL(vport->allmulti_rule) ||
		    vaddr->vport == vport_num)
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
					 mac, vport_num);
				continue;
			}
			iter_vaddr->vport = vport_num;
			iter_vaddr->flow_rule =
					esw_fdb_set_vport_rule(esw,
							       mac,
							       vport_num);
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
	u16 vport = vaddr->vport;

	if (!esw->fdb_table.legacy.fdb)
		return 0;

	esw_mc = l2addr_hash_find(hash, mac, struct esw_mc_addr);
	if (esw_mc)
		goto add;

	esw_mc = l2addr_hash_add(hash, mac, struct esw_mc_addr, GFP_KERNEL);
	if (!esw_mc)
		return -ENOMEM;

	esw_mc->uplink_rule = /* Forward MC MAC to Uplink */
		esw_fdb_set_vport_rule(esw, mac, MLX5_VPORT_UPLINK);

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
	u16 vport = vaddr->vport;

	if (!esw->fdb_table.legacy.fdb)
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
				      struct mlx5_vport *vport, int list_type)
{
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
				       struct mlx5_vport *vport, int list_type)
{
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

	err = mlx5_query_nic_vport_mac_list(esw->dev, vport->vport, list_type,
					    mac_list, &size);
	if (err)
		goto out;
	esw_debug(esw->dev, "vport[%d] context update %s list size (%d)\n",
		  vport->vport, is_uc ? "UC" : "MC", size);

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
				 mac_list[i], vport->vport);
			continue;
		}
		addr->vport = vport->vport;
		addr->action = MLX5_ACTION_ADD;
	}
out:
	kfree(mac_list);
}

/* Sync vport UC/MC list from vport context
 * Must be called after esw_update_vport_addr_list
 */
static void esw_update_vport_mc_promisc(struct mlx5_eswitch *esw,
					struct mlx5_vport *vport)
{
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
				 mac, vport->vport);
			continue;
		}
		addr->vport = vport->vport;
		addr->action = MLX5_ACTION_ADD;
		addr->mc_promisc = true;
	}
}

/* Apply vport rx mode to HW FDB table */
static void esw_apply_vport_rx_mode(struct mlx5_eswitch *esw,
				    struct mlx5_vport *vport,
				    bool promisc, bool mc_promisc)
{
	struct esw_mc_addr *allmulti_addr = &esw->mc_promisc;

	if (IS_ERR_OR_NULL(vport->allmulti_rule) != mc_promisc)
		goto promisc;

	if (mc_promisc) {
		vport->allmulti_rule =
			esw_fdb_set_vport_allmulti_rule(esw, vport->vport);
		if (!allmulti_addr->uplink_rule)
			allmulti_addr->uplink_rule =
				esw_fdb_set_vport_allmulti_rule(esw,
								MLX5_VPORT_UPLINK);
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
		vport->promisc_rule =
			esw_fdb_set_vport_promisc_rule(esw, vport->vport);
	} else if (vport->promisc_rule) {
		mlx5_del_flow_rules(vport->promisc_rule);
		vport->promisc_rule = NULL;
	}
}

/* Sync vport rx mode from vport context */
static void esw_update_vport_rx_mode(struct mlx5_eswitch *esw,
				     struct mlx5_vport *vport)
{
	int promisc_all = 0;
	int promisc_uc = 0;
	int promisc_mc = 0;
	int err;

	err = mlx5_query_nic_vport_promisc(esw->dev,
					   vport->vport,
					   &promisc_uc,
					   &promisc_mc,
					   &promisc_all);
	if (err)
		return;
	esw_debug(esw->dev, "vport[%d] context update rx mode promisc_all=%d, all_multi=%d\n",
		  vport->vport, promisc_all, promisc_mc);

	if (!vport->info.trusted || !vport->enabled) {
		promisc_uc = 0;
		promisc_mc = 0;
		promisc_all = 0;
	}

	esw_apply_vport_rx_mode(esw, vport, promisc_all,
				(promisc_all || promisc_mc));
}

void esw_vport_change_handle_locked(struct mlx5_vport *vport)
{
	struct mlx5_core_dev *dev = vport->dev;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	u8 mac[ETH_ALEN];

	mlx5_query_nic_vport_mac_address(dev, vport->vport, true, mac);
	esw_debug(dev, "vport[%d] Context Changed: perm mac: %pM\n",
		  vport->vport, mac);

	if (vport->enabled_events & MLX5_VPORT_UC_ADDR_CHANGE) {
		esw_update_vport_addr_list(esw, vport, MLX5_NVPRT_LIST_TYPE_UC);
		esw_apply_vport_addr_list(esw, vport, MLX5_NVPRT_LIST_TYPE_UC);
	}

	if (vport->enabled_events & MLX5_VPORT_MC_ADDR_CHANGE)
		esw_update_vport_addr_list(esw, vport, MLX5_NVPRT_LIST_TYPE_MC);

	if (vport->enabled_events & MLX5_VPORT_PROMISC_CHANGE) {
		esw_update_vport_rx_mode(esw, vport);
		if (!IS_ERR_OR_NULL(vport->allmulti_rule))
			esw_update_vport_mc_promisc(esw, vport);
	}

	if (vport->enabled_events & (MLX5_VPORT_PROMISC_CHANGE | MLX5_VPORT_MC_ADDR_CHANGE))
		esw_apply_vport_addr_list(esw, vport, MLX5_NVPRT_LIST_TYPE_MC);

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

static void node_guid_gen_from_mac(u64 *node_guid, const u8 *mac)
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

static int esw_vport_setup_acl(struct mlx5_eswitch *esw,
			       struct mlx5_vport *vport)
{
	if (esw->mode == MLX5_ESWITCH_LEGACY)
		return esw_legacy_vport_acl_setup(esw, vport);
	else
		return esw_vport_create_offloads_acl_tables(esw, vport);
}

static void esw_vport_cleanup_acl(struct mlx5_eswitch *esw,
				  struct mlx5_vport *vport)
{
	if (esw->mode == MLX5_ESWITCH_LEGACY)
		esw_legacy_vport_acl_cleanup(esw, vport);
	else
		esw_vport_destroy_offloads_acl_tables(esw, vport);
}

static int mlx5_esw_vport_caps_get(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	int query_out_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *query_ctx;
	void *hca_caps;
	int err;

	if (!MLX5_CAP_GEN(esw->dev, vhca_resource_manager))
		return 0;

	query_ctx = kzalloc(query_out_sz, GFP_KERNEL);
	if (!query_ctx)
		return -ENOMEM;

	err = mlx5_vport_get_other_func_cap(esw->dev, vport->vport, query_ctx,
					    MLX5_CAP_GENERAL);
	if (err)
		goto out_free;

	hca_caps = MLX5_ADDR_OF(query_hca_cap_out, query_ctx, capability);
	vport->info.roce_enabled = MLX5_GET(cmd_hca_cap, hca_caps, roce);

	memset(query_ctx, 0, query_out_sz);
	err = mlx5_vport_get_other_func_cap(esw->dev, vport->vport, query_ctx,
					    MLX5_CAP_GENERAL_2);
	if (err)
		goto out_free;

	hca_caps = MLX5_ADDR_OF(query_hca_cap_out, query_ctx, capability);
	vport->info.mig_enabled = MLX5_GET(cmd_hca_cap_2, hca_caps, migratable);
out_free:
	kfree(query_ctx);
	return err;
}

static int esw_vport_setup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	bool vst_mode_steering = esw_vst_mode_is_steering(esw);
	u16 vport_num = vport->vport;
	int flags;
	int err;

	err = esw_vport_setup_acl(esw, vport);
	if (err)
		return err;

	if (mlx5_esw_is_manager_vport(esw, vport_num))
		return 0;

	err = mlx5_esw_vport_caps_get(esw, vport);
	if (err)
		goto err_caps;

	mlx5_modify_vport_admin_state(esw->dev,
				      MLX5_VPORT_STATE_OP_MOD_ESW_VPORT,
				      vport_num, 1,
				      vport->info.link_state);

	/* Host PF has its own mac/guid. */
	if (vport_num) {
		mlx5_modify_nic_vport_mac_address(esw->dev, vport_num,
						  vport->info.mac);
		mlx5_modify_nic_vport_node_guid(esw->dev, vport_num,
						vport->info.node_guid);
	}

	flags = (vport->info.vlan || vport->info.qos) ?
		SET_VLAN_STRIP | SET_VLAN_INSERT : 0;
	if (esw->mode == MLX5_ESWITCH_OFFLOADS || !vst_mode_steering)
		modify_esw_vport_cvlan(esw->dev, vport_num, vport->info.vlan,
				       vport->info.qos, flags);

	return 0;

err_caps:
	esw_vport_cleanup_acl(esw, vport);
	return err;
}

/* Don't cleanup vport->info, it's needed to restore vport configuration */
static void esw_vport_cleanup(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	u16 vport_num = vport->vport;

	if (!mlx5_esw_is_manager_vport(esw, vport_num))
		mlx5_modify_vport_admin_state(esw->dev,
					      MLX5_VPORT_STATE_OP_MOD_ESW_VPORT,
					      vport_num, 1,
					      MLX5_VPORT_ADMIN_STATE_DOWN);

	mlx5_esw_qos_vport_disable(esw, vport);
	esw_vport_cleanup_acl(esw, vport);
}

int mlx5_esw_vport_enable(struct mlx5_eswitch *esw, u16 vport_num,
			  enum mlx5_eswitch_vport_event enabled_events)
{
	struct mlx5_vport *vport;
	int ret;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return PTR_ERR(vport);

	mutex_lock(&esw->state_lock);
	WARN_ON(vport->enabled);

	esw_debug(esw->dev, "Enabling VPORT(%d)\n", vport_num);

	ret = esw_vport_setup(esw, vport);
	if (ret)
		goto done;

	/* Sync with current vport context */
	vport->enabled_events = enabled_events;
	vport->enabled = true;

	/* Esw manager is trusted by default. Host PF (vport 0) is trusted as well
	 * in smartNIC as it's a vport group manager.
	 */
	if (mlx5_esw_is_manager_vport(esw, vport_num) ||
	    (!vport_num && mlx5_core_is_ecpf(esw->dev)))
		vport->info.trusted = true;

	if (!mlx5_esw_is_manager_vport(esw, vport->vport) &&
	    MLX5_CAP_GEN(esw->dev, vhca_resource_manager)) {
		ret = mlx5_esw_vport_vhca_id_set(esw, vport_num);
		if (ret)
			goto err_vhca_mapping;
	}

	/* External controller host PF has factory programmed MAC.
	 * Read it from the device.
	 */
	if (mlx5_core_is_ecpf(esw->dev) && vport_num == MLX5_VPORT_PF)
		mlx5_query_nic_vport_mac_address(esw->dev, vport_num, true, vport->info.mac);

	esw_vport_change_handle_locked(vport);

	esw->enabled_vports++;
	esw_debug(esw->dev, "Enabled VPORT(%d)\n", vport_num);
done:
	mutex_unlock(&esw->state_lock);
	return ret;

err_vhca_mapping:
	esw_vport_cleanup(esw, vport);
	mutex_unlock(&esw->state_lock);
	return ret;
}

void mlx5_esw_vport_disable(struct mlx5_eswitch *esw, u16 vport_num)
{
	struct mlx5_vport *vport;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return;

	mutex_lock(&esw->state_lock);
	if (!vport->enabled)
		goto done;

	esw_debug(esw->dev, "Disabling vport(%d)\n", vport_num);
	/* Mark this vport as disabled to discard new events */
	vport->enabled = false;

	/* Disable events from this vport */
	arm_vport_context_events_cmd(esw->dev, vport->vport, 0);

	if (!mlx5_esw_is_manager_vport(esw, vport->vport) &&
	    MLX5_CAP_GEN(esw->dev, vhca_resource_manager))
		mlx5_esw_vport_vhca_id_clear(esw, vport_num);

	/* We don't assume VFs will cleanup after themselves.
	 * Calling vport change handler while vport is disabled will cleanup
	 * the vport resources.
	 */
	esw_vport_change_handle_locked(vport);
	vport->enabled_events = 0;
	esw_vport_cleanup(esw, vport);
	esw->enabled_vports--;

done:
	mutex_unlock(&esw->state_lock);
}

static int eswitch_vport_event(struct notifier_block *nb,
			       unsigned long type, void *data)
{
	struct mlx5_eswitch *esw = mlx5_nb_cof(nb, struct mlx5_eswitch, nb);
	struct mlx5_eqe *eqe = data;
	struct mlx5_vport *vport;
	u16 vport_num;

	vport_num = be16_to_cpu(eqe->data.vport_change.vport_num);
	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (!IS_ERR(vport))
		queue_work(esw->work_queue, &vport->vport_change_handler);
	return NOTIFY_OK;
}

/**
 * mlx5_esw_query_functions - Returns raw output about functions state
 * @dev:	Pointer to device to query
 *
 * mlx5_esw_query_functions() allocates and returns functions changed
 * raw output memory pointer from device on success. Otherwise returns ERR_PTR.
 * Caller must free the memory using kvfree() when valid pointer is returned.
 */
const u32 *mlx5_esw_query_functions(struct mlx5_core_dev *dev)
{
	int outlen = MLX5_ST_SZ_BYTES(query_esw_functions_out);
	u32 in[MLX5_ST_SZ_DW(query_esw_functions_in)] = {};
	u32 *out;
	int err;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(query_esw_functions_in, in, opcode,
		 MLX5_CMD_OP_QUERY_ESW_FUNCTIONS);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
	if (!err)
		return out;

	kvfree(out);
	return ERR_PTR(err);
}

static void mlx5_eswitch_event_handlers_register(struct mlx5_eswitch *esw)
{
	MLX5_NB_INIT(&esw->nb, eswitch_vport_event, NIC_VPORT_CHANGE);
	mlx5_eq_notifier_register(esw->dev, &esw->nb);

	if (esw->mode == MLX5_ESWITCH_OFFLOADS && mlx5_eswitch_is_funcs_handler(esw->dev)) {
		MLX5_NB_INIT(&esw->esw_funcs.nb, mlx5_esw_funcs_changed_handler,
			     ESW_FUNCTIONS_CHANGED);
		mlx5_eq_notifier_register(esw->dev, &esw->esw_funcs.nb);
	}
}

static void mlx5_eswitch_event_handlers_unregister(struct mlx5_eswitch *esw)
{
	if (esw->mode == MLX5_ESWITCH_OFFLOADS && mlx5_eswitch_is_funcs_handler(esw->dev))
		mlx5_eq_notifier_unregister(esw->dev, &esw->esw_funcs.nb);

	mlx5_eq_notifier_unregister(esw->dev, &esw->nb);

	flush_workqueue(esw->work_queue);
}

static void mlx5_eswitch_clear_vf_vports_info(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	unsigned long i;

	mlx5_esw_for_each_vf_vport(esw, i, vport, esw->esw_funcs.num_vfs) {
		memset(&vport->qos, 0, sizeof(vport->qos));
		memset(&vport->info, 0, sizeof(vport->info));
		vport->info.link_state = MLX5_VPORT_ADMIN_STATE_AUTO;
	}
}

/* Public E-Switch API */
int mlx5_eswitch_load_vport(struct mlx5_eswitch *esw, u16 vport_num,
			    enum mlx5_eswitch_vport_event enabled_events)
{
	int err;

	err = mlx5_esw_vport_enable(esw, vport_num, enabled_events);
	if (err)
		return err;

	mlx5_esw_vport_debugfs_create(esw, vport_num, false, 0);
	err = esw_offloads_load_rep(esw, vport_num);
	if (err)
		goto err_rep;

	return err;

err_rep:
	mlx5_esw_vport_debugfs_destroy(esw, vport_num);
	mlx5_esw_vport_disable(esw, vport_num);
	return err;
}

void mlx5_eswitch_unload_vport(struct mlx5_eswitch *esw, u16 vport_num)
{
	esw_offloads_unload_rep(esw, vport_num);
	mlx5_esw_vport_debugfs_destroy(esw, vport_num);
	mlx5_esw_vport_disable(esw, vport_num);
}

void mlx5_eswitch_unload_vf_vports(struct mlx5_eswitch *esw, u16 num_vfs)
{
	struct mlx5_vport *vport;
	unsigned long i;

	mlx5_esw_for_each_vf_vport(esw, i, vport, num_vfs) {
		if (!vport->enabled)
			continue;
		mlx5_eswitch_unload_vport(esw, vport->vport);
	}
}

int mlx5_eswitch_load_vf_vports(struct mlx5_eswitch *esw, u16 num_vfs,
				enum mlx5_eswitch_vport_event enabled_events)
{
	struct mlx5_vport *vport;
	unsigned long i;
	int err;

	mlx5_esw_for_each_vf_vport(esw, i, vport, num_vfs) {
		err = mlx5_eswitch_load_vport(esw, vport->vport, enabled_events);
		if (err)
			goto vf_err;
	}

	return 0;

vf_err:
	mlx5_eswitch_unload_vf_vports(esw, num_vfs);
	return err;
}

static int host_pf_enable_hca(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_ecpf(dev))
		return 0;

	/* Once vport and representor are ready, take out the external host PF
	 * out of initializing state. Enabling HCA clears the iser->initializing
	 * bit and host PF driver loading can progress.
	 */
	return mlx5_cmd_host_pf_enable_hca(dev);
}

static void host_pf_disable_hca(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_ecpf(dev))
		return;

	mlx5_cmd_host_pf_disable_hca(dev);
}

/* mlx5_eswitch_enable_pf_vf_vports() enables vports of PF, ECPF and VFs
 * whichever are present on the eswitch.
 */
int
mlx5_eswitch_enable_pf_vf_vports(struct mlx5_eswitch *esw,
				 enum mlx5_eswitch_vport_event enabled_events)
{
	int ret;

	/* Enable PF vport */
	ret = mlx5_eswitch_load_vport(esw, MLX5_VPORT_PF, enabled_events);
	if (ret)
		return ret;

	/* Enable external host PF HCA */
	ret = host_pf_enable_hca(esw->dev);
	if (ret)
		goto pf_hca_err;

	/* Enable ECPF vport */
	if (mlx5_ecpf_vport_exists(esw->dev)) {
		ret = mlx5_eswitch_load_vport(esw, MLX5_VPORT_ECPF, enabled_events);
		if (ret)
			goto ecpf_err;
	}

	/* Enable VF vports */
	ret = mlx5_eswitch_load_vf_vports(esw, esw->esw_funcs.num_vfs,
					  enabled_events);
	if (ret)
		goto vf_err;
	return 0;

vf_err:
	if (mlx5_ecpf_vport_exists(esw->dev))
		mlx5_eswitch_unload_vport(esw, MLX5_VPORT_ECPF);
ecpf_err:
	host_pf_disable_hca(esw->dev);
pf_hca_err:
	mlx5_eswitch_unload_vport(esw, MLX5_VPORT_PF);
	return ret;
}

/* mlx5_eswitch_disable_pf_vf_vports() disables vports of PF, ECPF and VFs
 * whichever are previously enabled on the eswitch.
 */
void mlx5_eswitch_disable_pf_vf_vports(struct mlx5_eswitch *esw)
{
	mlx5_eswitch_unload_vf_vports(esw, esw->esw_funcs.num_vfs);

	if (mlx5_ecpf_vport_exists(esw->dev))
		mlx5_eswitch_unload_vport(esw, MLX5_VPORT_ECPF);

	host_pf_disable_hca(esw->dev);
	mlx5_eswitch_unload_vport(esw, MLX5_VPORT_PF);
}

static void mlx5_eswitch_get_devlink_param(struct mlx5_eswitch *esw)
{
	struct devlink *devlink = priv_to_devlink(esw->dev);
	union devlink_param_value val;
	int err;

	err = devlink_param_driverinit_value_get(devlink,
						 MLX5_DEVLINK_PARAM_ID_ESW_LARGE_GROUP_NUM,
						 &val);
	if (!err) {
		esw->params.large_group_num = val.vu32;
	} else {
		esw_warn(esw->dev,
			 "Devlink can't get param fdb_large_groups, uses default (%d).\n",
			 ESW_OFFLOADS_DEFAULT_NUM_GROUPS);
		esw->params.large_group_num = ESW_OFFLOADS_DEFAULT_NUM_GROUPS;
	}
}

static void
mlx5_eswitch_update_num_of_vfs(struct mlx5_eswitch *esw, int num_vfs)
{
	const u32 *out;

	if (num_vfs < 0)
		return;

	if (!mlx5_core_is_ecpf_esw_manager(esw->dev)) {
		esw->esw_funcs.num_vfs = num_vfs;
		return;
	}

	out = mlx5_esw_query_functions(esw->dev);
	if (IS_ERR(out))
		return;

	esw->esw_funcs.num_vfs = MLX5_GET(query_esw_functions_out, out,
					  host_params_context.host_num_of_vfs);
	kvfree(out);
}

static void mlx5_esw_mode_change_notify(struct mlx5_eswitch *esw, u16 mode)
{
	struct mlx5_esw_event_info info = {};

	info.new_mode = mode;

	blocking_notifier_call_chain(&esw->n_head, 0, &info);
}

static int mlx5_esw_acls_ns_init(struct mlx5_eswitch *esw)
{
	struct mlx5_core_dev *dev = esw->dev;
	int total_vports;
	int err;

	if (esw->flags & MLX5_ESWITCH_VPORT_ACL_NS_CREATED)
		return 0;

	total_vports = mlx5_eswitch_get_total_vports(dev);

	if (MLX5_CAP_ESW_EGRESS_ACL(dev, ft_support)) {
		err = mlx5_fs_egress_acls_init(dev, total_vports);
		if (err)
			return err;
	} else {
		esw_warn(dev, "engress ACL is not supported by FW\n");
	}

	if (MLX5_CAP_ESW_INGRESS_ACL(dev, ft_support)) {
		err = mlx5_fs_ingress_acls_init(dev, total_vports);
		if (err)
			goto err;
	} else {
		esw_warn(dev, "ingress ACL is not supported by FW\n");
	}
	esw->flags |= MLX5_ESWITCH_VPORT_ACL_NS_CREATED;
	return 0;

err:
	if (MLX5_CAP_ESW_EGRESS_ACL(dev, ft_support))
		mlx5_fs_egress_acls_cleanup(dev);
	return err;
}

static void mlx5_esw_acls_ns_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_core_dev *dev = esw->dev;

	esw->flags &= ~MLX5_ESWITCH_VPORT_ACL_NS_CREATED;
	if (MLX5_CAP_ESW_INGRESS_ACL(dev, ft_support))
		mlx5_fs_ingress_acls_cleanup(dev);
	if (MLX5_CAP_ESW_EGRESS_ACL(dev, ft_support))
		mlx5_fs_egress_acls_cleanup(dev);
}

/**
 * mlx5_eswitch_enable_locked - Enable eswitch
 * @esw:	Pointer to eswitch
 * @num_vfs:	Enable eswitch for given number of VFs. This is optional.
 *		Valid value are 0, > 0 and MLX5_ESWITCH_IGNORE_NUM_VFS.
 *		Caller should pass num_vfs > 0 when enabling eswitch for
 *		vf vports. Caller should pass num_vfs = 0, when eswitch
 *		is enabled without sriov VFs or when caller
 *		is unaware of the sriov state of the host PF on ECPF based
 *		eswitch. Caller should pass < 0 when num_vfs should be
 *		completely ignored. This is typically the case when eswitch
 *		is enabled without sriov regardless of PF/ECPF system.
 * mlx5_eswitch_enable_locked() Enables eswitch in either legacy or offloads
 * mode. If num_vfs >=0 is provided, it setup VF related eswitch vports.
 * It returns 0 on success or error code on failure.
 */
int mlx5_eswitch_enable_locked(struct mlx5_eswitch *esw, int num_vfs)
{
	int err;

	lockdep_assert_held(&esw->mode_lock);

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, ft_support)) {
		esw_warn(esw->dev, "FDB is not supported, aborting ...\n");
		return -EOPNOTSUPP;
	}

	mlx5_eswitch_get_devlink_param(esw);

	err = mlx5_esw_acls_ns_init(esw);
	if (err)
		return err;

	mlx5_eswitch_update_num_of_vfs(esw, num_vfs);

	if (esw->mode == MLX5_ESWITCH_LEGACY) {
		err = esw_legacy_enable(esw);
	} else {
		mlx5_rescan_drivers(esw->dev);
		err = esw_offloads_enable(esw);
	}

	if (err)
		goto abort;

	esw->fdb_table.flags |= MLX5_ESW_FDB_CREATED;

	mlx5_eswitch_event_handlers_register(esw);

	esw_info(esw->dev, "Enable: mode(%s), nvfs(%d), active vports(%d)\n",
		 esw->mode == MLX5_ESWITCH_LEGACY ? "LEGACY" : "OFFLOADS",
		 esw->esw_funcs.num_vfs, esw->enabled_vports);

	mlx5_esw_mode_change_notify(esw, esw->mode);

	return 0;

abort:
	mlx5_esw_acls_ns_cleanup(esw);
	return err;
}

/**
 * mlx5_eswitch_enable - Enable eswitch
 * @esw:	Pointer to eswitch
 * @num_vfs:	Enable eswitch switch for given number of VFs.
 *		Caller must pass num_vfs > 0 when enabling eswitch for
 *		vf vports.
 * mlx5_eswitch_enable() returns 0 on success or error code on failure.
 */
int mlx5_eswitch_enable(struct mlx5_eswitch *esw, int num_vfs)
{
	bool toggle_lag;
	int ret;

	if (!mlx5_esw_allowed(esw))
		return 0;

	devl_assert_locked(priv_to_devlink(esw->dev));

	toggle_lag = !mlx5_esw_is_fdb_created(esw);

	if (toggle_lag)
		mlx5_lag_disable_change(esw->dev);

	down_write(&esw->mode_lock);
	if (!mlx5_esw_is_fdb_created(esw)) {
		ret = mlx5_eswitch_enable_locked(esw, num_vfs);
	} else {
		enum mlx5_eswitch_vport_event vport_events;

		vport_events = (esw->mode == MLX5_ESWITCH_LEGACY) ?
					MLX5_LEGACY_SRIOV_VPORT_EVENTS : MLX5_VPORT_UC_ADDR_CHANGE;
		ret = mlx5_eswitch_load_vf_vports(esw, num_vfs, vport_events);
		if (!ret)
			esw->esw_funcs.num_vfs = num_vfs;
	}
	up_write(&esw->mode_lock);

	if (toggle_lag)
		mlx5_lag_enable_change(esw->dev);

	return ret;
}

/* When disabling sriov, free driver level resources. */
void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw, bool clear_vf)
{
	if (!mlx5_esw_allowed(esw))
		return;

	devl_assert_locked(priv_to_devlink(esw->dev));
	down_write(&esw->mode_lock);
	/* If driver is unloaded, this function is called twice by remove_one()
	 * and mlx5_unload(). Prevent the second call.
	 */
	if (!esw->esw_funcs.num_vfs && !clear_vf)
		goto unlock;

	esw_info(esw->dev, "Unload vfs: mode(%s), nvfs(%d), active vports(%d)\n",
		 esw->mode == MLX5_ESWITCH_LEGACY ? "LEGACY" : "OFFLOADS",
		 esw->esw_funcs.num_vfs, esw->enabled_vports);

	mlx5_eswitch_unload_vf_vports(esw, esw->esw_funcs.num_vfs);
	if (clear_vf)
		mlx5_eswitch_clear_vf_vports_info(esw);
	/* If disabling sriov in switchdev mode, free meta rules here
	 * because it depends on num_vfs.
	 */
	if (esw->mode == MLX5_ESWITCH_OFFLOADS) {
		struct devlink *devlink = priv_to_devlink(esw->dev);

		devl_rate_nodes_destroy(devlink);
	}
	/* Destroy legacy fdb when disabling sriov in legacy mode. */
	if (esw->mode == MLX5_ESWITCH_LEGACY)
		mlx5_eswitch_disable_locked(esw);

	esw->esw_funcs.num_vfs = 0;

unlock:
	up_write(&esw->mode_lock);
}

/* Free resources for corresponding eswitch mode. It is called by devlink
 * when changing eswitch mode or modprobe when unloading driver.
 */
void mlx5_eswitch_disable_locked(struct mlx5_eswitch *esw)
{
	struct devlink *devlink = priv_to_devlink(esw->dev);

	/* Notify eswitch users that it is exiting from current mode.
	 * So that it can do necessary cleanup before the eswitch is disabled.
	 */
	mlx5_esw_mode_change_notify(esw, MLX5_ESWITCH_LEGACY);

	mlx5_eswitch_event_handlers_unregister(esw);

	esw_info(esw->dev, "Disable: mode(%s), nvfs(%d), active vports(%d)\n",
		 esw->mode == MLX5_ESWITCH_LEGACY ? "LEGACY" : "OFFLOADS",
		 esw->esw_funcs.num_vfs, esw->enabled_vports);

	if (esw->fdb_table.flags & MLX5_ESW_FDB_CREATED) {
		esw->fdb_table.flags &= ~MLX5_ESW_FDB_CREATED;
		if (esw->mode == MLX5_ESWITCH_OFFLOADS)
			esw_offloads_disable(esw);
		else if (esw->mode == MLX5_ESWITCH_LEGACY)
			esw_legacy_disable(esw);
		mlx5_esw_acls_ns_cleanup(esw);
	}

	if (esw->mode == MLX5_ESWITCH_OFFLOADS)
		devl_rate_nodes_destroy(devlink);
}

void mlx5_eswitch_disable(struct mlx5_eswitch *esw)
{
	if (!mlx5_esw_allowed(esw))
		return;

	devl_assert_locked(priv_to_devlink(esw->dev));
	mlx5_lag_disable_change(esw->dev);
	down_write(&esw->mode_lock);
	mlx5_eswitch_disable_locked(esw);
	esw->mode = MLX5_ESWITCH_LEGACY;
	up_write(&esw->mode_lock);
	mlx5_lag_enable_change(esw->dev);
}

static int mlx5_query_hca_cap_host_pf(struct mlx5_core_dev *dev, void *out)
{
	u16 opmod = (MLX5_CAP_GENERAL << 1) | (HCA_CAP_OPMOD_GET_MAX & 0x01);
	u8 in[MLX5_ST_SZ_BYTES(query_hca_cap_in)] = {};

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, op_mod, opmod);
	MLX5_SET(query_hca_cap_in, in, function_id, MLX5_VPORT_PF);
	MLX5_SET(query_hca_cap_in, in, other_function, true);
	return mlx5_cmd_exec_inout(dev, query_hca_cap, in, out);
}

int mlx5_esw_sf_max_hpf_functions(struct mlx5_core_dev *dev, u16 *max_sfs, u16 *sf_base_id)

{
	int query_out_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *query_ctx;
	void *hca_caps;
	int err;

	if (!mlx5_core_is_ecpf(dev)) {
		*max_sfs = 0;
		return 0;
	}

	query_ctx = kzalloc(query_out_sz, GFP_KERNEL);
	if (!query_ctx)
		return -ENOMEM;

	err = mlx5_query_hca_cap_host_pf(dev, query_ctx);
	if (err)
		goto out_free;

	hca_caps = MLX5_ADDR_OF(query_hca_cap_out, query_ctx, capability);
	*max_sfs = MLX5_GET(cmd_hca_cap, hca_caps, max_num_sf);
	*sf_base_id = MLX5_GET(cmd_hca_cap, hca_caps, sf_base_id);

out_free:
	kfree(query_ctx);
	return err;
}

static int mlx5_esw_vport_alloc(struct mlx5_eswitch *esw, struct mlx5_core_dev *dev,
				int index, u16 vport_num)
{
	struct mlx5_vport *vport;
	int err;

	vport = kzalloc(sizeof(*vport), GFP_KERNEL);
	if (!vport)
		return -ENOMEM;

	vport->dev = esw->dev;
	vport->vport = vport_num;
	vport->index = index;
	vport->info.link_state = MLX5_VPORT_ADMIN_STATE_AUTO;
	INIT_WORK(&vport->vport_change_handler, esw_vport_change_handler);
	err = xa_insert(&esw->vports, vport_num, vport, GFP_KERNEL);
	if (err)
		goto insert_err;

	esw->total_vports++;
	return 0;

insert_err:
	kfree(vport);
	return err;
}

static void mlx5_esw_vport_free(struct mlx5_eswitch *esw, struct mlx5_vport *vport)
{
	xa_erase(&esw->vports, vport->vport);
	kfree(vport);
}

static void mlx5_esw_vports_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	unsigned long i;

	mlx5_esw_for_each_vport(esw, i, vport)
		mlx5_esw_vport_free(esw, vport);
	xa_destroy(&esw->vports);
}

static int mlx5_esw_vports_init(struct mlx5_eswitch *esw)
{
	struct mlx5_core_dev *dev = esw->dev;
	u16 max_host_pf_sfs;
	u16 base_sf_num;
	int idx = 0;
	int err;
	int i;

	xa_init(&esw->vports);

	err = mlx5_esw_vport_alloc(esw, dev, idx, MLX5_VPORT_PF);
	if (err)
		goto err;
	if (esw->first_host_vport == MLX5_VPORT_PF)
		xa_set_mark(&esw->vports, idx, MLX5_ESW_VPT_HOST_FN);
	idx++;

	for (i = 0; i < mlx5_core_max_vfs(dev); i++) {
		err = mlx5_esw_vport_alloc(esw, dev, idx, idx);
		if (err)
			goto err;
		xa_set_mark(&esw->vports, idx, MLX5_ESW_VPT_VF);
		xa_set_mark(&esw->vports, idx, MLX5_ESW_VPT_HOST_FN);
		idx++;
	}
	base_sf_num = mlx5_sf_start_function_id(dev);
	for (i = 0; i < mlx5_sf_max_functions(dev); i++) {
		err = mlx5_esw_vport_alloc(esw, dev, idx, base_sf_num + i);
		if (err)
			goto err;
		xa_set_mark(&esw->vports, base_sf_num + i, MLX5_ESW_VPT_SF);
		idx++;
	}

	err = mlx5_esw_sf_max_hpf_functions(dev, &max_host_pf_sfs, &base_sf_num);
	if (err)
		goto err;
	for (i = 0; i < max_host_pf_sfs; i++) {
		err = mlx5_esw_vport_alloc(esw, dev, idx, base_sf_num + i);
		if (err)
			goto err;
		xa_set_mark(&esw->vports, base_sf_num + i, MLX5_ESW_VPT_SF);
		idx++;
	}

	if (mlx5_ecpf_vport_exists(dev)) {
		err = mlx5_esw_vport_alloc(esw, dev, idx, MLX5_VPORT_ECPF);
		if (err)
			goto err;
		idx++;
	}
	err = mlx5_esw_vport_alloc(esw, dev, idx, MLX5_VPORT_UPLINK);
	if (err)
		goto err;
	return 0;

err:
	mlx5_esw_vports_cleanup(esw);
	return err;
}

int mlx5_eswitch_init(struct mlx5_core_dev *dev)
{
	struct mlx5_eswitch *esw;
	int err;

	if (!MLX5_VPORT_MANAGER(dev))
		return 0;

	esw = kzalloc(sizeof(*esw), GFP_KERNEL);
	if (!esw)
		return -ENOMEM;

	esw->dev = dev;
	esw->manager_vport = mlx5_eswitch_manager_vport(dev);
	esw->first_host_vport = mlx5_eswitch_first_host_vport_num(dev);

	esw->work_queue = create_singlethread_workqueue("mlx5_esw_wq");
	if (!esw->work_queue) {
		err = -ENOMEM;
		goto abort;
	}

	err = mlx5_esw_vports_init(esw);
	if (err)
		goto abort;

	err = esw_offloads_init_reps(esw);
	if (err)
		goto reps_err;

	mutex_init(&esw->offloads.encap_tbl_lock);
	hash_init(esw->offloads.encap_tbl);
	mutex_init(&esw->offloads.decap_tbl_lock);
	hash_init(esw->offloads.decap_tbl);
	mlx5e_mod_hdr_tbl_init(&esw->offloads.mod_hdr);
	atomic64_set(&esw->offloads.num_flows, 0);
	ida_init(&esw->offloads.vport_metadata_ida);
	xa_init_flags(&esw->offloads.vhca_map, XA_FLAGS_ALLOC);
	mutex_init(&esw->state_lock);
	init_rwsem(&esw->mode_lock);
	refcount_set(&esw->qos.refcnt, 0);

	esw->enabled_vports = 0;
	esw->mode = MLX5_ESWITCH_LEGACY;
	esw->offloads.inline_mode = MLX5_INLINE_MODE_NONE;
	if (MLX5_CAP_ESW_FLOWTABLE_FDB(dev, reformat) &&
	    MLX5_CAP_ESW_FLOWTABLE_FDB(dev, decap))
		esw->offloads.encap = DEVLINK_ESWITCH_ENCAP_MODE_BASIC;
	else
		esw->offloads.encap = DEVLINK_ESWITCH_ENCAP_MODE_NONE;
	if (MLX5_ESWITCH_MANAGER(dev) &&
	    mlx5_esw_vport_match_metadata_supported(esw))
		esw->flags |= MLX5_ESWITCH_VPORT_MATCH_METADATA;

	dev->priv.eswitch = esw;
	BLOCKING_INIT_NOTIFIER_HEAD(&esw->n_head);

	esw->dbgfs = debugfs_create_dir("esw", mlx5_debugfs_get_dev_root(esw->dev));
	esw_info(dev,
		 "Total vports %d, per vport: max uc(%d) max mc(%d)\n",
		 esw->total_vports,
		 MLX5_MAX_UC_PER_VPORT(dev),
		 MLX5_MAX_MC_PER_VPORT(dev));
	return 0;

reps_err:
	mlx5_esw_vports_cleanup(esw);
abort:
	if (esw->work_queue)
		destroy_workqueue(esw->work_queue);
	kfree(esw);
	return err;
}

void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw)
{
	if (!esw || !MLX5_VPORT_MANAGER(esw->dev))
		return;

	esw_info(esw->dev, "cleanup\n");

	debugfs_remove_recursive(esw->dbgfs);
	esw->dev->priv.eswitch = NULL;
	destroy_workqueue(esw->work_queue);
	WARN_ON(refcount_read(&esw->qos.refcnt));
	mutex_destroy(&esw->state_lock);
	WARN_ON(!xa_empty(&esw->offloads.vhca_map));
	xa_destroy(&esw->offloads.vhca_map);
	ida_destroy(&esw->offloads.vport_metadata_ida);
	mlx5e_mod_hdr_tbl_destroy(&esw->offloads.mod_hdr);
	mutex_destroy(&esw->offloads.encap_tbl_lock);
	mutex_destroy(&esw->offloads.decap_tbl_lock);
	esw_offloads_cleanup_reps(esw);
	mlx5_esw_vports_cleanup(esw);
	kfree(esw);
}

/* Vport Administration */
static int
mlx5_esw_set_vport_mac_locked(struct mlx5_eswitch *esw,
			      struct mlx5_vport *evport, const u8 *mac)
{
	u16 vport_num = evport->vport;
	u64 node_guid;
	int err = 0;

	if (is_multicast_ether_addr(mac))
		return -EINVAL;

	if (evport->info.spoofchk && !is_valid_ether_addr(mac))
		mlx5_core_warn(esw->dev,
			       "Set invalid MAC while spoofchk is on, vport(%d)\n",
			       vport_num);

	err = mlx5_modify_nic_vport_mac_address(esw->dev, vport_num, mac);
	if (err) {
		mlx5_core_warn(esw->dev,
			       "Failed to mlx5_modify_nic_vport_mac vport(%d) err=(%d)\n",
			       vport_num, err);
		return err;
	}

	node_guid_gen_from_mac(&node_guid, mac);
	err = mlx5_modify_nic_vport_node_guid(esw->dev, vport_num, node_guid);
	if (err)
		mlx5_core_warn(esw->dev,
			       "Failed to set vport %d node guid, err = %d. RDMA_CM will not function properly for this VF.\n",
			       vport_num, err);

	ether_addr_copy(evport->info.mac, mac);
	evport->info.node_guid = node_guid;
	if (evport->enabled && esw->mode == MLX5_ESWITCH_LEGACY)
		err = esw_acl_ingress_lgcy_setup(esw, evport);

	return err;
}

int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       u16 vport, const u8 *mac)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);
	int err = 0;

	if (IS_ERR(evport))
		return PTR_ERR(evport);

	mutex_lock(&esw->state_lock);
	err = mlx5_esw_set_vport_mac_locked(esw, evport, mac);
	mutex_unlock(&esw->state_lock);
	return err;
}

static bool mlx5_esw_check_port_type(struct mlx5_eswitch *esw, u16 vport_num, xa_mark_t mark)
{
	struct mlx5_vport *vport;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return false;

	return xa_get_mark(&esw->vports, vport_num, mark);
}

bool mlx5_eswitch_is_vf_vport(struct mlx5_eswitch *esw, u16 vport_num)
{
	return mlx5_esw_check_port_type(esw, vport_num, MLX5_ESW_VPT_VF);
}

bool mlx5_esw_is_sf_vport(struct mlx5_eswitch *esw, u16 vport_num)
{
	return mlx5_esw_check_port_type(esw, vport_num, MLX5_ESW_VPT_SF);
}

int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 u16 vport, int link_state)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);
	int opmod = MLX5_VPORT_STATE_OP_MOD_ESW_VPORT;
	int other_vport = 1;
	int err = 0;

	if (!mlx5_esw_allowed(esw))
		return -EPERM;
	if (IS_ERR(evport))
		return PTR_ERR(evport);

	if (vport == MLX5_VPORT_UPLINK) {
		opmod = MLX5_VPORT_STATE_OP_MOD_UPLINK;
		other_vport = 0;
		vport = 0;
	}
	mutex_lock(&esw->state_lock);
	if (esw->mode != MLX5_ESWITCH_LEGACY) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	err = mlx5_modify_vport_admin_state(esw->dev, opmod, vport, other_vport, link_state);
	if (err) {
		mlx5_core_warn(esw->dev, "Failed to set vport %d link state, opmod = %d, err = %d",
			       vport, opmod, err);
		goto unlock;
	}

	evport->info.link_state = link_state;

unlock:
	mutex_unlock(&esw->state_lock);
	return err;
}

int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  u16 vport, struct ifla_vf_info *ivi)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);

	if (IS_ERR(evport))
		return PTR_ERR(evport);

	memset(ivi, 0, sizeof(*ivi));
	ivi->vf = vport - 1;

	mutex_lock(&esw->state_lock);
	ether_addr_copy(ivi->mac, evport->info.mac);
	ivi->linkstate = evport->info.link_state;
	ivi->vlan = evport->info.vlan;
	ivi->qos = evport->info.qos;
	ivi->spoofchk = evport->info.spoofchk;
	ivi->trusted = evport->info.trusted;
	if (evport->qos.enabled) {
		ivi->min_tx_rate = evport->qos.min_rate;
		ivi->max_tx_rate = evport->qos.max_rate;
	}
	mutex_unlock(&esw->state_lock);

	return 0;
}

int __mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				  u16 vport, u16 vlan, u8 qos, u8 set_flags)
{
	struct mlx5_vport *evport = mlx5_eswitch_get_vport(esw, vport);
	bool vst_mode_steering = esw_vst_mode_is_steering(esw);
	int err = 0;

	if (IS_ERR(evport))
		return PTR_ERR(evport);
	if (vlan > 4095 || qos > 7)
		return -EINVAL;

	if (esw->mode == MLX5_ESWITCH_OFFLOADS || !vst_mode_steering) {
		err = modify_esw_vport_cvlan(esw->dev, vport, vlan, qos, set_flags);
		if (err)
			return err;
	}

	evport->info.vlan = vlan;
	evport->info.qos = qos;
	if (evport->enabled && esw->mode == MLX5_ESWITCH_LEGACY) {
		err = esw_acl_ingress_lgcy_setup(esw, evport);
		if (err)
			return err;
		err = esw_acl_egress_lgcy_setup(esw, evport);
	}

	return err;
}

int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 u16 vport_num,
				 struct ifla_vf_stats *vf_stats)
{
	struct mlx5_vport *vport = mlx5_eswitch_get_vport(esw, vport_num);
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)] = {};
	struct mlx5_vport_drop_stats stats = {};
	int err = 0;
	u32 *out;

	if (IS_ERR(vport))
		return PTR_ERR(vport);

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_vport_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	MLX5_SET(query_vport_counter_in, in, op_mod, 0);
	MLX5_SET(query_vport_counter_in, in, vport_number, vport->vport);
	MLX5_SET(query_vport_counter_in, in, other_vport, 1);

	err = mlx5_cmd_exec_inout(esw->dev, query_vport_counter, in, out);
	if (err)
		goto free_out;

	#define MLX5_GET_CTR(p, x) \
		MLX5_GET64(query_vport_counter_out, p, x)

	memset(vf_stats, 0, sizeof(*vf_stats));
	vf_stats->rx_packets =
		MLX5_GET_CTR(out, received_eth_unicast.packets) +
		MLX5_GET_CTR(out, received_ib_unicast.packets) +
		MLX5_GET_CTR(out, received_eth_multicast.packets) +
		MLX5_GET_CTR(out, received_ib_multicast.packets) +
		MLX5_GET_CTR(out, received_eth_broadcast.packets);

	vf_stats->rx_bytes =
		MLX5_GET_CTR(out, received_eth_unicast.octets) +
		MLX5_GET_CTR(out, received_ib_unicast.octets) +
		MLX5_GET_CTR(out, received_eth_multicast.octets) +
		MLX5_GET_CTR(out, received_ib_multicast.octets) +
		MLX5_GET_CTR(out, received_eth_broadcast.octets);

	vf_stats->tx_packets =
		MLX5_GET_CTR(out, transmitted_eth_unicast.packets) +
		MLX5_GET_CTR(out, transmitted_ib_unicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_multicast.packets) +
		MLX5_GET_CTR(out, transmitted_ib_multicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_broadcast.packets);

	vf_stats->tx_bytes =
		MLX5_GET_CTR(out, transmitted_eth_unicast.octets) +
		MLX5_GET_CTR(out, transmitted_ib_unicast.octets) +
		MLX5_GET_CTR(out, transmitted_eth_multicast.octets) +
		MLX5_GET_CTR(out, transmitted_ib_multicast.octets) +
		MLX5_GET_CTR(out, transmitted_eth_broadcast.octets);

	vf_stats->multicast =
		MLX5_GET_CTR(out, received_eth_multicast.packets) +
		MLX5_GET_CTR(out, received_ib_multicast.packets);

	vf_stats->broadcast =
		MLX5_GET_CTR(out, received_eth_broadcast.packets);

	err = mlx5_esw_query_vport_drop_stats(esw->dev, vport, &stats);
	if (err)
		goto free_out;
	vf_stats->rx_dropped = stats.rx_dropped;
	vf_stats->tx_dropped = stats.tx_dropped;

free_out:
	kvfree(out);
	return err;
}

u8 mlx5_eswitch_mode(const struct mlx5_core_dev *dev)
{
	struct mlx5_eswitch *esw = dev->priv.eswitch;

	return mlx5_esw_allowed(esw) ? esw->mode : MLX5_ESWITCH_LEGACY;
}
EXPORT_SYMBOL_GPL(mlx5_eswitch_mode);

enum devlink_eswitch_encap_mode
mlx5_eswitch_get_encap_mode(const struct mlx5_core_dev *dev)
{
	struct mlx5_eswitch *esw;

	esw = dev->priv.eswitch;
	return (mlx5_eswitch_mode(dev) == MLX5_ESWITCH_OFFLOADS)  ? esw->offloads.encap :
		DEVLINK_ESWITCH_ENCAP_MODE_NONE;
}
EXPORT_SYMBOL(mlx5_eswitch_get_encap_mode);

bool mlx5_esw_multipath_prereq(struct mlx5_core_dev *dev0,
			       struct mlx5_core_dev *dev1)
{
	return (dev0->priv.eswitch->mode == MLX5_ESWITCH_OFFLOADS &&
		dev1->priv.eswitch->mode == MLX5_ESWITCH_OFFLOADS);
}

int mlx5_esw_event_notifier_register(struct mlx5_eswitch *esw, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&esw->n_head, nb);
}

void mlx5_esw_event_notifier_unregister(struct mlx5_eswitch *esw, struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&esw->n_head, nb);
}

/**
 * mlx5_esw_hold() - Try to take a read lock on esw mode lock.
 * @mdev: mlx5 core device.
 *
 * Should be called by esw resources callers.
 *
 * Return: true on success or false.
 */
bool mlx5_esw_hold(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	/* e.g. VF doesn't have eswitch so nothing to do */
	if (!mlx5_esw_allowed(esw))
		return true;

	if (down_read_trylock(&esw->mode_lock) != 0)
		return true;

	return false;
}

/**
 * mlx5_esw_release() - Release a read lock on esw mode lock.
 * @mdev: mlx5 core device.
 */
void mlx5_esw_release(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	if (mlx5_esw_allowed(esw))
		up_read(&esw->mode_lock);
}

/**
 * mlx5_esw_get() - Increase esw user count.
 * @mdev: mlx5 core device.
 */
void mlx5_esw_get(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	if (mlx5_esw_allowed(esw))
		atomic64_inc(&esw->user_count);
}

/**
 * mlx5_esw_put() - Decrease esw user count.
 * @mdev: mlx5 core device.
 */
void mlx5_esw_put(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	if (mlx5_esw_allowed(esw))
		atomic64_dec_if_positive(&esw->user_count);
}

/**
 * mlx5_esw_try_lock() - Take a write lock on esw mode lock.
 * @esw: eswitch device.
 *
 * Should be called by esw mode change routine.
 *
 * Return:
 * * 0       - esw mode if successfully locked and refcount is 0.
 * * -EBUSY  - refcount is not 0.
 * * -EINVAL - In the middle of switching mode or lock is already held.
 */
int mlx5_esw_try_lock(struct mlx5_eswitch *esw)
{
	if (down_write_trylock(&esw->mode_lock) == 0)
		return -EINVAL;

	if (atomic64_read(&esw->user_count) > 0) {
		up_write(&esw->mode_lock);
		return -EBUSY;
	}

	return esw->mode;
}

/**
 * mlx5_esw_unlock() - Release write lock on esw mode lock
 * @esw: eswitch device.
 */
void mlx5_esw_unlock(struct mlx5_eswitch *esw)
{
	up_write(&esw->mode_lock);
}

/**
 * mlx5_eswitch_get_total_vports - Get total vports of the eswitch
 *
 * @dev: Pointer to core device
 *
 * mlx5_eswitch_get_total_vports returns total number of eswitch vports.
 */
u16 mlx5_eswitch_get_total_vports(const struct mlx5_core_dev *dev)
{
	struct mlx5_eswitch *esw;

	esw = dev->priv.eswitch;
	return mlx5_esw_allowed(esw) ? esw->total_vports : 0;
}
EXPORT_SYMBOL_GPL(mlx5_eswitch_get_total_vports);

/**
 * mlx5_eswitch_get_core_dev - Get the mdev device
 * @esw : eswitch device.
 *
 * Return the mellanox core device which manages the eswitch.
 */
struct mlx5_core_dev *mlx5_eswitch_get_core_dev(struct mlx5_eswitch *esw)
{
	return mlx5_esw_allowed(esw) ? esw->dev : NULL;
}
EXPORT_SYMBOL(mlx5_eswitch_get_core_dev);
