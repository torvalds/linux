/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include <linux/netdevice.h>
#include <net/bonding.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/vport.h>
#include "lib/devcom.h"
#include "mlx5_core.h"
#include "eswitch.h"
#include "esw/acl/ofld.h"
#include "lag.h"
#include "mp.h"
#include "mpesw.h"

enum {
	MLX5_LAG_EGRESS_PORT_1 = 1,
	MLX5_LAG_EGRESS_PORT_2,
};

/* General purpose, use for short periods of time.
 * Beware of lock dependencies (preferably, no locks should be acquired
 * under it).
 */
static DEFINE_SPINLOCK(lag_lock);

static int get_port_sel_mode(enum mlx5_lag_mode mode, unsigned long flags)
{
	if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &flags))
		return MLX5_LAG_PORT_SELECT_MODE_PORT_SELECT_FT;

	if (mode == MLX5_LAG_MODE_MPESW)
		return MLX5_LAG_PORT_SELECT_MODE_PORT_SELECT_MPESW;

	return MLX5_LAG_PORT_SELECT_MODE_QUEUE_AFFINITY;
}

static u8 lag_active_port_bits(struct mlx5_lag *ldev)
{
	u8 enabled_ports[MLX5_MAX_PORTS] = {};
	u8 active_port = 0;
	int num_enabled;
	int idx;

	mlx5_infer_tx_enabled(&ldev->tracker, ldev->ports, enabled_ports,
			      &num_enabled);
	for (idx = 0; idx < num_enabled; idx++)
		active_port |= BIT_MASK(enabled_ports[idx]);

	return active_port;
}

static int mlx5_cmd_create_lag(struct mlx5_core_dev *dev, u8 *ports, int mode,
			       unsigned long flags)
{
	bool fdb_sel_mode = test_bit(MLX5_LAG_MODE_FLAG_FDB_SEL_MODE_NATIVE,
				     &flags);
	int port_sel_mode = get_port_sel_mode(mode, flags);
	u32 in[MLX5_ST_SZ_DW(create_lag_in)] = {};
	void *lag_ctx;

	lag_ctx = MLX5_ADDR_OF(create_lag_in, in, ctx);
	MLX5_SET(create_lag_in, in, opcode, MLX5_CMD_OP_CREATE_LAG);
	MLX5_SET(lagc, lag_ctx, fdb_selection_mode, fdb_sel_mode);

	switch (port_sel_mode) {
	case MLX5_LAG_PORT_SELECT_MODE_QUEUE_AFFINITY:
		MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, ports[0]);
		MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, ports[1]);
		break;
	case MLX5_LAG_PORT_SELECT_MODE_PORT_SELECT_FT:
		if (!MLX5_CAP_PORT_SELECTION(dev, port_select_flow_table_bypass))
			break;

		MLX5_SET(lagc, lag_ctx, active_port,
			 lag_active_port_bits(mlx5_lag_dev(dev)));
		break;
	default:
		break;
	}
	MLX5_SET(lagc, lag_ctx, port_select_mode, port_sel_mode);

	return mlx5_cmd_exec_in(dev, create_lag, in);
}

static int mlx5_cmd_modify_lag(struct mlx5_core_dev *dev, u8 num_ports,
			       u8 *ports)
{
	u32 in[MLX5_ST_SZ_DW(modify_lag_in)] = {};
	void *lag_ctx = MLX5_ADDR_OF(modify_lag_in, in, ctx);

	MLX5_SET(modify_lag_in, in, opcode, MLX5_CMD_OP_MODIFY_LAG);
	MLX5_SET(modify_lag_in, in, field_select, 0x1);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, ports[0]);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, ports[1]);

	return mlx5_cmd_exec_in(dev, modify_lag, in);
}

int mlx5_cmd_create_vport_lag(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(create_vport_lag_in)] = {};

	MLX5_SET(create_vport_lag_in, in, opcode, MLX5_CMD_OP_CREATE_VPORT_LAG);

	return mlx5_cmd_exec_in(dev, create_vport_lag, in);
}
EXPORT_SYMBOL(mlx5_cmd_create_vport_lag);

int mlx5_cmd_destroy_vport_lag(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(destroy_vport_lag_in)] = {};

	MLX5_SET(destroy_vport_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_VPORT_LAG);

	return mlx5_cmd_exec_in(dev, destroy_vport_lag, in);
}
EXPORT_SYMBOL(mlx5_cmd_destroy_vport_lag);

static void mlx5_infer_tx_disabled(struct lag_tracker *tracker, u8 num_ports,
				   u8 *ports, int *num_disabled)
{
	int i;

	*num_disabled = 0;
	for (i = 0; i < num_ports; i++) {
		if (!tracker->netdev_state[i].tx_enabled ||
		    !tracker->netdev_state[i].link_up)
			ports[(*num_disabled)++] = i;
	}
}

void mlx5_infer_tx_enabled(struct lag_tracker *tracker, u8 num_ports,
			   u8 *ports, int *num_enabled)
{
	int i;

	*num_enabled = 0;
	for (i = 0; i < num_ports; i++) {
		if (tracker->netdev_state[i].tx_enabled &&
		    tracker->netdev_state[i].link_up)
			ports[(*num_enabled)++] = i;
	}

	if (*num_enabled == 0)
		mlx5_infer_tx_disabled(tracker, num_ports, ports, num_enabled);
}

