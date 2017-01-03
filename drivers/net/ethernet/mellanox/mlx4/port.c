/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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

#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/export.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"
#include "mlx4_stats.h"

#define MLX4_MAC_VALID		(1ull << 63)

#define MLX4_VLAN_VALID		(1u << 31)
#define MLX4_VLAN_MASK		0xfff

#define MLX4_STATS_TRAFFIC_COUNTERS_MASK	0xfULL
#define MLX4_STATS_TRAFFIC_DROPS_MASK		0xc0ULL
#define MLX4_STATS_ERROR_COUNTERS_MASK		0x1ffc30ULL
#define MLX4_STATS_PORT_COUNTERS_MASK		0x1fe00000ULL

#define MLX4_FLAG_V_IGNORE_FCS_MASK		0x2
#define MLX4_IGNORE_FCS_MASK			0x1
#define MLX4_TC_MAX_NUMBER			8

void mlx4_init_mac_table(struct mlx4_dev *dev, struct mlx4_mac_table *table)
{
	int i;

	mutex_init(&table->mutex);
	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		table->entries[i] = 0;
		table->refs[i]	 = 0;
		table->is_dup[i] = false;
	}
	table->max   = 1 << dev->caps.log_num_macs;
	table->total = 0;
}

void mlx4_init_vlan_table(struct mlx4_dev *dev, struct mlx4_vlan_table *table)
{
	int i;

	mutex_init(&table->mutex);
	for (i = 0; i < MLX4_MAX_VLAN_NUM; i++) {
		table->entries[i] = 0;
		table->refs[i]	 = 0;
		table->is_dup[i] = false;
	}
	table->max   = (1 << dev->caps.log_num_vlans) - MLX4_VLAN_REGULAR;
	table->total = 0;
}

void mlx4_init_roce_gid_table(struct mlx4_dev *dev,
			      struct mlx4_roce_gid_table *table)
{
	int i;

	mutex_init(&table->mutex);
	for (i = 0; i < MLX4_ROCE_MAX_GIDS; i++)
		memset(table->roce_gids[i].raw, 0, MLX4_ROCE_GID_ENTRY_SIZE);
}

static int validate_index(struct mlx4_dev *dev,
			  struct mlx4_mac_table *table, int index)
{
	int err = 0;

	if (index < 0 || index >= table->max || !table->entries[index]) {
		mlx4_warn(dev, "No valid Mac entry for the given index\n");
		err = -EINVAL;
	}
	return err;
}

static int find_index(struct mlx4_dev *dev,
		      struct mlx4_mac_table *table, u64 mac)
{
	int i;

	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (table->refs[i] &&
		    (MLX4_MAC_MASK & mac) ==
		    (MLX4_MAC_MASK & be64_to_cpu(table->entries[i])))
			return i;
	}
	/* Mac not found */
	return -EINVAL;
}

static int mlx4_set_port_mac_table(struct mlx4_dev *dev, u8 port,
				   __be64 *entries)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_mod;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, entries, MLX4_MAC_TABLE_SIZE);

	in_mod = MLX4_SET_PORT_MAC_TABLE << 8 | port;

	err = mlx4_cmd(dev, mailbox->dma, in_mod, MLX4_SET_PORT_ETH_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

int mlx4_find_cached_mac(struct mlx4_dev *dev, u8 port, u64 mac, int *idx)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	int i;

	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (!table->refs[i])
			continue;

		if (mac == (MLX4_MAC_MASK & be64_to_cpu(table->entries[i]))) {
			*idx = i;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(mlx4_find_cached_mac);

static bool mlx4_need_mf_bond(struct mlx4_dev *dev)
{
	int i, num_eth_ports = 0;

	if (!mlx4_is_mfunc(dev))
		return false;
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH)
		++num_eth_ports;

	return (num_eth_ports ==  2) ? true : false;
}

int __mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	int i, err = 0;
	int free = -1;
	int free_for_dup = -1;
	bool dup = mlx4_is_mf_bonded(dev);
	u8 dup_port = (port == 1) ? 2 : 1;
	struct mlx4_mac_table *dup_table = &mlx4_priv(dev)->port[dup_port].mac_table;
	bool need_mf_bond = mlx4_need_mf_bond(dev);
	bool can_mf_bond = true;

	mlx4_dbg(dev, "Registering MAC: 0x%llx for port %d %s duplicate\n",
		 (unsigned long long)mac, port,
		 dup ? "with" : "without");

	if (need_mf_bond) {
		if (port == 1) {
			mutex_lock(&table->mutex);
			mutex_lock_nested(&dup_table->mutex, SINGLE_DEPTH_NESTING);
		} else {
			mutex_lock(&dup_table->mutex);
			mutex_lock_nested(&table->mutex, SINGLE_DEPTH_NESTING);
		}
	} else {
		mutex_lock(&table->mutex);
	}

	if (need_mf_bond) {
		int index_at_port = -1;
		int index_at_dup_port = -1;

		for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
			if (((MLX4_MAC_MASK & mac) == (MLX4_MAC_MASK & be64_to_cpu(table->entries[i]))))
				index_at_port = i;
			if (((MLX4_MAC_MASK & mac) == (MLX4_MAC_MASK & be64_to_cpu(dup_table->entries[i]))))
				index_at_dup_port = i;
		}

		/* check that same mac is not in the tables at different indices */
		if ((index_at_port != index_at_dup_port) &&
		    (index_at_port >= 0) &&
		    (index_at_dup_port >= 0))
			can_mf_bond = false;

		/* If the mac is already in the primary table, the slot must be
		 * available in the duplicate table as well.
		 */
		if (index_at_port >= 0 && index_at_dup_port < 0 &&
		    dup_table->refs[index_at_port]) {
			can_mf_bond = false;
		}
		/* If the mac is already in the duplicate table, check that the
		 * corresponding index is not occupied in the primary table, or
		 * the primary table already contains the mac at the same index.
		 * Otherwise, you cannot bond (primary contains a different mac
		 * at that index).
		 */
		if (index_at_dup_port >= 0) {
			if (!table->refs[index_at_dup_port] ||
			    ((MLX4_MAC_MASK & mac) == (MLX4_MAC_MASK & be64_to_cpu(table->entries[index_at_dup_port]))))
				free_for_dup = index_at_dup_port;
			else
				can_mf_bond = false;
		}
	}

	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (!table->refs[i]) {
			if (free < 0)
				free = i;
			if (free_for_dup < 0 && need_mf_bond && can_mf_bond) {
				if (!dup_table->refs[i])
					free_for_dup = i;
			}
			continue;
		}

		if ((MLX4_MAC_MASK & mac) ==
		     (MLX4_MAC_MASK & be64_to_cpu(table->entries[i]))) {
			/* MAC already registered, increment ref count */
			err = i;
			++table->refs[i];
			if (dup) {
				u64 dup_mac = MLX4_MAC_MASK & be64_to_cpu(dup_table->entries[i]);

				if (dup_mac != mac || !dup_table->is_dup[i]) {
					mlx4_warn(dev, "register mac: expect duplicate mac 0x%llx on port %d index %d\n",
						  mac, dup_port, i);
				}
			}
			goto out;
		}
	}

	if (need_mf_bond && (free_for_dup < 0)) {
		if (dup) {
			mlx4_warn(dev, "Fail to allocate duplicate MAC table entry\n");
			mlx4_warn(dev, "High Availability for virtual functions may not work as expected\n");
			dup = false;
		}
		can_mf_bond = false;
	}

	if (need_mf_bond && can_mf_bond)
		free = free_for_dup;

	mlx4_dbg(dev, "Free MAC index is %d\n", free);

	if (table->total == table->max) {
		/* No free mac entries */
		err = -ENOSPC;
		goto out;
	}

	/* Register new MAC */
	table->entries[free] = cpu_to_be64(mac | MLX4_MAC_VALID);

	err = mlx4_set_port_mac_table(dev, port, table->entries);
	if (unlikely(err)) {
		mlx4_err(dev, "Failed adding MAC: 0x%llx\n",
			 (unsigned long long) mac);
		table->entries[free] = 0;
		goto out;
	}
	table->refs[free] = 1;
	table->is_dup[free] = false;
	++table->total;
	if (dup) {
		dup_table->refs[free] = 0;
		dup_table->is_dup[free] = true;
		dup_table->entries[free] = cpu_to_be64(mac | MLX4_MAC_VALID);

		err = mlx4_set_port_mac_table(dev, dup_port, dup_table->entries);
		if (unlikely(err)) {
			mlx4_warn(dev, "Failed adding duplicate mac: 0x%llx\n", mac);
			dup_table->is_dup[free] = false;
			dup_table->entries[free] = 0;
			goto out;
		}
		++dup_table->total;
	}
	err = free;
