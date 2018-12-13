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
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include "mlx5_core.h"

enum {
	MLX5_LAG_FLAG_BONDED = 1 << 0,
};

struct lag_func {
	struct mlx5_core_dev *dev;
	struct net_device    *netdev;
};

/* Used for collection of netdev event info. */
struct lag_tracker {
	enum   netdev_lag_tx_type           tx_type;
	struct netdev_lag_lower_state_info  netdev_state[MLX5_MAX_PORTS];
	bool is_bonded;
};

/* LAG data of a ConnectX card.
 * It serves both its phys functions.
 */
struct mlx5_lag {
	u8                        flags;
	u8                        v2p_map[MLX5_MAX_PORTS];
	struct lag_func           pf[MLX5_MAX_PORTS];
	struct lag_tracker        tracker;
	struct delayed_work       bond_work;
	struct notifier_block     nb;

	/* Admin state. Allow lag only if allowed is true
	 * even if network conditions for lag were met
	 */
	bool                      allowed;
};

/* General purpose, use for short periods of time.
 * Beware of lock dependencies (preferably, no locks should be acquired
 * under it).
 */
static DEFINE_MUTEX(lag_mutex);

static int mlx5_cmd_create_lag(struct mlx5_core_dev *dev, u8 remap_port1,
			       u8 remap_port2)
{
	u32   in[MLX5_ST_SZ_DW(create_lag_in)]   = {0};
	u32   out[MLX5_ST_SZ_DW(create_lag_out)] = {0};
	void *lag_ctx = MLX5_ADDR_OF(create_lag_in, in, ctx);

	MLX5_SET(create_lag_in, in, opcode, MLX5_CMD_OP_CREATE_LAG);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, remap_port1);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, remap_port2);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_modify_lag(struct mlx5_core_dev *dev, u8 remap_port1,
			       u8 remap_port2)
{
	u32   in[MLX5_ST_SZ_DW(modify_lag_in)]   = {0};
	u32   out[MLX5_ST_SZ_DW(modify_lag_out)] = {0};
	void *lag_ctx = MLX5_ADDR_OF(modify_lag_in, in, ctx);

	MLX5_SET(modify_lag_in, in, opcode, MLX5_CMD_OP_MODIFY_LAG);
	MLX5_SET(modify_lag_in, in, field_select, 0x1);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, remap_port1);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, remap_port2);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_destroy_lag(struct mlx5_core_dev *dev)
{
	u32  in[MLX5_ST_SZ_DW(destroy_lag_in)]  = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_lag_out)] = {0};

	MLX5_SET(destroy_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_LAG);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_create_vport_lag(struct mlx5_core_dev *dev)
{
	u32  in[MLX5_ST_SZ_DW(create_vport_lag_in)]  = {0};
	u32 out[MLX5_ST_SZ_DW(create_vport_lag_out)] = {0};

	MLX5_SET(create_vport_lag_in, in, opcode, MLX5_CMD_OP_CREATE_VPORT_LAG);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_cmd_create_vport_lag);

int mlx5_cmd_destroy_vport_lag(struct mlx5_core_dev *dev)
{
	u32  in[MLX5_ST_SZ_DW(destroy_vport_lag_in)]  = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_vport_lag_out)] = {0};

	MLX5_SET(destroy_vport_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_VPORT_LAG);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_cmd_destroy_vport_lag);

static int mlx5_cmd_query_cong_counter(struct mlx5_core_dev *dev,
				       bool reset, void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_statistics_in)] = { };

	MLX5_SET(query_cong_statistics_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_STATISTICS);
	MLX5_SET(query_cong_statistics_in, in, clear, reset);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}

static struct mlx5_lag *mlx5_lag_dev_get(struct mlx5_core_dev *dev)
{
	return dev->priv.lag;
}

static int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				       struct net_device *ndev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].netdev == ndev)
			return i;

	return -1;
}

static bool mlx5_lag_is_bonded(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_FLAG_BONDED);
}

static void mlx5_infer_tx_affinity_mapping(struct lag_tracker *tracker,
					   u8 *port1, u8 *port2)
{
	*port1 = 1;
	*port2 = 2;
	if (!tracker->netdev_state[0].tx_enabled ||
	    !tracker->netdev_state[0].link_up) {
		*port1 = 2;
		return;
	}

	if (!tracker->netdev_state[1].tx_enabled ||
	    !tracker->netdev_state[1].link_up)
		*port2 = 1;
}