static void mlx5_lag_print_mapping(struct mlx5_core_dev *dev,
				   struct mlx5_lag *ldev,
				   struct lag_tracker *tracker,
				   unsigned long flags)
{
	char buf[MLX5_MAX_PORTS * 10 + 1] = {};
	u8 enabled_ports[MLX5_MAX_PORTS] = {};
	int written = 0;
	int num_enabled;
	int idx;
	int err;
	int i;
	int j;

	if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &flags)) {
		mlx5_infer_tx_enabled(tracker, ldev->ports, enabled_ports,
				      &num_enabled);
		for (i = 0; i < num_enabled; i++) {
			err = scnprintf(buf + written, 4, "%d, ", enabled_ports[i] + 1);
			if (err != 3)
				return;
			written += err;
		}
		buf[written - 2] = 0;
		mlx5_core_info(dev, "lag map active ports: %s\n", buf);
	} else {
		for (i = 0; i < ldev->ports; i++) {
			for (j  = 0; j < ldev->buckets; j++) {
				idx = i * ldev->buckets + j;
				err = scnprintf(buf + written, 10,
						" port %d:%d", i + 1, ldev->v2p_map[idx]);
				if (err != 9)
					return;
				written += err;
			}
		}
		mlx5_core_info(dev, "lag map:%s\n", buf);
	}
}

static int mlx5_lag_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr);
static void mlx5_do_bond_work(struct work_struct *work);

static void mlx5_ldev_free(struct kref *ref)
{
	struct mlx5_lag *ldev = container_of(ref, struct mlx5_lag, ref);

	if (ldev->nb.notifier_call)
		unregister_netdevice_notifier_net(&init_net, &ldev->nb);
	mlx5_lag_mp_cleanup(ldev);
	destroy_workqueue(ldev->wq);
	mlx5_lag_mpesw_cleanup(ldev);
	mutex_destroy(&ldev->lock);
	kfree(ldev);
}

static void mlx5_ldev_put(struct mlx5_lag *ldev)
{
	kref_put(&ldev->ref, mlx5_ldev_free);
}

static void mlx5_ldev_get(struct mlx5_lag *ldev)
{
	kref_get(&ldev->ref);
}

static struct mlx5_lag *mlx5_lag_dev_alloc(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	int err;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return NULL;

	ldev->wq = create_singlethread_workqueue("mlx5_lag");
	if (!ldev->wq) {
		kfree(ldev);
		return NULL;
	}

	kref_init(&ldev->ref);
	mutex_init(&ldev->lock);
	INIT_DELAYED_WORK(&ldev->bond_work, mlx5_do_bond_work);

	ldev->nb.notifier_call = mlx5_lag_netdev_event;
	if (register_netdevice_notifier_net(&init_net, &ldev->nb)) {
		ldev->nb.notifier_call = NULL;
		mlx5_core_err(dev, "Failed to register LAG netdev notifier\n");
	}
	ldev->mode = MLX5_LAG_MODE_NONE;

	err = mlx5_lag_mp_init(ldev);
	if (err)
		mlx5_core_err(dev, "Failed to init multipath lag err=%d\n",
			      err);

	mlx5_lag_mpesw_init(ldev);
	ldev->ports = MLX5_CAP_GEN(dev, num_lag_ports);
	ldev->buckets = 1;

	return ldev;
}

int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				struct net_device *ndev)
{
	int i;

	for (i = 0; i < ldev->ports; i++)
		if (ldev->pf[i].netdev == ndev)
			return i;

	return -ENOENT;
}

static bool __mlx5_lag_is_roce(struct mlx5_lag *ldev)
{
	return ldev->mode == MLX5_LAG_MODE_ROCE;
}

static bool __mlx5_lag_is_sriov(struct mlx5_lag *ldev)
{
	return ldev->mode == MLX5_LAG_MODE_SRIOV;
}

/* Create a mapping between steering slots and active ports.
 * As we have ldev->buckets slots per port first assume the native
 * mapping should be used.
 * If there are ports that are disabled fill the relevant slots
 * with mapping that points to active ports.
 */
static void mlx5_infer_tx_affinity_mapping(struct lag_tracker *tracker,
					   u8 num_ports,
					   u8 buckets,
					   u8 *ports)
{
	int disabled[MLX5_MAX_PORTS] = {};
	int enabled[MLX5_MAX_PORTS] = {};
	int disabled_ports_num = 0;
	int enabled_ports_num = 0;
	int idx;
	u32 rand;
	int i;
	int j;

	for (i = 0; i < num_ports; i++) {
		if (tracker->netdev_state[i].tx_enabled &&
		    tracker->netdev_state[i].link_up)
			enabled[enabled_ports_num++] = i;
		else
			disabled[disabled_ports_num++] = i;
	}

	/* Use native mapping by default where each port's buckets
	 * point the native port: 1 1 1 .. 1 2 2 2 ... 2 3 3 3 ... 3 etc
	 */
	for (i = 0; i < num_ports; i++)
		for (j = 0; j < buckets; j++) {
			idx = i * buckets + j;
			ports[idx] = MLX5_LAG_EGRESS_PORT_1 + i;
		}

	/* If all ports are disabled/enabled keep native mapping */
	if (enabled_ports_num == num_ports ||
	    disabled_ports_num == num_ports)
		return;

	/* Go over the disabled ports and for each assign a random active port */
	for (i = 0; i < disabled_ports_num; i++) {
		for (j = 0; j < buckets; j++) {
			get_random_bytes(&rand, 4);
			ports[disabled[i] * buckets + j] = enabled[rand % enabled_ports_num] + 1;
		}
	}
}

static bool mlx5_lag_has_drop_rule(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < ldev->ports; i++)
		if (ldev->pf[i].has_drop)
			return true;
	return false;
}

static void mlx5_lag_drop_rule_cleanup(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < ldev->ports; i++) {
		if (!ldev->pf[i].has_drop)
			continue;

		mlx5_esw_acl_ingress_vport_drop_rule_destroy(ldev->pf[i].dev->priv.eswitch,
							     MLX5_VPORT_UPLINK);
		ldev->pf[i].has_drop = false;
	}
}

static void mlx5_lag_drop_rule_setup(struct mlx5_lag *ldev,
				     struct lag_tracker *tracker)
{
	u8 disabled_ports[MLX5_MAX_PORTS] = {};
	struct mlx5_core_dev *dev;
	int disabled_index;
	int num_disabled;
	int err;
	int i;