out:
	if (need_mf_bond) {
		if (port == 2) {
			mutex_unlock(&table->mutex);
			mutex_unlock(&dup_table->mutex);
		} else {
			mutex_unlock(&dup_table->mutex);
			mutex_unlock(&table->mutex);
		}
	} else {
		mutex_unlock(&table->mutex);
	}
	return err;
}
EXPORT_SYMBOL_GPL(__mlx4_register_mac);

int mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac)
{
	u64 out_param = 0;
	int err = -EINVAL;

	if (mlx4_is_mfunc(dev)) {
		if (!(dev->flags & MLX4_FLAG_OLD_REG_MAC)) {
			err = mlx4_cmd_imm(dev, mac, &out_param,
					   ((u32) port) << 8 | (u32) RES_MAC,
					   RES_OP_RESERVE_AND_MAP, MLX4_CMD_ALLOC_RES,
					   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		}
		if (err && err == -EINVAL && mlx4_is_slave(dev)) {
			/* retry using old REG_MAC format */
			set_param_l(&out_param, port);
			err = mlx4_cmd_imm(dev, mac, &out_param, RES_MAC,
					   RES_OP_RESERVE_AND_MAP, MLX4_CMD_ALLOC_RES,
					   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
			if (!err)
				dev->flags |= MLX4_FLAG_OLD_REG_MAC;
		}
		if (err)
			return err;

		return get_param_l(&out_param);
	}
	return __mlx4_register_mac(dev, port, mac);
}
EXPORT_SYMBOL_GPL(mlx4_register_mac);

int mlx4_get_base_qpn(struct mlx4_dev *dev, u8 port)
{
	return dev->caps.reserved_qps_base[MLX4_QP_REGION_ETH_ADDR] +
			(port - 1) * (1 << dev->caps.log_num_macs);
}
EXPORT_SYMBOL_GPL(mlx4_get_base_qpn);

void __mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, u64 mac)
{
	struct mlx4_port_info *info;
	struct mlx4_mac_table *table;
	int index;
	bool dup = mlx4_is_mf_bonded(dev);
	u8 dup_port = (port == 1) ? 2 : 1;
	struct mlx4_mac_table *dup_table = &mlx4_priv(dev)->port[dup_port].mac_table;

	if (port < 1 || port > dev->caps.num_ports) {
		mlx4_warn(dev, "invalid port number (%d), aborting...\n", port);
		return;
	}
	info = &mlx4_priv(dev)->port[port];
	table = &info->mac_table;

	if (dup) {
		if (port == 1) {
			mutex_lock(&table->mutex);
			mutex_lock_nested(&dup_table->mutex, SINGLE_DEPTH_NESTING);
		} else {
			mutex_lock(&dup_table->mutex);
			mutex_lock_nested(&table->mutex, SINGLE_DEPTH_NESTING);
		}
	} else {
		mutex_lock(&table->mutex);
	}

	index = find_index(dev, table, mac);

	if (validate_index(dev, table, index))
		goto out;

	if (--table->refs[index] || table->is_dup[index]) {
		mlx4_dbg(dev, "Have more references for index %d, no need to modify mac table\n",
			 index);
		if (!table->refs[index])
			dup_table->is_dup[index] = false;
		goto out;
	}

	table->entries[index] = 0;
	if (mlx4_set_port_mac_table(dev, port, table->entries))
		mlx4_warn(dev, "Fail to set mac in port %d during unregister\n", port);
	--table->total;

	if (dup) {
		dup_table->is_dup[index] = false;
		if (dup_table->refs[index])
			goto out;
		dup_table->entries[index] = 0;
		if (mlx4_set_port_mac_table(dev, dup_port, dup_table->entries))
			mlx4_warn(dev, "Fail to set mac in duplicate port %d during unregister\n", dup_port);

		--table->total;
	}
out:
	if (dup) {
		if (port == 2) {
			mutex_unlock(&table->mutex);
			mutex_unlock(&dup_table->mutex);
		} else {
			mutex_unlock(&dup_table->mutex);
			mutex_unlock(&table->mutex);
		}
	} else {
		mutex_unlock(&table->mutex);
	}
}
EXPORT_SYMBOL_GPL(__mlx4_unregister_mac);

void mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, u64 mac)
{
	u64 out_param = 0;

	if (mlx4_is_mfunc(dev)) {
		if (!(dev->flags & MLX4_FLAG_OLD_REG_MAC)) {
			(void) mlx4_cmd_imm(dev, mac, &out_param,
					    ((u32) port) << 8 | (u32) RES_MAC,
					    RES_OP_RESERVE_AND_MAP, MLX4_CMD_FREE_RES,
					    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		} else {
			/* use old unregister mac format */
			set_param_l(&out_param, port);
			(void) mlx4_cmd_imm(dev, mac, &out_param, RES_MAC,
					    RES_OP_RESERVE_AND_MAP, MLX4_CMD_FREE_RES,
					    MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		}
		return;
	}
	__mlx4_unregister_mac(dev, port, mac);
	return;
}
EXPORT_SYMBOL_GPL(mlx4_unregister_mac);

int __mlx4_replace_mac(struct mlx4_dev *dev, u8 port, int qpn, u64 new_mac)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	struct mlx4_mac_table *table = &info->mac_table;
	int index = qpn - info->base_qpn;
	int err = 0;
	bool dup = mlx4_is_mf_bonded(dev);
	u8 dup_port = (port == 1) ? 2 : 1;
	struct mlx4_mac_table *dup_table = &mlx4_priv(dev)->port[dup_port].mac_table;

	/* CX1 doesn't support multi-functions */
	if (dup) {
		if (port == 1) {
			mutex_lock(&table->mutex);
			mutex_lock_nested(&dup_table->mutex, SINGLE_DEPTH_NESTING);
		} else {
			mutex_lock(&dup_table->mutex);
			mutex_lock_nested(&table->mutex, SINGLE_DEPTH_NESTING);
		}
	} else {
		mutex_lock(&table->mutex);
	}

	err = validate_index(dev, table, index);
	if (err)
		goto out;

	table->entries[index] = cpu_to_be64(new_mac | MLX4_MAC_VALID);

	err = mlx4_set_port_mac_table(dev, port, table->entries);
	if (unlikely(err)) {
		mlx4_err(dev, "Failed adding MAC: 0x%llx\n",
			 (unsigned long long) new_mac);
		table->entries[index] = 0;
	} else {
		if (dup) {
			dup_table->entries[index] = cpu_to_be64(new_mac | MLX4_MAC_VALID);

			err = mlx4_set_port_mac_table(dev, dup_port, dup_table->entries);
			if (unlikely(err)) {
				mlx4_err(dev, "Failed adding duplicate MAC: 0x%llx\n",
					 (unsigned long long)new_mac);
				dup_table->entries[index] = 0;
			}
		}
	}
out:
	if (dup) {
		if (port == 2) {
			mutex_unlock(&table->mutex);
			mutex_unlock(&dup_table->mutex);
		} else {
			mutex_unlock(&dup_table->mutex);
			mutex_unlock(&table->mutex);
		}
	} else {
		mutex_unlock(&table->mutex);
	}
	return err;
}
EXPORT_SYMBOL_GPL(__mlx4_replace_mac);

static int mlx4_set_port_vlan_table(struct mlx4_dev *dev, u8 port,
				    __be32 *entries)
{
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_mod;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, entries, MLX4_VLAN_TABLE_SIZE);
	in_mod = MLX4_SET_PORT_VLAN_TABLE << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, MLX4_SET_PORT_ETH_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);

	return err;
}

