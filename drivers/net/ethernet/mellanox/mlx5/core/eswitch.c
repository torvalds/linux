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
#include "mlx5_core.h"
#include "eswitch.h"

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

/* HW UC L2 table hash node */
struct mlx5_uc_l2addr {
	struct l2addr_node node;
	u8                 action;
	u32                table_index;
	u32                vport;
};

/* Vport UC L2 table hash node */
struct mlx5_vport_addr {
	struct l2addr_node node;
	u8                 action;
};

enum {
	UC_ADDR_CHANGE = BIT(0),
	MC_ADDR_CHANGE = BIT(1),
};

static int arm_vport_context_events_cmd(struct mlx5_core_dev *dev, int vport,
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

/* SW E-Switch L2 Table management */
static int l2_table_addr_add(struct mlx5_eswitch *esw,
			     u8 mac[ETH_ALEN], u32 vport)
{
	struct hlist_head *hash;
	struct mlx5_uc_l2addr *addr;
	int err;

	hash = esw->l2_table.l2_hash;
	addr = l2addr_hash_find(hash, mac, struct mlx5_uc_l2addr);
	if (addr) {
		esw_warn(esw->dev,
			 "Failed to set L2 mac(%pM) for vport(%d), mac is already in use by vport(%d)\n",
			 mac, vport, addr->vport);
		return -EEXIST;
	}

	addr = l2addr_hash_add(hash, mac, struct mlx5_uc_l2addr,
			       GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	addr->vport = vport;
	addr->action = MLX5_ACTION_NONE;
	err = set_l2_table_entry(esw->dev, mac, 0, 0, &addr->table_index);
	if (err)
		l2addr_hash_del(addr);
	else
		esw_debug(esw->dev, "\tADDED L2 MAC: vport[%d] %pM index:%d\n",
			  vport, addr->node.addr, addr->table_index);
	return err;
}

static int l2_table_addr_del(struct mlx5_eswitch *esw,
			     u8 mac[ETH_ALEN], u32 vport)
{
	struct hlist_head *hash;
	struct mlx5_uc_l2addr *addr;

	hash = esw->l2_table.l2_hash;
	addr = l2addr_hash_find(hash, mac, struct mlx5_uc_l2addr);
	if (!addr || addr->vport != vport) {
		esw_warn(esw->dev, "MAC(%pM) doesn't belong to vport (%d)\n",
			 mac, vport);
		return -EINVAL;
	}

	esw_debug(esw->dev, "\tDELETE L2 MAC: vport[%d] %pM index:%d\n",
		  vport, addr->node.addr, addr->table_index);
	del_l2_table_entry(esw->dev, addr->table_index);
	l2addr_hash_del(addr);
	return 0;
}

/* E-Switch vport uc list management */

/* Apply vport uc list to HW l2 table */
static void esw_apply_vport_uc_list(struct mlx5_eswitch *esw,
				    u32 vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	struct mlx5_vport_addr *addr;
	struct l2addr_node *node;
	struct hlist_head *hash;
	struct hlist_node *tmp;
	int hi;

	hash = vport->uc_list;
	for_each_l2hash_node(node, tmp, hash, hi) {
		addr = container_of(node, struct mlx5_vport_addr, node);
		switch (addr->action) {
		case MLX5_ACTION_ADD:
			l2_table_addr_add(esw, addr->node.addr, vport_num);
			addr->action = MLX5_ACTION_NONE;
			break;
		case MLX5_ACTION_DEL:
			l2_table_addr_del(esw, addr->node.addr, vport_num);
			l2addr_hash_del(addr);
			break;
		}
	}
}

/* Sync vport uc list from vport context */
static void esw_update_vport_uc_list(struct mlx5_eswitch *esw,
				     u32 vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	struct mlx5_vport_addr *addr;
	struct l2addr_node *node;
	u8 (*mac_list)[ETH_ALEN];
	struct hlist_head *hash;
	struct hlist_node *tmp;
	int size;
	int err;
	int hi;
	int i;

	size = MLX5_MAX_UC_PER_VPORT(esw->dev);

	mac_list = kcalloc(size, ETH_ALEN, GFP_KERNEL);
	if (!mac_list)
		return;

	hash = vport->uc_list;

	for_each_l2hash_node(node, tmp, hash, hi) {
		addr = container_of(node, struct mlx5_vport_addr, node);
		addr->action = MLX5_ACTION_DEL;
	}

	err = mlx5_query_nic_vport_mac_list(esw->dev, vport_num,
					    MLX5_NVPRT_LIST_TYPE_UC,
					    mac_list, &size);
	if (err)
		return;
	esw_debug(esw->dev, "vport[%d] context update UC list size (%d)\n",
		  vport_num, size);

	for (i = 0; i < size; i++) {
		if (!is_valid_ether_addr(mac_list[i]))
			continue;

		addr = l2addr_hash_find(hash, mac_list[i],
					struct mlx5_vport_addr);
		if (addr) {
			addr->action = MLX5_ACTION_NONE;
			continue;
		}

		addr = l2addr_hash_add(hash, mac_list[i],
				       struct mlx5_vport_addr,
				       GFP_KERNEL);
		if (!addr) {
			esw_warn(esw->dev,
				 "Failed to add MAC(%pM) to vport[%d] DB\n",
				 mac_list[i], vport_num);
			continue;
		}
		addr->action = MLX5_ACTION_ADD;
	}
	kfree(mac_list);
}

static void esw_vport_change_handler(struct work_struct *work)
{
	struct mlx5_vport *vport =
		container_of(work, struct mlx5_vport, vport_change_handler);
	struct mlx5_core_dev *dev = vport->dev;
	u8 mac[ETH_ALEN];

	mlx5_query_nic_vport_mac_address(dev, vport->vport, mac);

	if (!is_valid_ether_addr(mac))
		goto out;

	esw_update_vport_uc_list(dev->priv.eswitch, vport->vport);
	esw_apply_vport_uc_list(dev->priv.eswitch, vport->vport);

out:
	if (vport->enabled)
		arm_vport_context_events_cmd(dev, vport->vport,
					     UC_ADDR_CHANGE);
}

static void esw_enable_vport(struct mlx5_eswitch *esw, int vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	unsigned long flags;

	spin_lock_irqsave(&vport->lock, flags);
	vport->enabled = true;
	spin_unlock_irqrestore(&vport->lock, flags);

	arm_vport_context_events_cmd(esw->dev, vport_num, UC_ADDR_CHANGE);
}

static void esw_disable_vport(struct mlx5_eswitch *esw, int vport_num)
{
	struct mlx5_vport *vport = &esw->vports[vport_num];
	unsigned long flags;

	if (!vport->enabled)
		return;

	/* Mark this vport as disabled to discard new events */
	spin_lock_irqsave(&vport->lock, flags);
	vport->enabled = false;
	spin_unlock_irqrestore(&vport->lock, flags);

	/* Wait for current already scheduled events to complete */
	flush_workqueue(esw->work_queue);

	/* Disable events from this vport */
	arm_vport_context_events_cmd(esw->dev, vport->vport, 0);
}

/* Public E-Switch API */
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

	esw->total_vports = total_vports;
	for (vport_num = 0; vport_num < total_vports; vport_num++) {
		struct mlx5_vport *vport = &esw->vports[vport_num];

		vport->vport = vport_num;
		vport->dev = dev;
		INIT_WORK(&vport->vport_change_handler,
			  esw_vport_change_handler);
		spin_lock_init(&vport->lock);
	}

	dev->priv.eswitch = esw;

	esw_enable_vport(esw, 0);
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