	/* First delete the current drop rule so there won't be any dropped
	 * packets
	 */
	mlx5_lag_drop_rule_cleanup(ldev);

	if (!ldev->tracker.has_inactive)
		return;

	mlx5_infer_tx_disabled(tracker, ldev->ports, disabled_ports, &num_disabled);

	for (i = 0; i < num_disabled; i++) {
		disabled_index = disabled_ports[i];
		dev = ldev->pf[disabled_index].dev;
		err = mlx5_esw_acl_ingress_vport_drop_rule_create(dev->priv.eswitch,
								  MLX5_VPORT_UPLINK);
		if (!err)
			ldev->pf[disabled_index].has_drop = true;
		else
			mlx5_core_err(dev,
				      "Failed to create lag drop rule, error: %d", err);
	}
}

static int mlx5_cmd_modify_active_port(struct mlx5_core_dev *dev, u8 ports)
{
	u32 in[MLX5_ST_SZ_DW(modify_lag_in)] = {};
	void *lag_ctx;

	lag_ctx = MLX5_ADDR_OF(modify_lag_in, in, ctx);

	MLX5_SET(modify_lag_in, in, opcode, MLX5_CMD_OP_MODIFY_LAG);
	MLX5_SET(modify_lag_in, in, field_select, 0x2);

	MLX5_SET(lagc, lag_ctx, active_port, ports);

	return mlx5_cmd_exec_in(dev, modify_lag, in);
}

static int _mlx5_modify_lag(struct mlx5_lag *ldev, u8 *ports)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	u8 active_ports;
	int ret;

	if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &ldev->mode_flags)) {
		ret = mlx5_lag_port_sel_modify(ldev, ports);
		if (ret ||
		    !MLX5_CAP_PORT_SELECTION(dev0, port_select_flow_table_bypass))
			return ret;

		active_ports = lag_active_port_bits(ldev);

		return mlx5_cmd_modify_active_port(dev0, active_ports);
	}
	return mlx5_cmd_modify_lag(dev0, ldev->ports, ports);
}

void mlx5_modify_lag(struct mlx5_lag *ldev,
		     struct lag_tracker *tracker)
{
	u8 ports[MLX5_MAX_PORTS * MLX5_LAG_MAX_HASH_BUCKETS] = {};
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	int idx;
	int err;
	int i;
	int j;

	mlx5_infer_tx_affinity_mapping(tracker, ldev->ports, ldev->buckets, ports);

	for (i = 0; i < ldev->ports; i++) {
		for (j = 0; j < ldev->buckets; j++) {
			idx = i * ldev->buckets + j;
			if (ports[idx] == ldev->v2p_map[idx])
				continue;
			err = _mlx5_modify_lag(ldev, ports);
			if (err) {
				mlx5_core_err(dev0,
					      "Failed to modify LAG (%d)\n",
					      err);
				return;
			}
			memcpy(ldev->v2p_map, ports, sizeof(ports));

			mlx5_lag_print_mapping(dev0, ldev, tracker,
					       ldev->mode_flags);
			break;
		}
	}

	if (tracker->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP &&
	    !(ldev->mode == MLX5_LAG_MODE_ROCE))
		mlx5_lag_drop_rule_setup(ldev, tracker);
}

static int mlx5_lag_set_port_sel_mode_roce(struct mlx5_lag *ldev,
					   unsigned long *flags)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;

	if (!MLX5_CAP_PORT_SELECTION(dev0, port_select_flow_table)) {
		if (ldev->ports > 2)
			return -EINVAL;
		return 0;
	}

	if (ldev->ports > 2)
		ldev->buckets = MLX5_LAG_MAX_HASH_BUCKETS;

	set_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, flags);

	return 0;
}

static void mlx5_lag_set_port_sel_mode_offloads(struct mlx5_lag *ldev,
						struct lag_tracker *tracker,
						enum mlx5_lag_mode mode,
						unsigned long *flags)
{
	struct lag_func *dev0 = &ldev->pf[MLX5_LAG_P1];

	if (mode == MLX5_LAG_MODE_MPESW)
		return;

	if (MLX5_CAP_PORT_SELECTION(dev0->dev, port_select_flow_table) &&
	    tracker->tx_type == NETDEV_LAG_TX_TYPE_HASH)
		set_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, flags);
}

static int mlx5_lag_set_flags(struct mlx5_lag *ldev, enum mlx5_lag_mode mode,
			      struct lag_tracker *tracker, bool shared_fdb,
			      unsigned long *flags)
{
	bool roce_lag = mode == MLX5_LAG_MODE_ROCE;

	*flags = 0;
	if (shared_fdb) {
		set_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, flags);
		set_bit(MLX5_LAG_MODE_FLAG_FDB_SEL_MODE_NATIVE, flags);
	}

	if (mode == MLX5_LAG_MODE_MPESW)
		set_bit(MLX5_LAG_MODE_FLAG_FDB_SEL_MODE_NATIVE, flags);

	if (roce_lag)
		return mlx5_lag_set_port_sel_mode_roce(ldev, flags);

	mlx5_lag_set_port_sel_mode_offloads(ldev, tracker, mode, flags);
	return 0;
}

char *mlx5_get_str_port_sel_mode(enum mlx5_lag_mode mode, unsigned long flags)
{
	int port_sel_mode = get_port_sel_mode(mode, flags);

	switch (port_sel_mode) {
	case MLX5_LAG_PORT_SELECT_MODE_QUEUE_AFFINITY: return "queue_affinity";
	case MLX5_LAG_PORT_SELECT_MODE_PORT_SELECT_FT: return "hash";
	case MLX5_LAG_PORT_SELECT_MODE_PORT_SELECT_MPESW: return "mpesw";
	default: return "invalid";
	}
}