int mlx4_find_cached_vlan(struct mlx4_dev *dev, u8 port, u16 vid, int *idx)
{
	struct mlx4_vlan_table *table = &mlx4_priv(dev)->port[port].vlan_table;
	int i;

	for (i = 0; i < MLX4_MAX_VLAN_NUM; ++i) {
		if (table->refs[i] &&
		    (vid == (MLX4_VLAN_MASK &
			      be32_to_cpu(table->entries[i])))) {
			/* VLAN already registered, increase reference count */
			*idx = i;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(mlx4_find_cached_vlan);

int __mlx4_register_vlan(struct mlx4_dev *dev, u8 port, u16 vlan,
				int *index)
{
	struct mlx4_vlan_table *table = &mlx4_priv(dev)->port[port].vlan_table;
	int i, err = 0;
	int free = -1;
	int free_for_dup = -1;
	bool dup = mlx4_is_mf_bonded(dev);
	u8 dup_port = (port == 1) ? 2 : 1;
	struct mlx4_vlan_table *dup_table = &mlx4_priv(dev)->port[dup_port].vlan_table;
	bool need_mf_bond = mlx4_need_mf_bond(dev);
	bool can_mf_bond = true;

	mlx4_dbg(dev, "Registering VLAN: %d for port %d %s duplicate\n",
		 vlan, port,
		 dup ? "with" : "without");

	if (need_mf_bond) {
		if (port == 1) {
			mutex_lock(&table->mutex);
			mutex_lock_nested(&dup_table->mutex, SINGLE_DEPTH_NESTING);
		} else {
			mutex_lock(&dup_table->mutex);
			mutex_lock_nested(&table->mutex, SINGLE_DEPTH_NESTING);
		}
	} else {
		mutex_lock(&table->mutex);
	}

	if (table->total == table->max) {
		/* No free vlan entries */
		err = -ENOSPC;
		goto out;
	}

	if (need_mf_bond) {
		int index_at_port = -1;
		int index_at_dup_port = -1;

		for (i = MLX4_VLAN_REGULAR; i < MLX4_MAX_VLAN_NUM; i++) {
			if ((vlan == (MLX4_VLAN_MASK & be32_to_cpu(table->entries[i]))))
				index_at_port = i;
			if ((vlan == (MLX4_VLAN_MASK & be32_to_cpu(dup_table->entries[i]))))
				index_at_dup_port = i;
		}
		/* check that same vlan is not in the tables at different indices */
		if ((index_at_port != index_at_dup_port) &&
		    (index_at_port >= 0) &&
		    (index_at_dup_port >= 0))
			can_mf_bond = false;

		/* If the vlan is already in the primary table, the slot must be
		 * available in the duplicate table as well.
		 */
		if (index_at_port >= 0 && index_at_dup_port < 0 &&
		    dup_table->refs[index_at_port]) {
			can_mf_bond = false;
		}
		/* If the vlan is already in the duplicate table, check that the
		 * corresponding index is not occupied in the primary table, or
		 * the primary table already contains the vlan at the same index.
		 * Otherwise, you cannot bond (primary contains a different vlan
		 * at that index).
		 */
		if (index_at_dup_port >= 0) {
			if (!table->refs[index_at_dup_port] ||
			    (vlan == (MLX4_VLAN_MASK & be32_to_cpu(dup_table->entries[index_at_dup_port]))))
				free_for_dup = index_at_dup_port;
			else
				can_mf_bond = false;
		}
	}

	for (i = MLX4_VLAN_REGULAR; i < MLX4_MAX_VLAN_NUM; i++) {
		if (!table->refs[i]) {
			if (free < 0)
				free = i;
			if (free_for_dup < 0 && need_mf_bond && can_mf_bond) {
				if (!dup_table->refs[i])
					free_for_dup = i;
			}
		}

		if ((table->refs[i] || table->is_dup[i]) &&
		    (vlan == (MLX4_VLAN_MASK &
			      be32_to_cpu(table->entries[i])))) {
			/* Vlan already registered, increase references count */
			mlx4_dbg(dev, "vlan %u is already registered.\n", vlan);
			*index = i;
			++table->refs[i];
			if (dup) {
				u16 dup_vlan = MLX4_VLAN_MASK & be32_to_cpu(dup_table->entries[i]);

				if (dup_vlan != vlan || !dup_table->is_dup[i]) {
					mlx4_warn(dev, "register vlan: expected duplicate vlan %u on port %d index %d\n",
						  vlan, dup_port, i);
				}
			}
			goto out;
		}
	}

	if (need_mf_bond && (free_for_dup < 0)) {
		if (dup) {
			mlx4_warn(dev, "Fail to allocate duplicate VLAN table entry\n");
			mlx4_warn(dev, "High Availability for virtual functions may not work as expected\n");
			dup = false;
		}
		can_mf_bond = false;
	}

	if (need_mf_bond && can_mf_bond)
		free = free_for_dup;

	if (free < 0) {
		err = -ENOMEM;
		goto out;
	}

	/* Register new VLAN */
	table->refs[free] = 1;
	table->is_dup[free] = false;
	table->entries[free] = cpu_to_be32(vlan | MLX4_VLAN_VALID);

	err = mlx4_set_port_vlan_table(dev, port, table->entries);
	if (unlikely(err)) {
		mlx4_warn(dev, "Failed adding vlan: %u\n", vlan);
		table->refs[free] = 0;
		table->entries[free] = 0;
		goto out;
	}
	++table->total;
	if (dup) {
		dup_table->refs[free] = 0;
		dup_table->is_dup[free] = true;
		dup_table->entries[free] = cpu_to_be32(vlan | MLX4_VLAN_VALID);

		err = mlx4_set_port_vlan_table(dev, dup_port, dup_table->entries);
		if (unlikely(err)) {
			mlx4_warn(dev, "Failed adding duplicate vlan: %u\n", vlan);
			dup_table->is_dup[free] = false;
			dup_table->entries[free] = 0;
			goto out;
		}
		++dup_table->total;
	}

	*index = free;
out:
	if (need_mf_bond) {
		if (port == 2) {
			mutex_unlock(&table->mutex);
			mutex_unlock(&dup_table->mutex);
		} else {
			mutex_unlock(&dup_table->mutex);
			mutex_unlock(&table->mutex);
		}
	} else {
		mutex_unlock(&table->mutex);
	}
	return err;
}

int mlx4_register_vlan(struct mlx4_dev *dev, u8 port, u16 vlan, int *index)
{
	u64 out_param = 0;
	int err;

	if (vlan > 4095)
		return -EINVAL;

	if (mlx4_is_mfunc(dev)) {
		err = mlx4_cmd_imm(dev, vlan, &out_param,
				   ((u32) port) << 8 | (u32) RES_VLAN,
				   RES_OP_RESERVE_AND_MAP, MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (!err)
			*index = get_param_l(&out_param);

		return err;
	}
	return __mlx4_register_vlan(dev, port, vlan, index);
}
EXPORT_SYMBOL_GPL(mlx4_register_vlan);

void __mlx4_unregister_vlan(struct mlx4_dev *dev, u8 port, u16 vlan)
{
	struct mlx4_vlan_table *table = &mlx4_priv(dev)->port[port].vlan_table;
	int index;
	bool dup = mlx4_is_mf_bonded(dev);
	u8 dup_port = (port == 1) ? 2 : 1;
	struct mlx4_vlan_table *dup_table = &mlx4_priv(dev)->port[dup_port].vlan_table;

	if (dup) {
		if (port == 1) {
			mutex_lock(&table->mutex);
			mutex_lock_nested(&dup_table->mutex, SINGLE_DEPTH_NESTING);
		} else {
			mutex_lock(&dup_table->mutex);
			mutex_lock_nested(&table->mutex, SINGLE_DEPTH_NESTING);
		}
	} else {
		mutex_lock(&table->mutex);
	}

	if (mlx4_find_cached_vlan(dev, port, vlan, &index)) {
		mlx4_warn(dev, "vlan 0x%x is not in the vlan table\n", vlan);
		goto out;
	}

	if (index < MLX4_VLAN_REGULAR) {
		mlx4_warn(dev, "Trying to free special vlan index %d\n", index);
		goto out;
	}

	if (--table->refs[index] || table->is_dup[index]) {
		mlx4_dbg(dev, "Have %d more references for index %d, no need to modify vlan table\n",
			 table->refs[index], index);
		if (!table->refs[index])
			dup_table->is_dup[index] = false;
		goto out;
	}
	table->entries[index] = 0;
	if (mlx4_set_port_vlan_table(dev, port, table->entries))
		mlx4_warn(dev, "Fail to set vlan in port %d during unregister\n", port);
	--table->total;
	if (dup) {
		dup_table->is_dup[index] = false;
		if (dup_table->refs[index])
			goto out;
		dup_table->entries[index] = 0;
		if (mlx4_set_port_vlan_table(dev, dup_port, dup_table->entries))
			mlx4_warn(dev, "Fail to set vlan in duplicate port %d during unregister\n", dup_port);
		--dup_table->total;
	}
out:
	if (dup) {
		if (port == 2) {
			mutex_unlock(&table->mutex);
			mutex_unlock(&dup_table->mutex);
		} else {
			mutex_unlock(&dup_table->mutex);
			mutex_unlock(&table->mutex);
		}
	} else {
		mutex_unlock(&table->mutex);
	}
}

void mlx4_unregister_vlan(struct mlx4_dev *dev, u8 port, u16 vlan)
{
	u64 out_param = 0;

	if (mlx4_is_mfunc(dev)) {
		(void) mlx4_cmd_imm(dev, vlan, &out_param,
				    ((u32) port) << 8 | (u32) RES_VLAN,
				    RES_OP_RESERVE_AND_MAP,
				    MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A,
				    MLX4_CMD_WRAPPED);
		return;
	}
	__mlx4_unregister_vlan(dev, port, vlan);
}
EXPORT_SYMBOL_GPL(mlx4_unregister_vlan);

int mlx4_bond_mac_table(struct mlx4_dev *dev)
{
	struct mlx4_mac_table *t1 = &mlx4_priv(dev)->port[1].mac_table;
	struct mlx4_mac_table *t2 = &mlx4_priv(dev)->port[2].mac_table;
	int ret = 0;
	int i;
	bool update1 = false;
	bool update2 = false;

	mutex_lock(&t1->mutex);
	mutex_lock(&t2->mutex);
	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if ((t1->entries[i] != t2->entries[i]) &&
		    t1->entries[i] && t2->entries[i]) {
			mlx4_warn(dev, "can't duplicate entry %d in mac table\n", i);
			ret = -EINVAL;
			goto unlock;
		}
	}

	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (t1->entries[i] && !t2->entries[i]) {
			t2->entries[i] = t1->entries[i];
			t2->is_dup[i] = true;
			update2 = true;
		} else if (!t1->entries[i] && t2->entries[i]) {
			t1->entries[i] = t2->entries[i];
			t1->is_dup[i] = true;
			update1 = true;
		} else if (t1->entries[i] && t2->entries[i]) {
			t1->is_dup[i] = true;
			t2->is_dup[i] = true;
		}
	}

	if (update1) {
		ret = mlx4_set_port_mac_table(dev, 1, t1->entries);
		if (ret)
			mlx4_warn(dev, "failed to set MAC table for port 1 (%d)\n", ret);
	}
	if (!ret && update2) {
		ret = mlx4_set_port_mac_table(dev, 2, t2->entries);
		if (ret)
			mlx4_warn(dev, "failed to set MAC table for port 2 (%d)\n", ret);
	}

	if (ret)
		mlx4_warn(dev, "failed to create mirror MAC tables\n");
unlock:
	mutex_unlock(&t2->mutex);
	mutex_unlock(&t1->mutex);
	return ret;
}

int mlx4_unbond_mac_table(struct mlx4_dev *dev)
{
	struct mlx4_mac_table *t1 = &mlx4_priv(dev)->port[1].mac_table;
	struct mlx4_mac_table *t2 = &mlx4_priv(dev)->port[2].mac_table;
	int ret = 0;
	int ret1;
	int i;
	bool update1 = false;
	bool update2 = false;

	mutex_lock(&t1->mutex);
	mutex_lock(&t2->mutex);
	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (t1->entries[i] != t2->entries[i]) {
			mlx4_warn(dev, "mac table is in an unexpected state when trying to unbond\n");
			ret = -EINVAL;
			goto unlock;
		}
	}

	for (i = 0; i < MLX4_MAX_MAC_NUM; i++) {
		if (!t1->entries[i])
			continue;
		t1->is_dup[i] = false;
		if (!t1->refs[i]) {
			t1->entries[i] = 0;
			update1 = true;
		}
		t2->is_dup[i] = false;
		if (!t2->refs[i]) {
			t2->entries[i] = 0;
			update2 = true;
		}
	}

	if (update1) {
		ret = mlx4_set_port_mac_table(dev, 1, t1->entries);
		if (ret)
			mlx4_warn(dev, "failed to unmirror MAC tables for port 1(%d)\n", ret);
	}
	if (update2) {
		ret1 = mlx4_set_port_mac_table(dev, 2, t2->entries);
		if (ret1) {
			mlx4_warn(dev, "failed to unmirror MAC tables for port 2(%d)\n", ret1);
			ret = ret1;
		}
	}
unlock:
	mutex_unlock(&t2->mutex);
	mutex_unlock(&t1->mutex);
	return ret;
}

int mlx4_bond_vlan_table(struct mlx4_dev *dev)
{
	struct mlx4_vlan_table *t1 = &mlx4_priv(dev)->port[1].vlan_table;
	struct mlx4_vlan_table *t2 = &mlx4_priv(dev)->port[2].vlan_table;
	int ret = 0;
	int i;
	bool update1 = false;
	bool update2 = false;

	mutex_lock(&t1->mutex);
	mutex_lock(&t2->mutex);
	for (i = 0; i < MLX4_MAX_VLAN_NUM; i++) {
		if ((t1->entries[i] != t2->entries[i]) &&
		    t1->entries[i] && t2->entries[i]) {
			mlx4_warn(dev, "can't duplicate entry %d in vlan table\n", i);
			ret = -EINVAL;
			goto unlock;
		}
	}

	for (i = 0; i < MLX4_MAX_VLAN_NUM; i++) {
		if (t1->entries[i] && !t2->entries[i]) {
			t2->entries[i] = t1->entries[i];
			t2->is_dup[i] = true;
			update2 = true;
		} else if (!t1->entries[i] && t2->entries[i]) {
			t1->entries[i] = t2->entries[i];
			t1->is_dup[i] = true;
			update1 = true;
		} else if (t1->entries[i] && t2->entries[i]) {
			t1->is_dup[i] = true;
			t2->is_dup[i] = true;
		}
	}

	if (update1) {
		ret = mlx4_set_port_vlan_table(dev, 1, t1->entries);
		if (ret)
			mlx4_warn(dev, "failed to set VLAN table for port 1 (%d)\n", ret);
	}
	if (!ret && update2) {
		ret = mlx4_set_port_vlan_table(dev, 2, t2->entries);
		if (ret)
			mlx4_warn(dev, "failed to set VLAN table for port 2 (%d)\n", ret);
	}

	if (ret)
		mlx4_warn(dev, "failed to create mirror VLAN tables\n");
unlock:
	mutex_unlock(&t2->mutex);
	mutex_unlock(&t1->mutex);
	return ret;
}

int mlx4_unbond_vlan_table(struct mlx4_dev *dev)
{
	struct mlx4_vlan_table *t1 = &mlx4_priv(dev)->port[1].vlan_table;
	struct mlx4_vlan_table *t2 = &mlx4_priv(dev)->port[2].vlan_table;
	int ret = 0;
	int ret1;
	int i;
	bool update1 = false;
	bool update2 = false;

	mutex_lock(&t1->mutex);
	mutex_lock(&t2->mutex);
	for (i = 0; i < MLX4_MAX_VLAN_NUM; i++) {
		if (t1->entries[i] != t2->entries[i]) {
			mlx4_warn(dev, "vlan table is in an unexpected state when trying to unbond\n");
			ret = -EINVAL;
			goto unlock;
		}
	}

	for (i = 0; i < MLX4_MAX_VLAN_NUM; i++) {
		if (!t1->entries[i])
			continue;
		t1->is_dup[i] = false;
		if (!t1->refs[i]) {
			t1->entries[i] = 0;
			update1 = true;
		}
		t2->is_dup[i] = false;
		if (!t2->refs[i]) {
			t2->entries[i] = 0;
			update2 = true;
		}
	}

	if (update1) {
		ret = mlx4_set_port_vlan_table(dev, 1, t1->entries);
		if (ret)
			mlx4_warn(dev, "failed to unmirror VLAN tables for port 1(%d)\n", ret);
	}
	if (update2) {
		ret1 = mlx4_set_port_vlan_table(dev, 2, t2->entries);
		if (ret1) {
			mlx4_warn(dev, "failed to unmirror VLAN tables for port 2(%d)\n", ret1);
			ret = ret1;
		}
	}
unlock:
	mutex_unlock(&t2->mutex);
	mutex_unlock(&t1->mutex);
	return ret;
}

int mlx4_get_port_ib_caps(struct mlx4_dev *dev, u8 port, __be32 *caps)
{
	struct mlx4_cmd_mailbox *inmailbox, *outmailbox;
	u8 *inbuf, *outbuf;
	int err;

	inmailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(inmailbox))
		return PTR_ERR(inmailbox);

	outmailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(outmailbox)) {
		mlx4_free_cmd_mailbox(dev, inmailbox);
		return PTR_ERR(outmailbox);
	}

	inbuf = inmailbox->buf;
	outbuf = outmailbox->buf;
	inbuf[0] = 1;
	inbuf[1] = 1;
	inbuf[2] = 1;
	inbuf[3] = 1;
	*(__be16 *) (&inbuf[16]) = cpu_to_be16(0x0015);
	*(__be32 *) (&inbuf[20]) = cpu_to_be32(port);

	err = mlx4_cmd_box(dev, inmailbox->dma, outmailbox->dma, port, 3,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_NATIVE);
	if (!err)
		*caps = *(__be32 *) (outbuf + 84);
	mlx4_free_cmd_mailbox(dev, inmailbox);
	mlx4_free_cmd_mailbox(dev, outmailbox);
	return err;
}
static struct mlx4_roce_gid_entry zgid_entry;