static int mlx5_create_lag(struct mlx5_lag *ldev,
			   struct lag_tracker *tracker)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	int err;

	mlx5_infer_tx_affinity_mapping(tracker, &ldev->v2p_map[0],
				       &ldev->v2p_map[1]);

	err = mlx5_cmd_create_lag(dev0, ldev->v2p_map[0], ldev->v2p_map[1]);
	if (err)
		mlx5_core_err(dev0,
			      "Failed to create LAG (%d)\n",
			      err);
	return err;
}

static void mlx5_activate_lag(struct mlx5_lag *ldev,
			      struct lag_tracker *tracker)
{
	ldev->flags |= MLX5_LAG_FLAG_BONDED;
	mlx5_create_lag(ldev, tracker);
}

static void mlx5_deactivate_lag(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	int err;

	ldev->flags &= ~MLX5_LAG_FLAG_BONDED;

	err = mlx5_cmd_destroy_lag(dev0);
	if (err)
		mlx5_core_err(dev0,
			      "Failed to destroy LAG (%d)\n",
			      err);
}

static void mlx5_do_bond(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[1].dev;
	struct lag_tracker tracker;
	u8 v2p_port1, v2p_port2;
	int i, err;
	bool do_bond;

	if (!dev0 || !dev1)
		return;

	mutex_lock(&lag_mutex);
	tracker = ldev->tracker;
	mutex_unlock(&lag_mutex);

	do_bond = tracker.is_bonded && ldev->allowed;

	if (do_bond && !mlx5_lag_is_bonded(ldev)) {
		for (i = 0; i < MLX5_MAX_PORTS; i++)
			mlx5_remove_dev_by_protocol(ldev->pf[i].dev,
						    MLX5_INTERFACE_PROTOCOL_IB);

		mlx5_activate_lag(ldev, &tracker);

		mlx5_add_dev_by_protocol(dev0, MLX5_INTERFACE_PROTOCOL_IB);
		mlx5_nic_vport_enable_roce(dev1);
	} else if (do_bond && mlx5_lag_is_bonded(ldev)) {
		mlx5_infer_tx_affinity_mapping(&tracker, &v2p_port1,
					       &v2p_port2);

		if ((v2p_port1 != ldev->v2p_map[0]) ||
		    (v2p_port2 != ldev->v2p_map[1])) {
			ldev->v2p_map[0] = v2p_port1;
			ldev->v2p_map[1] = v2p_port2;

			err = mlx5_cmd_modify_lag(dev0, v2p_port1, v2p_port2);
			if (err)
				mlx5_core_err(dev0,
					      "Failed to modify LAG (%d)\n",
					      err);
		}
	} else if (!do_bond && mlx5_lag_is_bonded(ldev)) {
		mlx5_remove_dev_by_protocol(dev0, MLX5_INTERFACE_PROTOCOL_IB);
		mlx5_nic_vport_disable_roce(dev1);

		mlx5_deactivate_lag(ldev);

		for (i = 0; i < MLX5_MAX_PORTS; i++)
			if (ldev->pf[i].dev)
				mlx5_add_dev_by_protocol(ldev->pf[i].dev,
							 MLX5_INTERFACE_PROTOCOL_IB);
	}
}

static void mlx5_queue_bond_work(struct mlx5_lag *ldev, unsigned long delay)
{
	schedule_delayed_work(&ldev->bond_work, delay);
}

static void mlx5_do_bond_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mlx5_lag *ldev = container_of(delayed_work, struct mlx5_lag,
					     bond_work);
	int status;

	status = mlx5_dev_list_trylock();
	if (!status) {
		/* 1 sec delay. */
		mlx5_queue_bond_work(ldev, HZ);
		return;
	}

	mlx5_do_bond(ldev);
	mlx5_dev_list_unlock();
}

static int mlx5_handle_changeupper_event(struct mlx5_lag *ldev,
					 struct lag_tracker *tracker,
					 struct net_device *ndev,
					 struct netdev_notifier_changeupper_info *info)
{
	struct net_device *upper = info->upper_dev, *ndev_tmp;
	struct netdev_lag_upper_info *lag_upper_info = NULL;
	bool is_bonded;
	int bond_status = 0;
	int num_slaves = 0;
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
		if (idx > -1)
			bond_status |= (1 << idx);