static int mlx5_create_lag(struct mlx5_lag *ldev,
			   struct lag_tracker *tracker,
			   enum mlx5_lag_mode mode,
			   unsigned long flags)
{
	bool shared_fdb = test_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, &flags);
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	u32 in[MLX5_ST_SZ_DW(destroy_lag_in)] = {};
	int err;

	if (tracker)
		mlx5_lag_print_mapping(dev0, ldev, tracker, flags);
	mlx5_core_info(dev0, "shared_fdb:%d mode:%s\n",
		       shared_fdb, mlx5_get_str_port_sel_mode(mode, flags));

	err = mlx5_cmd_create_lag(dev0, ldev->v2p_map, mode, flags);
	if (err) {
		mlx5_core_err(dev0,
			      "Failed to create LAG (%d)\n",
			      err);
		return err;
	}

	if (shared_fdb) {
		err = mlx5_eswitch_offloads_config_single_fdb(dev0->priv.eswitch,
							      dev1->priv.eswitch);
		if (err)
			mlx5_core_err(dev0, "Can't enable single FDB mode\n");
		else
			mlx5_core_info(dev0, "Operation mode is single FDB\n");
	}

	if (err) {
		MLX5_SET(destroy_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_LAG);
		if (mlx5_cmd_exec_in(dev0, destroy_lag, in))
			mlx5_core_err(dev0,
				      "Failed to deactivate RoCE LAG; driver restart required\n");
	}

	return err;
}

int mlx5_activate_lag(struct mlx5_lag *ldev,
		      struct lag_tracker *tracker,
		      enum mlx5_lag_mode mode,
		      bool shared_fdb)
{
	bool roce_lag = mode == MLX5_LAG_MODE_ROCE;
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	unsigned long flags = 0;
	int err;

	err = mlx5_lag_set_flags(ldev, mode, tracker, shared_fdb, &flags);
	if (err)
		return err;

	if (mode != MLX5_LAG_MODE_MPESW) {
		mlx5_infer_tx_affinity_mapping(tracker, ldev->ports, ldev->buckets, ldev->v2p_map);
		if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &flags)) {
			err = mlx5_lag_port_sel_create(ldev, tracker->hash_type,
						       ldev->v2p_map);
			if (err) {
				mlx5_core_err(dev0,
					      "Failed to create LAG port selection(%d)\n",
					      err);
				return err;
			}
		}
	}

	err = mlx5_create_lag(ldev, tracker, mode, flags);
	if (err) {
		if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &flags))
			mlx5_lag_port_sel_destroy(ldev);
		if (roce_lag)
			mlx5_core_err(dev0,
				      "Failed to activate RoCE LAG\n");
		else
			mlx5_core_err(dev0,
				      "Failed to activate VF LAG\n"
				      "Make sure all VFs are unbound prior to VF LAG activation or deactivation\n");
		return err;
	}

	if (tracker && tracker->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP &&
	    !roce_lag)
		mlx5_lag_drop_rule_setup(ldev, tracker);

	ldev->mode = mode;
	ldev->mode_flags = flags;
	return 0;
}

static int mlx5_deactivate_lag(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	u32 in[MLX5_ST_SZ_DW(destroy_lag_in)] = {};
	bool roce_lag = __mlx5_lag_is_roce(ldev);
	unsigned long flags = ldev->mode_flags;
	int err;

	ldev->mode = MLX5_LAG_MODE_NONE;
	ldev->mode_flags = 0;
	mlx5_lag_mp_reset(ldev);

	if (test_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, &flags)) {
		mlx5_eswitch_offloads_destroy_single_fdb(dev0->priv.eswitch,
							 dev1->priv.eswitch);
		clear_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, &flags);
	}

	MLX5_SET(destroy_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_LAG);
	err = mlx5_cmd_exec_in(dev0, destroy_lag, in);
	if (err) {
		if (roce_lag) {
			mlx5_core_err(dev0,
				      "Failed to deactivate RoCE LAG; driver restart required\n");
		} else {
			mlx5_core_err(dev0,
				      "Failed to deactivate VF LAG; driver restart required\n"
				      "Make sure all VFs are unbound prior to VF LAG activation or deactivation\n");
		}
		return err;
	}

	if (test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &flags))
		mlx5_lag_port_sel_destroy(ldev);
	if (mlx5_lag_has_drop_rule(ldev))
		mlx5_lag_drop_rule_cleanup(ldev);

	return 0;
}

#define MLX5_LAG_OFFLOADS_SUPPORTED_PORTS 2
static bool mlx5_lag_check_prereq(struct mlx5_lag *ldev)
{
#ifdef CONFIG_MLX5_ESWITCH
	struct mlx5_core_dev *dev;
	u8 mode;
#endif
	int i;

	for (i = 0; i < ldev->ports; i++)
		if (!ldev->pf[i].dev)
			return false;

#ifdef CONFIG_MLX5_ESWITCH
	for (i = 0; i < ldev->ports; i++) {
		dev = ldev->pf[i].dev;
		if (mlx5_eswitch_num_vfs(dev->priv.eswitch) && !is_mdev_switchdev_mode(dev))
			return false;
	}

	dev = ldev->pf[MLX5_LAG_P1].dev;
	mode = mlx5_eswitch_mode(dev);
	for (i = 0; i < ldev->ports; i++)
		if (mlx5_eswitch_mode(ldev->pf[i].dev) != mode)
			return false;

	if (mode == MLX5_ESWITCH_OFFLOADS && ldev->ports != MLX5_LAG_OFFLOADS_SUPPORTED_PORTS)
		return false;
#else
	for (i = 0; i < ldev->ports; i++)
		if (mlx5_sriov_is_enabled(ldev->pf[i].dev))
			return false;
#endif
	return true;
}