int mlx4_get_slave_num_gids(struct mlx4_dev *dev, int slave, int port)
{
	int vfs;
	int slave_gid = slave;
	unsigned i;
	struct mlx4_slaves_pport slaves_pport;
	struct mlx4_active_ports actv_ports;
	unsigned max_port_p_one;

	if (slave == 0)
		return MLX4_ROCE_PF_GIDS;

	/* Slave is a VF */
	slaves_pport = mlx4_phys_to_slaves_pport(dev, port);
	actv_ports = mlx4_get_active_ports(dev, slave);
	max_port_p_one = find_first_bit(actv_ports.ports, dev->caps.num_ports) +
		bitmap_weight(actv_ports.ports, dev->caps.num_ports) + 1;

	for (i = 1; i < max_port_p_one; i++) {
		struct mlx4_active_ports exclusive_ports;
		struct mlx4_slaves_pport slaves_pport_actv;
		bitmap_zero(exclusive_ports.ports, dev->caps.num_ports);
		set_bit(i - 1, exclusive_ports.ports);
		if (i == port)
			continue;
		slaves_pport_actv = mlx4_phys_to_slaves_pport_actv(
				    dev, &exclusive_ports);
		slave_gid -= bitmap_weight(slaves_pport_actv.slaves,
					   dev->persist->num_vfs + 1);
	}
	vfs = bitmap_weight(slaves_pport.slaves, dev->persist->num_vfs + 1) - 1;
	if (slave_gid <= ((MLX4_ROCE_MAX_GIDS - MLX4_ROCE_PF_GIDS) % vfs))
		return ((MLX4_ROCE_MAX_GIDS - MLX4_ROCE_PF_GIDS) / vfs) + 1;
	return (MLX4_ROCE_MAX_GIDS - MLX4_ROCE_PF_GIDS) / vfs;
}