		num_slaves++;
	}
	rcu_read_unlock();

	/* None of this lagdev's netdevs are slaves of this master. */
	if (!(bond_status & 0x3))
		return 0;

	if (lag_upper_info)
		tracker->tx_type = lag_upper_info->tx_type;

	/* Determine bonding status:
	 * A device is considered bonded if both its physical ports are slaves
	 * of the same lag master, and only them.
	 * Lag mode must be activebackup or hash.
	 */
	is_bonded = (num_slaves == MLX5_MAX_PORTS) &&
		    (bond_status == 0x3) &&
		    ((tracker->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) ||
		     (tracker->tx_type == NETDEV_LAG_TX_TYPE_HASH));

	if (tracker->is_bonded != is_bonded) {
		tracker->is_bonded = is_bonded;
		return 1;
	}

	return 0;
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
	if (idx == -1)
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

static int mlx5_lag_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct lag_tracker tracker;
	struct mlx5_lag *ldev;
	int changed = 0;

	if (!net_eq(dev_net(ndev), &init_net))
		return NOTIFY_DONE;

	if ((event != NETDEV_CHANGEUPPER) && (event != NETDEV_CHANGELOWERSTATE))
		return NOTIFY_DONE;

	ldev    = container_of(this, struct mlx5_lag, nb);
	tracker = ldev->tracker;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		changed = mlx5_handle_changeupper_event(ldev, &tracker, ndev,
							ptr);
		break;
	case NETDEV_CHANGELOWERSTATE:
		changed = mlx5_handle_changelowerstate_event(ldev, &tracker,
							     ndev, ptr);
		break;
	}

	mutex_lock(&lag_mutex);
	ldev->tracker = tracker;
	mutex_unlock(&lag_mutex);

	if (changed)
		mlx5_queue_bond_work(ldev, 0);

	return NOTIFY_DONE;
}

static bool mlx5_lag_check_prereq(struct mlx5_lag *ldev)
{
	if ((ldev->pf[0].dev && mlx5_sriov_is_enabled(ldev->pf[0].dev)) ||
	    (ldev->pf[1].dev && mlx5_sriov_is_enabled(ldev->pf[1].dev)))
		return false;
	else
		return true;
}

static struct mlx5_lag *mlx5_lag_dev_alloc(void)
{
	struct mlx5_lag *ldev;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return NULL;

	INIT_DELAYED_WORK(&ldev->bond_work, mlx5_do_bond_work);
	ldev->allowed = mlx5_lag_check_prereq(ldev);

	return ldev;
}

static void mlx5_lag_dev_free(struct mlx5_lag *ldev)
{
	kfree(ldev);
}

static void mlx5_lag_dev_add_pf(struct mlx5_lag *ldev,
				struct mlx5_core_dev *dev,
				struct net_device *netdev)
{
	unsigned int fn = PCI_FUNC(dev->pdev->devfn);

	if (fn >= MLX5_MAX_PORTS)
		return;

	mutex_lock(&lag_mutex);
	ldev->pf[fn].dev    = dev;
	ldev->pf[fn].netdev = netdev;
	ldev->tracker.netdev_state[fn].link_up = 0;
	ldev->tracker.netdev_state[fn].tx_enabled = 0;

	ldev->allowed = mlx5_lag_check_prereq(ldev);
	dev->priv.lag = ldev;

	mutex_unlock(&lag_mutex);
}

static void mlx5_lag_dev_remove_pf(struct mlx5_lag *ldev,
				   struct mlx5_core_dev *dev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev == dev)
			break;

	if (i == MLX5_MAX_PORTS)
		return;

	mutex_lock(&lag_mutex);
	memset(&ldev->pf[i], 0, sizeof(*ldev->pf));

	dev->priv.lag = NULL;
	ldev->allowed = mlx5_lag_check_prereq(ldev);
	mutex_unlock(&lag_mutex);
}

/* Must be called with intf_mutex held */
void mlx5_lag_add(struct mlx5_core_dev *dev, struct net_device *netdev)
{
	struct mlx5_lag *ldev = NULL;
	struct mlx5_core_dev *tmp_dev;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    !MLX5_CAP_GEN(dev, lag_master) ||
	    (MLX5_CAP_GEN(dev, num_lag_ports) != MLX5_MAX_PORTS))
		return;

	tmp_dev = mlx5_get_next_phys_dev(dev);
	if (tmp_dev)
		ldev = tmp_dev->priv.lag;

	if (!ldev) {
		ldev = mlx5_lag_dev_alloc();
		if (!ldev) {
			mlx5_core_err(dev, "Failed to alloc lag dev\n");
			return;
		}
	}

	mlx5_lag_dev_add_pf(ldev, dev, netdev);

	if (!ldev->nb.notifier_call) {
		ldev->nb.notifier_call = mlx5_lag_netdev_event;
		if (register_netdevice_notifier(&ldev->nb)) {
			ldev->nb.notifier_call = NULL;
			mlx5_core_err(dev, "Failed to register LAG netdev notifier\n");
		}
	}
}