static void mlx5_lag_add_devices(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < ldev->ports; i++) {
		if (!ldev->pf[i].dev)
			continue;

		if (ldev->pf[i].dev->priv.flags &
		    MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)
			continue;

		ldev->pf[i].dev->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
		mlx5_rescan_drivers_locked(ldev->pf[i].dev);
	}
}

static void mlx5_lag_remove_devices(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < ldev->ports; i++) {
		if (!ldev->pf[i].dev)
			continue;

		if (ldev->pf[i].dev->priv.flags &
		    MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)
			continue;

		ldev->pf[i].dev->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
		mlx5_rescan_drivers_locked(ldev->pf[i].dev);
	}
}

void mlx5_disable_lag(struct mlx5_lag *ldev)
{
	bool shared_fdb = test_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, &ldev->mode_flags);
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	bool roce_lag;
	int err;
	int i;

	roce_lag = __mlx5_lag_is_roce(ldev);

	if (shared_fdb) {
		mlx5_lag_remove_devices(ldev);
	} else if (roce_lag) {
		if (!(dev0->priv.flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)) {
			dev0->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
			mlx5_rescan_drivers_locked(dev0);
		}
		for (i = 1; i < ldev->ports; i++)
			mlx5_nic_vport_disable_roce(ldev->pf[i].dev);
	}

	err = mlx5_deactivate_lag(ldev);
	if (err)
		return;

	if (shared_fdb || roce_lag)
		mlx5_lag_add_devices(ldev);

	if (shared_fdb) {
		if (!(dev0->priv.flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV))
			mlx5_eswitch_reload_reps(dev0->priv.eswitch);
		if (!(dev1->priv.flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV))
			mlx5_eswitch_reload_reps(dev1->priv.eswitch);
	}
}

bool mlx5_shared_fdb_supported(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;

	if (is_mdev_switchdev_mode(dev0) &&
	    is_mdev_switchdev_mode(dev1) &&
	    mlx5_eswitch_vport_match_metadata_enabled(dev0->priv.eswitch) &&
	    mlx5_eswitch_vport_match_metadata_enabled(dev1->priv.eswitch) &&
	    mlx5_devcom_is_paired(dev0->priv.devcom,
				  MLX5_DEVCOM_ESW_OFFLOADS) &&
	    MLX5_CAP_GEN(dev1, lag_native_fdb_selection) &&
	    MLX5_CAP_ESW(dev1, root_ft_on_other_esw) &&
	    MLX5_CAP_ESW(dev0, esw_shared_ingress_acl))
		return true;

	return false;
}

static bool mlx5_lag_is_roce_lag(struct mlx5_lag *ldev)
{
	bool roce_lag = true;
	int i;

	for (i = 0; i < ldev->ports; i++)
		roce_lag = roce_lag && !mlx5_sriov_is_enabled(ldev->pf[i].dev);

#ifdef CONFIG_MLX5_ESWITCH
	for (i = 0; i < ldev->ports; i++)
		roce_lag = roce_lag && is_mdev_legacy_mode(ldev->pf[i].dev);
#endif

	return roce_lag;
}

static bool mlx5_lag_should_modify_lag(struct mlx5_lag *ldev, bool do_bond)
{
	return do_bond && __mlx5_lag_is_active(ldev) &&
	       ldev->mode != MLX5_LAG_MODE_MPESW;
}

static bool mlx5_lag_should_disable_lag(struct mlx5_lag *ldev, bool do_bond)
{
	return !do_bond && __mlx5_lag_is_active(ldev) &&
	       ldev->mode != MLX5_LAG_MODE_MPESW;
}

static void mlx5_do_bond(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	struct lag_tracker tracker = { };
	bool do_bond, roce_lag;
	int err;
	int i;

	if (!mlx5_lag_is_ready(ldev)) {
		do_bond = false;
	} else {
		/* VF LAG is in multipath mode, ignore bond change requests */
		if (mlx5_lag_is_multipath(dev0))
			return;

		tracker = ldev->tracker;

		do_bond = tracker.is_bonded && mlx5_lag_check_prereq(ldev);
	}

	if (do_bond && !__mlx5_lag_is_active(ldev)) {
		bool shared_fdb = mlx5_shared_fdb_supported(ldev);

		roce_lag = mlx5_lag_is_roce_lag(ldev);

		if (shared_fdb || roce_lag)
			mlx5_lag_remove_devices(ldev);

		err = mlx5_activate_lag(ldev, &tracker,
					roce_lag ? MLX5_LAG_MODE_ROCE :
						   MLX5_LAG_MODE_SRIOV,
					shared_fdb);
		if (err) {
			if (shared_fdb || roce_lag)
				mlx5_lag_add_devices(ldev);

			return;
		} else if (roce_lag) {
			dev0->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
			mlx5_rescan_drivers_locked(dev0);
			for (i = 1; i < ldev->ports; i++)
				mlx5_nic_vport_enable_roce(ldev->pf[i].dev);
		} else if (shared_fdb) {
			dev0->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
			mlx5_rescan_drivers_locked(dev0);

			err = mlx5_eswitch_reload_reps(dev0->priv.eswitch);
			if (!err)
				err = mlx5_eswitch_reload_reps(dev1->priv.eswitch);

			if (err) {
				dev0->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
				mlx5_rescan_drivers_locked(dev0);
				mlx5_deactivate_lag(ldev);
				mlx5_lag_add_devices(ldev);
				mlx5_eswitch_reload_reps(dev0->priv.eswitch);
				mlx5_eswitch_reload_reps(dev1->priv.eswitch);
				mlx5_core_err(dev0, "Failed to enable lag\n");
				return;
			}
		}
	} else if (mlx5_lag_should_modify_lag(ldev, do_bond)) {
		mlx5_modify_lag(ldev, &tracker);
	} else if (mlx5_lag_should_disable_lag(ldev, do_bond)) {
		mlx5_disable_lag(ldev);
	}
}