int mlx4_get_base_gid_ix(struct mlx4_dev *dev, int slave, int port)
{
	int gids;
	unsigned i;
	int slave_gid = slave;
	int vfs;

	struct mlx4_slaves_pport slaves_pport;
	struct mlx4_active_ports actv_ports;
	unsigned max_port_p_one;

	if (slave == 0)
		return 0;

	slaves_pport = mlx4_phys_to_slaves_pport(dev, port);
	actv_ports = mlx4_get_active_ports(dev, slave);
	max_port_p_one = find_first_bit(actv_ports.ports, dev->caps.num_ports) +
		bitmap_weight(actv_ports.ports, dev->caps.num_ports) + 1;

	for (i = 1; i < max_port_p_one; i++) {
		struct mlx4_active_ports exclusive_ports;
		struct mlx4_slaves_pport slaves_pport_actv;
		bitmap_zero(exclusive_ports.ports, dev->caps.num_ports);
		set_bit(i - 1, exclusive_ports.ports);
		if (i == port)
			continue;
		slaves_pport_actv = mlx4_phys_to_slaves_pport_actv(
				    dev, &exclusive_ports);
		slave_gid -= bitmap_weight(slaves_pport_actv.slaves,
					   dev->persist->num_vfs + 1);
	}
	gids = MLX4_ROCE_MAX_GIDS - MLX4_ROCE_PF_GIDS;
	vfs = bitmap_weight(slaves_pport.slaves, dev->persist->num_vfs + 1) - 1;
	if (slave_gid <= gids % vfs)
		return MLX4_ROCE_PF_GIDS + ((gids / vfs) + 1) * (slave_gid - 1);

	return MLX4_ROCE_PF_GIDS + (gids % vfs) +
		((gids / vfs) * (slave_gid - 1));
}
EXPORT_SYMBOL_GPL(mlx4_get_base_gid_ix);