/* Must be called with intf_mutex held */
void mlx5_lag_remove(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	int i;

	ldev = mlx5_lag_dev_get(dev);
	if (!ldev)
		return;

	if (mlx5_lag_is_bonded(ldev))
		mlx5_deactivate_lag(ldev);

	mlx5_lag_dev_remove_pf(ldev, dev);

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev)
			break;

	if (i == MLX5_MAX_PORTS) {
		if (ldev->nb.notifier_call)
			unregister_netdevice_notifier(&ldev->nb);
		cancel_delayed_work_sync(&ldev->bond_work);
		mlx5_lag_dev_free(ldev);
	}
}

bool mlx5_lag_is_active(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && mlx5_lag_is_bonded(ldev);
	mutex_unlock(&lag_mutex);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_active);

static int mlx5_lag_set_state(struct mlx5_core_dev *dev, bool allow)
{
	struct mlx5_lag *ldev;
	int ret = 0;
	bool lag_active;

	mlx5_dev_list_lock();

	ldev = mlx5_lag_dev_get(dev);
	if (!ldev) {
		ret = -ENODEV;
		goto unlock;
	}
	lag_active = mlx5_lag_is_bonded(ldev);
	if (!mlx5_lag_check_prereq(ldev) && allow) {
		ret = -EINVAL;
		goto unlock;
	}
	if (ldev->allowed == allow)
		goto unlock;
	ldev->allowed = allow;
	if ((lag_active && !allow) || allow)
		mlx5_do_bond(ldev);
unlock:
	mlx5_dev_list_unlock();
	return ret;
}

int mlx5_lag_forbid(struct mlx5_core_dev *dev)
{
	return mlx5_lag_set_state(dev, false);
}

int mlx5_lag_allow(struct mlx5_core_dev *dev)
{
	return mlx5_lag_set_state(dev, true);
}

struct net_device *mlx5_lag_get_roce_netdev(struct mlx5_core_dev *dev)
{
	struct net_device *ndev = NULL;
	struct mlx5_lag *ldev;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);

	if (!(ldev && mlx5_lag_is_bonded(ldev)))
		goto unlock;

	if (ldev->tracker.tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		ndev = ldev->tracker.netdev_state[0].tx_enabled ?
		       ldev->pf[0].netdev : ldev->pf[1].netdev;
	} else {
		ndev = ldev->pf[0].netdev;
	}
	if (ndev)
		dev_hold(ndev);

unlock:
	mutex_unlock(&lag_mutex);

	return ndev;
}
EXPORT_SYMBOL(mlx5_lag_get_roce_netdev);

bool mlx5_lag_intf_add(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev,
						 priv);
	struct mlx5_lag *ldev;

	if (intf->protocol != MLX5_INTERFACE_PROTOCOL_IB)
		return true;

	ldev = mlx5_lag_dev_get(dev);
	if (!ldev || !mlx5_lag_is_bonded(ldev) || ldev->pf[0].dev == dev)
		return true;

	/* If bonded, we do not add an IB device for PF1. */
	return false;
}

int mlx5_lag_query_cong_counters(struct mlx5_core_dev *dev,
				 u64 *values,
				 int num_counters,
				 size_t *offsets)
{
	int outlen = MLX5_ST_SZ_BYTES(query_cong_statistics_out);
	struct mlx5_core_dev *mdev[MLX5_MAX_PORTS];
	struct mlx5_lag *ldev;
	int num_ports;
	int ret, i, j;
	void *out;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	memset(values, 0, sizeof(*values) * num_counters);

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	if (ldev && mlx5_lag_is_bonded(ldev)) {
		num_ports = MLX5_MAX_PORTS;
		mdev[0] = ldev->pf[0].dev;
		mdev[1] = ldev->pf[1].dev;
	} else {
		num_ports = 1;
		mdev[0] = dev;
	}

	for (i = 0; i < num_ports; ++i) {
		ret = mlx5_cmd_query_cong_counter(mdev[i], false, out, outlen);
		if (ret)
			goto unlock;

		for (j = 0; j < num_counters; ++j)
			values[j] += be64_to_cpup((__be64 *)(out + offsets[j]));
	}

unlock:
	mutex_unlock(&lag_mutex);
	kvfree(out);
	return ret;
}
EXPORT_SYMBOL(mlx5_lag_query_cong_counters);