static void mlx5_queue_bond_work(struct mlx5_lag *ldev, unsigned long delay)
{
	queue_delayed_work(ldev->wq, &ldev->bond_work, delay);
}

static void mlx5_do_bond_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mlx5_lag *ldev = container_of(delayed_work, struct mlx5_lag,
					     bond_work);
	int status;

	status = mlx5_dev_list_trylock();
	if (!status) {
		mlx5_queue_bond_work(ldev, HZ);
		return;
	}

	mutex_lock(&ldev->lock);
	if (ldev->mode_changes_in_progress) {
		mutex_unlock(&ldev->lock);
		mlx5_dev_list_unlock();
		mlx5_queue_bond_work(ldev, HZ);
		return;
	}

	mlx5_do_bond(ldev);
	mutex_unlock(&ldev->lock);
	mlx5_dev_list_unlock();
}

static int mlx5_handle_changeupper_event(struct mlx5_lag *ldev,
					 struct lag_tracker *tracker,
					 struct netdev_notifier_changeupper_info *info)
{
	struct net_device *upper = info->upper_dev, *ndev_tmp;
	struct netdev_lag_upper_info *lag_upper_info = NULL;
	bool is_bonded, is_in_lag, mode_supported;
	bool has_inactive = 0;
	struct slave *slave;
	u8 bond_status = 0;
	int num_slaves = 0;
	int changed = 0;
	int idx;

	if (!netif_is_lag_master(upper))
		return 0;

	if (info->linking)
		lag_upper_info = info->upper_info;

	/* The event may still be of interest if the slave does not belong to
	 * us, but is enslaved to a master which has one or more of our netdevs
	 * as slaves (e.g., if a new slave is added to a master that bonds two
	 * of our netdevs, we should unbond).
	 */
	rcu_read_lock();
	for_each_netdev_in_bond_rcu(upper, ndev_tmp) {
		idx = mlx5_lag_dev_get_netdev_idx(ldev, ndev_tmp);
		if (idx >= 0) {
			slave = bond_slave_get_rcu(ndev_tmp);
			if (slave)
				has_inactive |= bond_is_slave_inactive(slave);
			bond_status |= (1 << idx);
		}

		num_slaves++;
	}
	rcu_read_unlock();

	/* None of this lagdev's netdevs are slaves of this master. */
	if (!(bond_status & GENMASK(ldev->ports - 1, 0)))
		return 0;

	if (lag_upper_info) {
		tracker->tx_type = lag_upper_info->tx_type;
		tracker->hash_type = lag_upper_info->hash_type;
	}

	tracker->has_inactive = has_inactive;
	/* Determine bonding status:
	 * A device is considered bonded if both its physical ports are slaves
	 * of the same lag master, and only them.
	 */
	is_in_lag = num_slaves == ldev->ports &&
		bond_status == GENMASK(ldev->ports - 1, 0);

	/* Lag mode must be activebackup or hash. */
	mode_supported = tracker->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP ||
			 tracker->tx_type == NETDEV_LAG_TX_TYPE_HASH;

	is_bonded = is_in_lag && mode_supported;
	if (tracker->is_bonded != is_bonded) {
		tracker->is_bonded = is_bonded;
		changed = 1;
	}

	if (!is_in_lag)
		return changed;

	if (!mlx5_lag_is_ready(ldev))
		NL_SET_ERR_MSG_MOD(info->info.extack,
				   "Can't activate LAG offload, PF is configured with more than 64 VFs");
	else if (!mode_supported)
		NL_SET_ERR_MSG_MOD(info->info.extack,
				   "Can't activate LAG offload, TX type isn't supported");

	return changed;
}

static int mlx5_handle_changelowerstate_event(struct mlx5_lag *ldev,
					      struct lag_tracker *tracker,
					      struct net_device *ndev,
					      struct netdev_notifier_changelowerstate_info *info)
{
	struct netdev_lag_lower_state_info *lag_lower_info;
	int idx;

	if (!netif_is_lag_port(ndev))
		return 0;

	idx = mlx5_lag_dev_get_netdev_idx(ldev, ndev);
	if (idx < 0)
		return 0;

	/* This information is used to determine virtual to physical
	 * port mapping.
	 */
	lag_lower_info = info->lower_state_info;
	if (!lag_lower_info)
		return 0;

	tracker->netdev_state[idx] = *lag_lower_info;

	return 1;
}

static int mlx5_handle_changeinfodata_event(struct mlx5_lag *ldev,
					    struct lag_tracker *tracker,
					    struct net_device *ndev)
{
	struct net_device *ndev_tmp;
	struct slave *slave;
	bool has_inactive = 0;
	int idx;

	if (!netif_is_lag_master(ndev))
		return 0;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(ndev, ndev_tmp) {
		idx = mlx5_lag_dev_get_netdev_idx(ldev, ndev_tmp);
		if (idx < 0)
			continue;

		slave = bond_slave_get_rcu(ndev_tmp);
		if (slave)
			has_inactive |= bond_is_slave_inactive(slave);
	}
	rcu_read_unlock();

	if (tracker->has_inactive == has_inactive)
		return 0;

	tracker->has_inactive = has_inactive;

	return 1;
}

/* this handler is always registered to netdev events */
static int mlx5_lag_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct lag_tracker tracker;
	struct mlx5_lag *ldev;
	int changed = 0;

	if (event != NETDEV_CHANGEUPPER &&
	    event != NETDEV_CHANGELOWERSTATE &&
	    event != NETDEV_CHANGEINFODATA)
		return NOTIFY_DONE;

	ldev    = container_of(this, struct mlx5_lag, nb);

	tracker = ldev->tracker;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		changed = mlx5_handle_changeupper_event(ldev, &tracker, ptr);
		break;
	case NETDEV_CHANGELOWERSTATE:
		changed = mlx5_handle_changelowerstate_event(ldev, &tracker,
							     ndev, ptr);
		break;
	case NETDEV_CHANGEINFODATA:
		changed = mlx5_handle_changeinfodata_event(ldev, &tracker, ndev);
		break;
	}

	ldev->tracker = tracker;

	if (changed)
		mlx5_queue_bond_work(ldev, 0);

	return NOTIFY_DONE;
}