static int mlx4_reset_roce_port_gids(struct mlx4_dev *dev, int slave,
				     int port, struct mlx4_cmd_mailbox *mailbox)
{
	struct mlx4_roce_gid_entry *gid_entry_mbox;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int num_gids, base, offset;
	int i, err;

	num_gids = mlx4_get_slave_num_gids(dev, slave, port);
	base = mlx4_get_base_gid_ix(dev, slave, port);

	memset(mailbox->buf, 0, MLX4_MAILBOX_SIZE);

	mutex_lock(&(priv->port[port].gid_table.mutex));
	/* Zero-out gids belonging to that slave in the port GID table */
	for (i = 0, offset = base; i < num_gids; offset++, i++)
		memcpy(priv->port[port].gid_table.roce_gids[offset].raw,
		       zgid_entry.raw, MLX4_ROCE_GID_ENTRY_SIZE);

	/* Now, copy roce port gids table to mailbox for passing to FW */
	gid_entry_mbox = (struct mlx4_roce_gid_entry *)mailbox->buf;
	for (i = 0; i < MLX4_ROCE_MAX_GIDS; gid_entry_mbox++, i++)
		memcpy(gid_entry_mbox->raw,
		       priv->port[port].gid_table.roce_gids[i].raw,
		       MLX4_ROCE_GID_ENTRY_SIZE);

	err = mlx4_cmd(dev, mailbox->dma,
		       ((u32)port) | (MLX4_SET_PORT_GID_TABLE << 8),
		       MLX4_SET_PORT_ETH_OPCODE, MLX4_CMD_SET_PORT,
		       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
	mutex_unlock(&(priv->port[port].gid_table.mutex));
	return err;
}


void mlx4_reset_roce_gids(struct mlx4_dev *dev, int slave)
{
	struct mlx4_active_ports actv_ports;
	struct mlx4_cmd_mailbox *mailbox;
	int num_eth_ports, err;
	int i;

	if (slave < 0 || slave > dev->persist->num_vfs)
		return;

	actv_ports = mlx4_get_active_ports(dev, slave);

	for (i = 0, num_eth_ports = 0; i < dev->caps.num_ports; i++) {
		if (test_bit(i, actv_ports.ports)) {
			if (dev->caps.port_type[i + 1] != MLX4_PORT_TYPE_ETH)
				continue;
			num_eth_ports++;
		}
	}

	if (!num_eth_ports)
		return;

	/* have ETH ports.  Alloc mailbox for SET_PORT command */
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return;

	for (i = 0; i < dev->caps.num_ports; i++) {
		if (test_bit(i, actv_ports.ports)) {
			if (dev->caps.port_type[i + 1] != MLX4_PORT_TYPE_ETH)
				continue;
			err = mlx4_reset_roce_port_gids(dev, slave, i + 1, mailbox);
			if (err)
				mlx4_warn(dev, "Could not reset ETH port GID table for slave %d, port %d (%d)\n",
					  slave, i + 1, err);
		}
	}

	mlx4_free_cmd_mailbox(dev, mailbox);
	return;
}

static int mlx4_common_set_port(struct mlx4_dev *dev, int slave, u32 in_mod,
				u8 op_mod, struct mlx4_cmd_mailbox *inbox)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_port_info *port_info;
	struct mlx4_mfunc_master_ctx *master = &priv->mfunc.master;
	struct mlx4_slave_state *slave_st = &master->slave_state[slave];
	struct mlx4_set_port_rqp_calc_context *qpn_context;
	struct mlx4_set_port_general_context *gen_context;
	struct mlx4_roce_gid_entry *gid_entry_tbl, *gid_entry_mbox, *gid_entry_mb1;
	int reset_qkey_viols;
	int port;
	int is_eth;
	int num_gids;
	int base;
	u32 in_modifier;
	u32 promisc;
	u16 mtu, prev_mtu;
	int err;
	int i, j;
	int offset;
	__be32 agg_cap_mask;
	__be32 slave_cap_mask;
	__be32 new_cap_mask;

	port = in_mod & 0xff;
	in_modifier = in_mod >> 8;
	is_eth = op_mod;
	port_info = &priv->port[port];

	/* Slaves cannot perform SET_PORT operations except changing MTU */
	if (is_eth) {
		if (slave != dev->caps.function &&
		    in_modifier != MLX4_SET_PORT_GENERAL &&
		    in_modifier != MLX4_SET_PORT_GID_TABLE) {
			mlx4_warn(dev, "denying SET_PORT for slave:%d\n",
					slave);
			return -EINVAL;
		}
		switch (in_modifier) {
		case MLX4_SET_PORT_RQP_CALC:
			qpn_context = inbox->buf;
			qpn_context->base_qpn =
				cpu_to_be32(port_info->base_qpn);
			qpn_context->n_mac = 0x7;
			promisc = be32_to_cpu(qpn_context->promisc) >>
				SET_PORT_PROMISC_SHIFT;
			qpn_context->promisc = cpu_to_be32(
				promisc << SET_PORT_PROMISC_SHIFT |
				port_info->base_qpn);
			promisc = be32_to_cpu(qpn_context->mcast) >>
				SET_PORT_MC_PROMISC_SHIFT;
			qpn_context->mcast = cpu_to_be32(
				promisc << SET_PORT_MC_PROMISC_SHIFT |
				port_info->base_qpn);
			break;
		case MLX4_SET_PORT_GENERAL:
			gen_context = inbox->buf;
			/* Mtu is configured as the max MTU among all the
			 * the functions on the port. */
			mtu = be16_to_cpu(gen_context->mtu);
			mtu = min_t(int, mtu, dev->caps.eth_mtu_cap[port] +
				    ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN);
			prev_mtu = slave_st->mtu[port];
			slave_st->mtu[port] = mtu;
			if (mtu > master->max_mtu[port])
				master->max_mtu[port] = mtu;
			if (mtu < prev_mtu && prev_mtu ==
						master->max_mtu[port]) {
				slave_st->mtu[port] = mtu;
				master->max_mtu[port] = mtu;
				for (i = 0; i < dev->num_slaves; i++) {
					master->max_mtu[port] =
					max(master->max_mtu[port],
					    master->slave_state[i].mtu[port]);
				}
			}

			gen_context->mtu = cpu_to_be16(master->max_mtu[port]);
			/* Slave cannot change Global Pause configuration */
			if (slave != mlx4_master_func_num(dev) &&
			    ((gen_context->pptx != master->pptx) ||
			     (gen_context->pprx != master->pprx))) {
				gen_context->pptx = master->pptx;
				gen_context->pprx = master->pprx;
				mlx4_warn(dev,
					  "denying Global Pause change for slave:%d\n",
					  slave);
			} else {
				master->pptx = gen_context->pptx;
				master->pprx = gen_context->pprx;
			}
			break;
		case MLX4_SET_PORT_GID_TABLE:
			/* change to MULTIPLE entries: number of guest's gids
			 * need a FOR-loop here over number of gids the guest has.
			 * 1. Check no duplicates in gids passed by slave
			 */
			num_gids = mlx4_get_slave_num_gids(dev, slave, port);
			base = mlx4_get_base_gid_ix(dev, slave, port);
			gid_entry_mbox = (struct mlx4_roce_gid_entry *)(inbox->buf);
			for (i = 0; i < num_gids; gid_entry_mbox++, i++) {
				if (!memcmp(gid_entry_mbox->raw, zgid_entry.raw,
					    sizeof(zgid_entry)))
					continue;
				gid_entry_mb1 = gid_entry_mbox + 1;
				for (j = i + 1; j < num_gids; gid_entry_mb1++, j++) {
					if (!memcmp(gid_entry_mb1->raw,
						    zgid_entry.raw, sizeof(zgid_entry)))
						continue;
					if (!memcmp(gid_entry_mb1->raw, gid_entry_mbox->raw,
						    sizeof(gid_entry_mbox->raw))) {
						/* found duplicate */
						return -EINVAL;
					}
				}
			}

			/* 2. Check that do not have duplicates in OTHER
			 *    entries in the port GID table
			 */

			mutex_lock(&(priv->port[port].gid_table.mutex));
			for (i = 0; i < MLX4_ROCE_MAX_GIDS; i++) {
				if (i >= base && i < base + num_gids)
					continue; /* don't compare to slave's current gids */
				gid_entry_tbl = &priv->port[port].gid_table.roce_gids[i];
				if (!memcmp(gid_entry_tbl->raw, zgid_entry.raw, sizeof(zgid_entry)))
					continue;
				gid_entry_mbox = (struct mlx4_roce_gid_entry *)(inbox->buf);
				for (j = 0; j < num_gids; gid_entry_mbox++, j++) {
					if (!memcmp(gid_entry_mbox->raw, zgid_entry.raw,
						    sizeof(zgid_entry)))
						continue;
					if (!memcmp(gid_entry_mbox->raw, gid_entry_tbl->raw,
						    sizeof(gid_entry_tbl->raw))) {
						/* found duplicate */
						mlx4_warn(dev, "requested gid entry for slave:%d is a duplicate of gid at index %d\n",
							  slave, i);
						mutex_unlock(&(priv->port[port].gid_table.mutex));
						return -EINVAL;
					}
				}
			}

			/* insert slave GIDs with memcpy, starting at slave's base index */
			gid_entry_mbox = (struct mlx4_roce_gid_entry *)(inbox->buf);
			for (i = 0, offset = base; i < num_gids; gid_entry_mbox++, offset++, i++)
				memcpy(priv->port[port].gid_table.roce_gids[offset].raw,
				       gid_entry_mbox->raw, MLX4_ROCE_GID_ENTRY_SIZE);

			/* Now, copy roce port gids table to current mailbox for passing to FW */
			gid_entry_mbox = (struct mlx4_roce_gid_entry *)(inbox->buf);
			for (i = 0; i < MLX4_ROCE_MAX_GIDS; gid_entry_mbox++, i++)
				memcpy(gid_entry_mbox->raw,
				       priv->port[port].gid_table.roce_gids[i].raw,
				       MLX4_ROCE_GID_ENTRY_SIZE);

			err = mlx4_cmd(dev, inbox->dma, in_mod & 0xffff, op_mod,
				       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
				       MLX4_CMD_NATIVE);
			mutex_unlock(&(priv->port[port].gid_table.mutex));
			return err;
		}

		return mlx4_cmd(dev, inbox->dma, in_mod & 0xffff, op_mod,
				MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
				MLX4_CMD_NATIVE);
	}

	/* Slaves are not allowed to SET_PORT beacon (LED) blink */
	if (op_mod == MLX4_SET_PORT_BEACON_OPCODE) {
		mlx4_warn(dev, "denying SET_PORT Beacon slave:%d\n", slave);
		return -EPERM;
	}

	/* For IB, we only consider:
	 * - The capability mask, which is set to the aggregate of all
	 *   slave function capabilities
	 * - The QKey violatin counter - reset according to each request.
	 */

	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		reset_qkey_viols = (*(u8 *) inbox->buf) & 0x40;
		new_cap_mask = ((__be32 *) inbox->buf)[2];
	} else {
		reset_qkey_viols = ((u8 *) inbox->buf)[3] & 0x1;
		new_cap_mask = ((__be32 *) inbox->buf)[1];
	}

	/* slave may not set the IS_SM capability for the port */
	if (slave != mlx4_master_func_num(dev) &&
	    (be32_to_cpu(new_cap_mask) & MLX4_PORT_CAP_IS_SM))
		return -EINVAL;

	/* No DEV_MGMT in multifunc mode */
	if (mlx4_is_mfunc(dev) &&
	    (be32_to_cpu(new_cap_mask) & MLX4_PORT_CAP_DEV_MGMT_SUP))
		return -EINVAL;

	agg_cap_mask = 0;
	slave_cap_mask =
		priv->mfunc.master.slave_state[slave].ib_cap_mask[port];
	priv->mfunc.master.slave_state[slave].ib_cap_mask[port] = new_cap_mask;
	for (i = 0; i < dev->num_slaves; i++)
		agg_cap_mask |=
			priv->mfunc.master.slave_state[i].ib_cap_mask[port];

	/* only clear mailbox for guests.  Master may be setting
	* MTU or PKEY table size
	*/
	if (slave != dev->caps.function)
		memset(inbox->buf, 0, 256);
	if (dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		*(u8 *) inbox->buf	   |= !!reset_qkey_viols << 6;
		((__be32 *) inbox->buf)[2] = agg_cap_mask;
	} else {
		((u8 *) inbox->buf)[3]     |= !!reset_qkey_viols;
		((__be32 *) inbox->buf)[1] = agg_cap_mask;
	}

	err = mlx4_cmd(dev, inbox->dma, port, is_eth, MLX4_CMD_SET_PORT,
		       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
	if (err)
		priv->mfunc.master.slave_state[slave].ib_cap_mask[port] =
			slave_cap_mask;
	return err;
}

int mlx4_SET_PORT_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int port = mlx4_slave_convert_port(
			dev, slave, vhcr->in_modifier & 0xFF);

	if (port < 0)
		return -EINVAL;

	vhcr->in_modifier = (vhcr->in_modifier & ~0xFF) |
			    (port & 0xFF);

	return mlx4_common_set_port(dev, slave, vhcr->in_modifier,
				    vhcr->op_modifier, inbox);
}

/* bit locations for set port command with zero op modifier */
enum {
	MLX4_SET_PORT_VL_CAP	 = 4, /* bits 7:4 */
	MLX4_SET_PORT_MTU_CAP	 = 12, /* bits 15:12 */
	MLX4_CHANGE_PORT_PKEY_TBL_SZ = 20,
	MLX4_CHANGE_PORT_VL_CAP	 = 21,
	MLX4_CHANGE_PORT_MTU_CAP = 22,
};

int mlx4_SET_PORT(struct mlx4_dev *dev, u8 port, int pkey_tbl_sz)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err, vl_cap, pkey_tbl_flag = 0;

	if (dev->caps.port_type[port] == MLX4_PORT_TYPE_ETH)
		return 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	((__be32 *) mailbox->buf)[1] = dev->caps.ib_port_def_cap[port];

	if (pkey_tbl_sz >= 0 && mlx4_is_master(dev)) {
		pkey_tbl_flag = 1;
		((__be16 *) mailbox->buf)[20] = cpu_to_be16(pkey_tbl_sz);
	}

	/* IB VL CAP enum isn't used by the firmware, just numerical values */
	for (vl_cap = 8; vl_cap >= 1; vl_cap >>= 1) {
		((__be32 *) mailbox->buf)[0] = cpu_to_be32(
			(1 << MLX4_CHANGE_PORT_MTU_CAP) |
			(1 << MLX4_CHANGE_PORT_VL_CAP)  |
			(pkey_tbl_flag << MLX4_CHANGE_PORT_PKEY_TBL_SZ) |
			(dev->caps.port_ib_mtu[port] << MLX4_SET_PORT_MTU_CAP) |
			(vl_cap << MLX4_SET_PORT_VL_CAP));
		err = mlx4_cmd(dev, mailbox->dma, port,
			       MLX4_SET_PORT_IB_OPCODE, MLX4_CMD_SET_PORT,
			       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_WRAPPED);
		if (err != -ENOMEM)
			break;
	}

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

#define SET_PORT_ROCE_2_FLAGS          0x10
#define MLX4_SET_PORT_ROCE_V1_V2       0x2
int mlx4_SET_PORT_general(struct mlx4_dev *dev, u8 port, int mtu,
			  u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_general_context *context;
	int err;
	u32 in_mod;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	context = mailbox->buf;
	context->flags = SET_PORT_GEN_ALL_VALID;
	context->mtu = cpu_to_be16(mtu);
	context->pptx = (pptx * (!pfctx)) << 7;
	context->pfctx = pfctx;
	context->pprx = (pprx * (!pfcrx)) << 7;
	context->pfcrx = pfcrx;

	if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ROCE_V1_V2) {
		context->flags |= SET_PORT_ROCE_2_FLAGS;
		context->roce_mode |=
			MLX4_SET_PORT_ROCE_V1_V2 << 4;
	}
	in_mod = MLX4_SET_PORT_GENERAL << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, MLX4_SET_PORT_ETH_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_general);

int mlx4_SET_PORT_qpn_calc(struct mlx4_dev *dev, u8 port, u32 base_qpn,
			   u8 promisc)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_rqp_calc_context *context;
	int err;
	u32 in_mod;
	u32 m_promisc = (dev->caps.flags & MLX4_DEV_CAP_FLAG_VEP_MC_STEER) ?
		MCAST_DIRECT : MCAST_DEFAULT;

	if (dev->caps.steering_mode != MLX4_STEERING_MODE_A0)
		return 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	context = mailbox->buf;
	context->base_qpn = cpu_to_be32(base_qpn);
	context->n_mac = dev->caps.log_num_macs;
	context->promisc = cpu_to_be32(promisc << SET_PORT_PROMISC_SHIFT |
				       base_qpn);
	context->mcast = cpu_to_be32(m_promisc << SET_PORT_MC_PROMISC_SHIFT |
				     base_qpn);
	context->intra_no_vlan = 0;
	context->no_vlan = MLX4_NO_VLAN_IDX;
	context->intra_vlan_miss = 0;
	context->vlan_miss = MLX4_VLAN_MISS_IDX;

	in_mod = MLX4_SET_PORT_RQP_CALC << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, MLX4_SET_PORT_ETH_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_qpn_calc);

int mlx4_SET_PORT_fcs_check(struct mlx4_dev *dev, u8 port, u8 ignore_fcs_value)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_general_context *context;
	u32 in_mod;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	context = mailbox->buf;
	context->v_ignore_fcs |= MLX4_FLAG_V_IGNORE_FCS_MASK;
	if (ignore_fcs_value)
		context->ignore_fcs |= MLX4_IGNORE_FCS_MASK;
	else
		context->ignore_fcs &= ~MLX4_IGNORE_FCS_MASK;

	in_mod = MLX4_SET_PORT_GENERAL << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, 1, MLX4_CMD_SET_PORT,
		       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_fcs_check);

enum {
	VXLAN_ENABLE_MODIFY	= 1 << 7,
	VXLAN_STEERING_MODIFY	= 1 << 6,

	VXLAN_ENABLE		= 1 << 7,
};

struct mlx4_set_port_vxlan_context {
	u32	reserved1;
	u8	modify_flags;
	u8	reserved2;
	u8	enable_flags;
	u8	steering;
};

int mlx4_SET_PORT_VXLAN(struct mlx4_dev *dev, u8 port, u8 steering, int enable)
{
	int err;
	u32 in_mod;
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_set_port_vxlan_context  *context;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	context = mailbox->buf;
	memset(context, 0, sizeof(*context));

	context->modify_flags = VXLAN_ENABLE_MODIFY | VXLAN_STEERING_MODIFY;
	if (enable)
		context->enable_flags = VXLAN_ENABLE;
	context->steering  = steering;

	in_mod = MLX4_SET_PORT_VXLAN << 8 | port;
	err = mlx4_cmd(dev, mailbox->dma, in_mod, MLX4_SET_PORT_ETH_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_VXLAN);

int mlx4_SET_PORT_BEACON(struct mlx4_dev *dev, u8 port, u16 time)
{
	int err;
	struct mlx4_cmd_mailbox *mailbox;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	*((__be32 *)mailbox->buf) = cpu_to_be32(time);

	err = mlx4_cmd(dev, mailbox->dma, port, MLX4_SET_PORT_BEACON_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL(mlx4_SET_PORT_BEACON);

int mlx4_SET_MCAST_FLTR_wrapper(struct mlx4_dev *dev, int slave,
				struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd)
{
	int err = 0;

	return err;
}

int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port,
			u64 mac, u64 clear, u8 mode)
{
	return mlx4_cmd(dev, (mac | (clear << 63)), port, mode,
			MLX4_CMD_SET_MCAST_FLTR, MLX4_CMD_TIME_CLASS_B,
			MLX4_CMD_WRAPPED);
}
EXPORT_SYMBOL(mlx4_SET_MCAST_FLTR);

int mlx4_SET_VLAN_FLTR_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd)
{
	int err = 0;

	return err;
}

int mlx4_DUMP_ETH_STATS_wrapper(struct mlx4_dev *dev, int slave,
				struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd)
{
	return 0;
}

int mlx4_get_slave_from_roce_gid(struct mlx4_dev *dev, int port, u8 *gid,
				 int *slave_id)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i, found_ix = -1;
	int vf_gids = MLX4_ROCE_MAX_GIDS - MLX4_ROCE_PF_GIDS;
	struct mlx4_slaves_pport slaves_pport;
	unsigned num_vfs;
	int slave_gid;

	if (!mlx4_is_mfunc(dev))
		return -EINVAL;

	slaves_pport = mlx4_phys_to_slaves_pport(dev, port);
	num_vfs = bitmap_weight(slaves_pport.slaves,
				dev->persist->num_vfs + 1) - 1;

	for (i = 0; i < MLX4_ROCE_MAX_GIDS; i++) {
		if (!memcmp(priv->port[port].gid_table.roce_gids[i].raw, gid,
			    MLX4_ROCE_GID_ENTRY_SIZE)) {
			found_ix = i;
			break;
		}
	}

	if (found_ix >= 0) {
		/* Calculate a slave_gid which is the slave number in the gid
		 * table and not a globally unique slave number.
		 */
		if (found_ix < MLX4_ROCE_PF_GIDS)
			slave_gid = 0;
		else if (found_ix < MLX4_ROCE_PF_GIDS + (vf_gids % num_vfs) *
			 (vf_gids / num_vfs + 1))
			slave_gid = ((found_ix - MLX4_ROCE_PF_GIDS) /
				     (vf_gids / num_vfs + 1)) + 1;
		else
			slave_gid =
			((found_ix - MLX4_ROCE_PF_GIDS -
			  ((vf_gids % num_vfs) * ((vf_gids / num_vfs + 1)))) /
			 (vf_gids / num_vfs)) + vf_gids % num_vfs + 1;

		/* Calculate the globally unique slave id */
		if (slave_gid) {
			struct mlx4_active_ports exclusive_ports;
			struct mlx4_active_ports actv_ports;
			struct mlx4_slaves_pport slaves_pport_actv;
			unsigned max_port_p_one;
			int num_vfs_before = 0;
			int candidate_slave_gid;

			/* Calculate how many VFs are on the previous port, if exists */
			for (i = 1; i < port; i++) {
				bitmap_zero(exclusive_ports.ports, dev->caps.num_ports);
				set_bit(i - 1, exclusive_ports.ports);
				slaves_pport_actv =
					mlx4_phys_to_slaves_pport_actv(
							dev, &exclusive_ports);
				num_vfs_before += bitmap_weight(
						slaves_pport_actv.slaves,
						dev->persist->num_vfs + 1);
			}

			/* candidate_slave_gid isn't necessarily the correct slave, but
			 * it has the same number of ports and is assigned to the same
			 * ports as the real slave we're looking for. On dual port VF,
			 * slave_gid = [single port VFs on port <port>] +
			 * [offset of the current slave from the first dual port VF] +
			 * 1 (for the PF).
			 */
			candidate_slave_gid = slave_gid + num_vfs_before;

			actv_ports = mlx4_get_active_ports(dev, candidate_slave_gid);
			max_port_p_one = find_first_bit(
				actv_ports.ports, dev->caps.num_ports) +
				bitmap_weight(actv_ports.ports,
					      dev->caps.num_ports) + 1;

			/* Calculate the real slave number */
			for (i = 1; i < max_port_p_one; i++) {
				if (i == port)
					continue;
				bitmap_zero(exclusive_ports.ports,
					    dev->caps.num_ports);
				set_bit(i - 1, exclusive_ports.ports);
				slaves_pport_actv =
					mlx4_phys_to_slaves_pport_actv(
						dev, &exclusive_ports);
				slave_gid += bitmap_weight(
						slaves_pport_actv.slaves,
						dev->persist->num_vfs + 1);
			}
		}
		*slave_id = slave_gid;
	}

	return (found_ix >= 0) ? 0 : -EINVAL;
}
EXPORT_SYMBOL(mlx4_get_slave_from_roce_gid);