static void mlx5_ldev_add_netdev(struct mlx5_lag *ldev,
				 struct mlx5_core_dev *dev,
				 struct net_device *netdev)
{
	unsigned int fn = mlx5_get_dev_index(dev);
	unsigned long flags;

	if (fn >= ldev->ports)
		return;

	spin_lock_irqsave(&lag_lock, flags);
	ldev->pf[fn].netdev = netdev;
	ldev->tracker.netdev_state[fn].link_up = 0;
	ldev->tracker.netdev_state[fn].tx_enabled = 0;
	spin_unlock_irqrestore(&lag_lock, flags);
}

static void mlx5_ldev_remove_netdev(struct mlx5_lag *ldev,
				    struct net_device *netdev)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&lag_lock, flags);
	for (i = 0; i < ldev->ports; i++) {
		if (ldev->pf[i].netdev == netdev) {
			ldev->pf[i].netdev = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&lag_lock, flags);
}

static void mlx5_ldev_add_mdev(struct mlx5_lag *ldev,
			       struct mlx5_core_dev *dev)
{
	unsigned int fn = mlx5_get_dev_index(dev);

	if (fn >= ldev->ports)
		return;

	ldev->pf[fn].dev = dev;
	dev->priv.lag = ldev;
}

static void mlx5_ldev_remove_mdev(struct mlx5_lag *ldev,
				  struct mlx5_core_dev *dev)
{
	int i;

	for (i = 0; i < ldev->ports; i++)
		if (ldev->pf[i].dev == dev)
			break;

	if (i == ldev->ports)
		return;

	ldev->pf[i].dev = NULL;
	dev->priv.lag = NULL;
}

/* Must be called with intf_mutex held */
static int __mlx5_lag_dev_add_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = NULL;
	struct mlx5_core_dev *tmp_dev;

	tmp_dev = mlx5_get_next_phys_dev_lag(dev);
	if (tmp_dev)
		ldev = tmp_dev->priv.lag;

	if (!ldev) {
		ldev = mlx5_lag_dev_alloc(dev);
		if (!ldev) {
			mlx5_core_err(dev, "Failed to alloc lag dev\n");
			return 0;
		}
		mlx5_ldev_add_mdev(ldev, dev);
		return 0;
	}

	mutex_lock(&ldev->lock);
	if (ldev->mode_changes_in_progress) {
		mutex_unlock(&ldev->lock);
		return -EAGAIN;
	}
	mlx5_ldev_get(ldev);
	mlx5_ldev_add_mdev(ldev, dev);
	mutex_unlock(&ldev->lock);

	return 0;
}

void mlx5_lag_remove_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	/* mdev is being removed, might as well remove debugfs
	 * as early as possible.
	 */
	mlx5_ldev_remove_debugfs(dev->priv.dbg.lag_debugfs);
recheck:
	mutex_lock(&ldev->lock);
	if (ldev->mode_changes_in_progress) {
		mutex_unlock(&ldev->lock);
		msleep(100);
		goto recheck;
	}
	mlx5_ldev_remove_mdev(ldev, dev);
	mutex_unlock(&ldev->lock);
	mlx5_ldev_put(ldev);
}

void mlx5_lag_add_mdev(struct mlx5_core_dev *dev)
{
	int err;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    !MLX5_CAP_GEN(dev, lag_master) ||
	    (MLX5_CAP_GEN(dev, num_lag_ports) > MLX5_MAX_PORTS ||
	     MLX5_CAP_GEN(dev, num_lag_ports) <= 1))
		return;

recheck:
	mlx5_dev_list_lock();
	err = __mlx5_lag_dev_add_mdev(dev);
	mlx5_dev_list_unlock();

	if (err) {
		msleep(100);
		goto recheck;
	}
	mlx5_ldev_add_debugfs(dev);
}

void mlx5_lag_remove_netdev(struct mlx5_core_dev *dev,
			    struct net_device *netdev)
{
	struct mlx5_lag *ldev;
	bool lag_is_active;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	mutex_lock(&ldev->lock);
	mlx5_ldev_remove_netdev(ldev, netdev);
	clear_bit(MLX5_LAG_FLAG_NDEVS_READY, &ldev->state_flags);

	lag_is_active = __mlx5_lag_is_active(ldev);
	mutex_unlock(&ldev->lock);

	if (lag_is_active)
		mlx5_queue_bond_work(ldev, 0);
}

void mlx5_lag_add_netdev(struct mlx5_core_dev *dev,
			 struct net_device *netdev)
{
	struct mlx5_lag *ldev;
	int i;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	mutex_lock(&ldev->lock);
	mlx5_ldev_add_netdev(ldev, dev, netdev);

	for (i = 0; i < ldev->ports; i++)
		if (!ldev->pf[i].netdev)
			break;

	if (i >= ldev->ports)
		set_bit(MLX5_LAG_FLAG_NDEVS_READY, &ldev->state_flags);
	mutex_unlock(&ldev->lock);
	mlx5_queue_bond_work(ldev, 0);
}

bool mlx5_lag_is_roce(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	bool res;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	res  = ldev && __mlx5_lag_is_roce(ldev);
	spin_unlock_irqrestore(&lag_lock, flags);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_roce);

bool mlx5_lag_is_active(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	bool res;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	res  = ldev && __mlx5_lag_is_active(ldev);
	spin_unlock_irqrestore(&lag_lock, flags);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_active);

bool mlx5_lag_mode_is_hash(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	bool res = 0;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	if (ldev)
		res = test_bit(MLX5_LAG_MODE_FLAG_HASH_BASED, &ldev->mode_flags);
	spin_unlock_irqrestore(&lag_lock, flags);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_mode_is_hash);

bool mlx5_lag_is_master(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	bool res;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	res = ldev && __mlx5_lag_is_active(ldev) &&
		dev == ldev->pf[MLX5_LAG_P1].dev;
	spin_unlock_irqrestore(&lag_lock, flags);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_master);

bool mlx5_lag_is_sriov(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	bool res;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	res  = ldev && __mlx5_lag_is_sriov(ldev);
	spin_unlock_irqrestore(&lag_lock, flags);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_sriov);

bool mlx5_lag_is_shared_fdb(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	bool res;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	res = ldev && __mlx5_lag_is_sriov(ldev) &&
	      test_bit(MLX5_LAG_MODE_FLAG_SHARED_FDB, &ldev->mode_flags);
	spin_unlock_irqrestore(&lag_lock, flags);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_shared_fdb);

void mlx5_lag_disable_change(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	mlx5_dev_list_lock();
	mutex_lock(&ldev->lock);

	ldev->mode_changes_in_progress++;
	if (__mlx5_lag_is_active(ldev))
		mlx5_disable_lag(ldev);

	mutex_unlock(&ldev->lock);
	mlx5_dev_list_unlock();
}

void mlx5_lag_enable_change(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	mutex_lock(&ldev->lock);
	ldev->mode_changes_in_progress--;
	mutex_unlock(&ldev->lock);
	mlx5_queue_bond_work(ldev, 0);
}

struct net_device *mlx5_lag_get_roce_netdev(struct mlx5_core_dev *dev)
{
	struct net_device *ndev = NULL;
	struct mlx5_lag *ldev;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);

	if (!(ldev && __mlx5_lag_is_roce(ldev)))
		goto unlock;

	if (ldev->tracker.tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		for (i = 0; i < ldev->ports; i++)
			if (ldev->tracker.netdev_state[i].tx_enabled)
				ndev = ldev->pf[i].netdev;
		if (!ndev)
			ndev = ldev->pf[ldev->ports - 1].netdev;
	} else {
		ndev = ldev->pf[MLX5_LAG_P1].netdev;
	}
	if (ndev)
		dev_hold(ndev);

unlock:
	spin_unlock_irqrestore(&lag_lock, flags);

	return ndev;
}
EXPORT_SYMBOL(mlx5_lag_get_roce_netdev);

u8 mlx5_lag_get_slave_port(struct mlx5_core_dev *dev,
			   struct net_device *slave)
{
	struct mlx5_lag *ldev;
	unsigned long flags;
	u8 port = 0;
	int i;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	if (!(ldev && __mlx5_lag_is_roce(ldev)))
		goto unlock;

	for (i = 0; i < ldev->ports; i++) {
		if (ldev->pf[MLX5_LAG_P1].netdev == slave) {
			port = i;
			break;
		}
	}

	port = ldev->v2p_map[port * ldev->buckets];

unlock:
	spin_unlock_irqrestore(&lag_lock, flags);
	return port;
}
EXPORT_SYMBOL(mlx5_lag_get_slave_port);

u8 mlx5_lag_get_num_ports(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return 0;

	return ldev->ports;
}
EXPORT_SYMBOL(mlx5_lag_get_num_ports);

struct mlx5_core_dev *mlx5_lag_get_peer_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *peer_dev = NULL;
	struct mlx5_lag *ldev;
	unsigned long flags;

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		goto unlock;

	peer_dev = ldev->pf[MLX5_LAG_P1].dev == dev ?
			   ldev->pf[MLX5_LAG_P2].dev :
			   ldev->pf[MLX5_LAG_P1].dev;

unlock:
	spin_unlock_irqrestore(&lag_lock, flags);
	return peer_dev;
}
EXPORT_SYMBOL(mlx5_lag_get_peer_mdev);

int mlx5_lag_query_cong_counters(struct mlx5_core_dev *dev,
				 u64 *values,
				 int num_counters,
				 size_t *offsets)
{
	int outlen = MLX5_ST_SZ_BYTES(query_cong_statistics_out);
	struct mlx5_core_dev **mdev;
	struct mlx5_lag *ldev;
	unsigned long flags;
	int num_ports;
	int ret, i, j;
	void *out;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	mdev = kvzalloc(sizeof(mdev[0]) * MLX5_MAX_PORTS, GFP_KERNEL);
	if (!mdev) {
		ret = -ENOMEM;
		goto free_out;
	}

	memset(values, 0, sizeof(*values) * num_counters);

	spin_lock_irqsave(&lag_lock, flags);
	ldev = mlx5_lag_dev(dev);
	if (ldev && __mlx5_lag_is_active(ldev)) {
		num_ports = ldev->ports;
		for (i = 0; i < ldev->ports; i++)
			mdev[i] = ldev->pf[i].dev;
	} else {
		num_ports = 1;
		mdev[MLX5_LAG_P1] = dev;
	}
	spin_unlock_irqrestore(&lag_lock, flags);

	for (i = 0; i < num_ports; ++i) {
		u32 in[MLX5_ST_SZ_DW(query_cong_statistics_in)] = {};

		MLX5_SET(query_cong_statistics_in, in, opcode,
			 MLX5_CMD_OP_QUERY_CONG_STATISTICS);
		ret = mlx5_cmd_exec_inout(mdev[i], query_cong_statistics, in,
					  out);
		if (ret)
			goto free_mdev;

		for (j = 0; j < num_counters; ++j)
			values[j] += be64_to_cpup((__be64 *)(out + offsets[j]));
	}

free_mdev:
	kvfree(mdev);
free_out:
	kvfree(out);
	return ret;
}
EXPORT_SYMBOL(mlx5_lag_query_cong_counters);