int mlx4_get_roce_gid_from_slave(struct mlx4_dev *dev, int port, int slave_id,
				 u8 *gid)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (!mlx4_is_master(dev))
		return -EINVAL;

	memcpy(gid, priv->port[port].gid_table.roce_gids[slave_id].raw,
	       MLX4_ROCE_GID_ENTRY_SIZE);
	return 0;
}
EXPORT_SYMBOL(mlx4_get_roce_gid_from_slave);

/* Cable Module Info */
#define MODULE_INFO_MAX_READ 48

#define I2C_ADDR_LOW  0x50
#define I2C_ADDR_HIGH 0x51
#define I2C_PAGE_SIZE 256

/* Module Info Data */
struct mlx4_cable_info {
	u8	i2c_addr;
	u8	page_num;
	__be16	dev_mem_address;
	__be16	reserved1;
	__be16	size;
	__be32	reserved2[2];
	u8	data[MODULE_INFO_MAX_READ];
};

enum cable_info_err {
	 CABLE_INF_INV_PORT      = 0x1,
	 CABLE_INF_OP_NOSUP      = 0x2,
	 CABLE_INF_NOT_CONN      = 0x3,
	 CABLE_INF_NO_EEPRM      = 0x4,
	 CABLE_INF_PAGE_ERR      = 0x5,
	 CABLE_INF_INV_ADDR      = 0x6,
	 CABLE_INF_I2C_ADDR      = 0x7,
	 CABLE_INF_QSFP_VIO      = 0x8,
	 CABLE_INF_I2C_BUSY      = 0x9,
};

#define MAD_STATUS_2_CABLE_ERR(mad_status) ((mad_status >> 8) & 0xFF)

static inline const char *cable_info_mad_err_str(u16 mad_status)
{
	u8 err = MAD_STATUS_2_CABLE_ERR(mad_status);

	switch (err) {
	case CABLE_INF_INV_PORT:
		return "invalid port selected";
	case CABLE_INF_OP_NOSUP:
		return "operation not supported for this port (the port is of type CX4 or internal)";
	case CABLE_INF_NOT_CONN:
		return "cable is not connected";
	case CABLE_INF_NO_EEPRM:
		return "the connected cable has no EPROM (passive copper cable)";
	case CABLE_INF_PAGE_ERR:
		return "page number is greater than 15";
	case CABLE_INF_INV_ADDR:
		return "invalid device_address or size (that is, size equals 0 or address+size is greater than 256)";
	case CABLE_INF_I2C_ADDR:
		return "invalid I2C slave address";
	case CABLE_INF_QSFP_VIO:
		return "at least one cable violates the QSFP specification and ignores the modsel signal";
	case CABLE_INF_I2C_BUSY:
		return "I2C bus is constantly busy";
	}
	return "Unknown Error";
}

/**
 * mlx4_get_module_info - Read cable module eeprom data
 * @dev: mlx4_dev.
 * @port: port number.
 * @offset: byte offset in eeprom to start reading data from.
 * @size: num of bytes to read.
 * @data: output buffer to put the requested data into.
 *
 * Reads cable module eeprom data, puts the outcome data into
 * data pointer paramer.
 * Returns num of read bytes on success or a negative error
 * code.
 */
int mlx4_get_module_info(struct mlx4_dev *dev, u8 port,
			 u16 offset, u16 size, u8 *data)
{
	struct mlx4_cmd_mailbox *inbox, *outbox;
	struct mlx4_mad_ifc *inmad, *outmad;
	struct mlx4_cable_info *cable_info;
	u16 i2c_addr;
	int ret;

	if (size > MODULE_INFO_MAX_READ)
		size = MODULE_INFO_MAX_READ;

	inbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(inbox))
		return PTR_ERR(inbox);

	outbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(outbox)) {
		mlx4_free_cmd_mailbox(dev, inbox);
		return PTR_ERR(outbox);
	}

	inmad = (struct mlx4_mad_ifc *)(inbox->buf);
	outmad = (struct mlx4_mad_ifc *)(outbox->buf);

	inmad->method = 0x1; /* Get */
	inmad->class_version = 0x1;
	inmad->mgmt_class = 0x1;
	inmad->base_version = 0x1;
	inmad->attr_id = cpu_to_be16(0xFF60); /* Module Info */

	if (offset < I2C_PAGE_SIZE && offset + size > I2C_PAGE_SIZE)
		/* Cross pages reads are not allowed
		 * read until offset 256 in low page
		 */
		size -= offset + size - I2C_PAGE_SIZE;

	i2c_addr = I2C_ADDR_LOW;
	if (offset >= I2C_PAGE_SIZE) {
		/* Reset offset to high page */
		i2c_addr = I2C_ADDR_HIGH;
		offset -= I2C_PAGE_SIZE;
	}

	cable_info = (struct mlx4_cable_info *)inmad->data;
	cable_info->dev_mem_address = cpu_to_be16(offset);
	cable_info->page_num = 0;
	cable_info->i2c_addr = i2c_addr;
	cable_info->size = cpu_to_be16(size);

	ret = mlx4_cmd_box(dev, inbox->dma, outbox->dma, port, 3,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_NATIVE);
	if (ret)
		goto out;

	if (be16_to_cpu(outmad->status)) {
		/* Mad returned with bad status */
		ret = be16_to_cpu(outmad->status);
		mlx4_warn(dev,
			  "MLX4_CMD_MAD_IFC Get Module info attr(%x) port(%d) i2c_addr(%x) offset(%d) size(%d): Response Mad Status(%x) - %s\n",
			  0xFF60, port, i2c_addr, offset, size,
			  ret, cable_info_mad_err_str(ret));

		if (i2c_addr == I2C_ADDR_HIGH &&
		    MAD_STATUS_2_CABLE_ERR(ret) == CABLE_INF_I2C_ADDR)
			/* Some SFP cables do not support i2c slave
			 * address 0x51 (high page), abort silently.
			 */
			ret = 0;
		else
			ret = -ret;
		goto out;
	}
	cable_info = (struct mlx4_cable_info *)outmad->data;
	memcpy(data, cable_info->data, size);
	ret = size;
out:
	mlx4_free_cmd_mailbox(dev, inbox);
	mlx4_free_cmd_mailbox(dev, outbox);
	return ret;
}
EXPORT_SYMBOL(mlx4_get_module_info);

int mlx4_max_tc(struct mlx4_dev *dev)
{
	u8 num_tc = dev->caps.max_tc_eth;

	if (!num_tc)
		num_tc = MLX4_TC_MAX_NUMBER;

	return num_tc;
}
EXPORT_SYMBOL(mlx4_max_tc);
