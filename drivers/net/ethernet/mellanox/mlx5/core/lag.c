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
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/vport.h>
#include "lib/devcom.h"
#include "mlx5_core.h"
#include "eswitch.h"
#include "lag.h"
#include "lag_mp.h"

/* General purpose, use for short periods of time.
 * Beware of lock dependencies (preferably, no locks should be acquired
 * under it).
 */
static DEFINE_SPINLOCK(lag_lock);

static int mlx5_cmd_create_lag(struct mlx5_core_dev *dev, u8 remap_port1,
			       u8 remap_port2, bool shared_fdb)
{
	u32 in[MLX5_ST_SZ_DW(create_lag_in)] = {};
	void *lag_ctx = MLX5_ADDR_OF(create_lag_in, in, ctx);

	MLX5_SET(create_lag_in, in, opcode, MLX5_CMD_OP_CREATE_LAG);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, remap_port1);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, remap_port2);
	MLX5_SET(lagc, lag_ctx, fdb_selection_mode, shared_fdb);

	return mlx5_cmd_exec_in(dev, create_lag, in);
}

static int mlx5_cmd_modify_lag(struct mlx5_core_dev *dev, u8 remap_port1,
			       u8 remap_port2)
{
	u32 in[MLX5_ST_SZ_DW(modify_lag_in)] = {};
	void *lag_ctx = MLX5_ADDR_OF(modify_lag_in, in, ctx);

	MLX5_SET(modify_lag_in, in, opcode, MLX5_CMD_OP_MODIFY_LAG);
	MLX5_SET(modify_lag_in, in, field_select, 0x1);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, remap_port1);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, remap_port2);

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

static int mlx5_lag_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr);
static void mlx5_do_bond_work(struct work_struct *work);

static void mlx5_ldev_free(struct kref *ref)
{
	struct mlx5_lag *ldev = container_of(ref, struct mlx5_lag, ref);

	if (ldev->nb.notifier_call)
		unregister_netdevice_notifier_net(&init_net, &ldev->nb);
	mlx5_lag_mp_cleanup(ldev);
	cancel_delayed_work_sync(&ldev->bond_work);
	destroy_workqueue(ldev->wq);
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
	INIT_DELAYED_WORK(&ldev->bond_work, mlx5_do_bond_work);

	ldev->nb.notifier_call = mlx5_lag_netdev_event;
	if (register_netdevice_notifier_net(&init_net, &ldev->nb)) {
		ldev->nb.notifier_call = NULL;
		mlx5_core_err(dev, "Failed to register LAG netdev notifier\n");
	}

	err = mlx5_lag_mp_init(ldev);
	if (err)
		mlx5_core_err(dev, "Failed to init multipath lag err=%d\n",
			      err);

	return ldev;
}

int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				struct net_device *ndev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].netdev == ndev)
			return i;

	return -ENOENT;
}

static bool __mlx5_lag_is_roce(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_FLAG_ROCE);
}

static bool __mlx5_lag_is_sriov(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_FLAG_SRIOV);
}

static void mlx5_infer_tx_affinity_mapping(struct lag_tracker *tracker,
					   u8 *port1, u8 *port2)
{
	bool p1en;
	bool p2en;

	p1en = tracker->netdev_state[MLX5_LAG_P1].tx_enabled &&
	       tracker->netdev_state[MLX5_LAG_P1].link_up;

	p2en = tracker->netdev_state[MLX5_LAG_P2].tx_enabled &&
	       tracker->netdev_state[MLX5_LAG_P2].link_up;

	*port1 = 1;
	*port2 = 2;
	if ((!p1en && !p2en) || (p1en && p2en))
		return;

	if (p1en)
		*port2 = 1;
	else
		*port1 = 2;
}

void mlx5_modify_lag(struct mlx5_lag *ldev,
		     struct lag_tracker *tracker)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	u8 v2p_port1, v2p_port2;
	int err;

	mlx5_infer_tx_affinity_mapping(tracker, &v2p_port1,
				       &v2p_port2);

	if (v2p_port1 != ldev->v2p_map[MLX5_LAG_P1] ||
	    v2p_port2 != ldev->v2p_map[MLX5_LAG_P2]) {
		ldev->v2p_map[MLX5_LAG_P1] = v2p_port1;
		ldev->v2p_map[MLX5_LAG_P2] = v2p_port2;

		mlx5_core_info(dev0, "modify lag map port 1:%d port 2:%d",
			       ldev->v2p_map[MLX5_LAG_P1],
			       ldev->v2p_map[MLX5_LAG_P2]);

		err = mlx5_cmd_modify_lag(dev0, v2p_port1, v2p_port2);
		if (err)
			mlx5_core_err(dev0,
				      "Failed to modify LAG (%d)\n",
				      err);
	}
}

static int mlx5_create_lag(struct mlx5_lag *ldev,
			   struct lag_tracker *tracker,
			   bool shared_fdb)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	u32 in[MLX5_ST_SZ_DW(destroy_lag_in)] = {};
	int err;

	mlx5_infer_tx_affinity_mapping(tracker, &ldev->v2p_map[MLX5_LAG_P1],
				       &ldev->v2p_map[MLX5_LAG_P2]);

	mlx5_core_info(dev0, "lag map port 1:%d port 2:%d shared_fdb:%d",
		       ldev->v2p_map[MLX5_LAG_P1], ldev->v2p_map[MLX5_LAG_P2],
		       shared_fdb);

	err = mlx5_cmd_create_lag(dev0, ldev->v2p_map[MLX5_LAG_P1],
				  ldev->v2p_map[MLX5_LAG_P2], shared_fdb);
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
		      u8 flags,
		      bool shared_fdb)
{
	bool roce_lag = !!(flags & MLX5_LAG_FLAG_ROCE);
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	int err;

	err = mlx5_create_lag(ldev, tracker, shared_fdb);
	if (err) {
		if (roce_lag) {
			mlx5_core_err(dev0,
				      "Failed to activate RoCE LAG\n");
		} else {
			mlx5_core_err(dev0,
				      "Failed to activate VF LAG\n"
				      "Make sure all VFs are unbound prior to VF LAG activation or deactivation\n");
		}
		return err;
	}

	ldev->flags |= flags;
	ldev->shared_fdb = shared_fdb;
	return 0;
}

static int mlx5_deactivate_lag(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	u32 in[MLX5_ST_SZ_DW(destroy_lag_in)] = {};
	bool roce_lag = __mlx5_lag_is_roce(ldev);
	int err;

	ldev->flags &= ~MLX5_LAG_MODE_FLAGS;
	mlx5_lag_mp_reset(ldev);

	if (ldev->shared_fdb) {
		mlx5_eswitch_offloads_destroy_single_fdb(ldev->pf[MLX5_LAG_P1].dev->priv.eswitch,
							 ldev->pf[MLX5_LAG_P2].dev->priv.eswitch);
		ldev->shared_fdb = false;
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
	}

	return err;
}

static bool mlx5_lag_check_prereq(struct mlx5_lag *ldev)
{
	if (!ldev->pf[MLX5_LAG_P1].dev || !ldev->pf[MLX5_LAG_P2].dev)
		return false;

#ifdef CONFIG_MLX5_ESWITCH
	return mlx5_esw_lag_prereq(ldev->pf[MLX5_LAG_P1].dev,
				   ldev->pf[MLX5_LAG_P2].dev);
#else
	return (!mlx5_sriov_is_enabled(ldev->pf[MLX5_LAG_P1].dev) &&
		!mlx5_sriov_is_enabled(ldev->pf[MLX5_LAG_P2].dev));
#endif
}

static void mlx5_lag_add_devices(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++) {
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

	for (i = 0; i < MLX5_MAX_PORTS; i++) {
		if (!ldev->pf[i].dev)
			continue;

		if (ldev->pf[i].dev->priv.flags &
		    MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)
			continue;

		ldev->pf[i].dev->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
		mlx5_rescan_drivers_locked(ldev->pf[i].dev);
	}
}

static void mlx5_disable_lag(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	bool shared_fdb = ldev->shared_fdb;
	bool roce_lag;
	int err;

	roce_lag = __mlx5_lag_is_roce(ldev);

	if (shared_fdb) {
		mlx5_lag_remove_devices(ldev);
	} else if (roce_lag) {
		if (!(dev0->priv.flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)) {
			dev0->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
			mlx5_rescan_drivers_locked(dev0);
		}
		mlx5_nic_vport_disable_roce(dev1);
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

static bool mlx5_shared_fdb_supported(struct mlx5_lag *ldev)
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

static void mlx5_do_bond(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	struct lag_tracker tracker;
	bool do_bond, roce_lag;
	int err;

	if (!mlx5_lag_is_ready(ldev)) {
		do_bond = false;
	} else {
		tracker = ldev->tracker;

		do_bond = tracker.is_bonded && mlx5_lag_check_prereq(ldev);
	}

	if (do_bond && !__mlx5_lag_is_active(ldev)) {
		bool shared_fdb = mlx5_shared_fdb_supported(ldev);

		roce_lag = !mlx5_sriov_is_enabled(dev0) &&
			   !mlx5_sriov_is_enabled(dev1);

#ifdef CONFIG_MLX5_ESWITCH
		roce_lag = roce_lag &&
			   dev0->priv.eswitch->mode == MLX5_ESWITCH_NONE &&
			   dev1->priv.eswitch->mode == MLX5_ESWITCH_NONE;
#endif

		if (shared_fdb || roce_lag)
			mlx5_lag_remove_devices(ldev);

		err = mlx5_activate_lag(ldev, &tracker,
					roce_lag ? MLX5_LAG_FLAG_ROCE :
						   MLX5_LAG_FLAG_SRIOV,
					shared_fdb);
		if (err) {
			if (shared_fdb || roce_lag)
				mlx5_lag_add_devices(ldev);

			return;
		} else if (roce_lag) {
			dev0->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_IB_ADEV;
			mlx5_rescan_drivers_locked(dev0);
			mlx5_nic_vport_enable_roce(dev1);
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
	} else if (do_bond && __mlx5_lag_is_active(ldev)) {
		mlx5_modify_lag(ldev, &tracker);
	} else if (!do_bond && __mlx5_lag_is_active(ldev)) {
		mlx5_disable_lag(ldev);
	}
}

static void mlx5_queue_bond_work(struct mlx5_lag *ldev, unsigned long delay)
{
	queue_delayed_work(ldev->wq, &ldev->bond_work, delay);
}

static void mlx5_lag_lock_eswitches(struct mlx5_core_dev *dev0,
				    struct mlx5_core_dev *dev1)
{
	if (dev0)
		mlx5_esw_lock(dev0->priv.eswitch);
	if (dev1)
		mlx5_esw_lock(dev1->priv.eswitch);
}

static void mlx5_lag_unlock_eswitches(struct mlx5_core_dev *dev0,
				      struct mlx5_core_dev *dev1)
{
	if (dev1)
		mlx5_esw_unlock(dev1->priv.eswitch);
	if (dev0)
		mlx5_esw_unlock(dev0->priv.eswitch);
}

static void mlx5_do_bond_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mlx5_lag *ldev = container_of(delayed_work, struct mlx5_lag,
					     bond_work);
	struct mlx5_core_dev *dev0 = ldev->pf[MLX5_LAG_P1].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[MLX5_LAG_P2].dev;
	int status;

	status = mlx5_dev_list_trylock();
	if (!status) {
		mlx5_queue_bond_work(ldev, HZ);
		return;
	}

	if (ldev->mode_changes_in_progress) {
		mlx5_dev_list_unlock();
		mlx5_queue_bond_work(ldev, HZ);
		return;
	}

	mlx5_lag_lock_eswitches(dev0, dev1);
	mlx5_do_bond(ldev);
	mlx5_lag_unlock_eswitches(dev0, dev1);
	mlx5_dev_list_unlock();
}

static int mlx5_handle_changeupper_event(struct mlx5_lag *ldev,
					 struct lag_tracker *tracker,
					 struct net_device *ndev,
					 struct netdev_notifier_changeupper_info *info)
{
	struct net_device *upper = info->upper_dev, *ndev_tmp;
	struct netdev_lag_upper_info *lag_upper_info = NULL;
	bool is_bonded, is_in_lag, mode_supported;
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
		if (idx >= 0)
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
	 */
	is_in_lag = num_slaves == MLX5_MAX_PORTS && bond_status == 0x3;

	if (!mlx5_lag_is_ready(ldev) && is_in_lag) {
		NL_SET_ERR_MSG_MOD(info->info.extack,
				   "Can't activate LAG offload, PF is configured with more than 64 VFs");
		return 0;
	}

	/* Lag mode must be activebackup or hash. */
	mode_supported = tracker->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP ||
			 tracker->tx_type == NETDEV_LAG_TX_TYPE_HASH;

	if (is_in_lag && !mode_supported)
		NL_SET_ERR_MSG_MOD(info->info.extack,
				   "Can't activate LAG offload, TX type isn't supported");

	is_bonded = is_in_lag && mode_supported;
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

static int mlx5_lag_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct lag_tracker tracker;
	struct mlx5_lag *ldev;
	int changed = 0;

	if ((event != NETDEV_CHANGEUPPER) && (event != NETDEV_CHANGELOWERSTATE))
		return NOTIFY_DONE;

	ldev    = container_of(this, struct mlx5_lag, nb);

	if (!mlx5_lag_is_ready(ldev) && event == NETDEV_CHANGELOWERSTATE)
		return NOTIFY_DONE;

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

	ldev->tracker = tracker;

	if (changed)
		mlx5_queue_bond_work(ldev, 0);

	return NOTIFY_DONE;
}

static void mlx5_ldev_add_netdev(struct mlx5_lag *ldev,
				 struct mlx5_core_dev *dev,
				 struct net_device *netdev)
{
	unsigned int fn = PCI_FUNC(dev->pdev->devfn);

	if (fn >= MLX5_MAX_PORTS)
		return;

	spin_lock(&lag_lock);
	ldev->pf[fn].netdev = netdev;
	ldev->tracker.netdev_state[fn].link_up = 0;
	ldev->tracker.netdev_state[fn].tx_enabled = 0;
	spin_unlock(&lag_lock);
}

static void mlx5_ldev_remove_netdev(struct mlx5_lag *ldev,
				    struct net_device *netdev)
{
	int i;

	spin_lock(&lag_lock);
	for (i = 0; i < MLX5_MAX_PORTS; i++) {
		if (ldev->pf[i].netdev == netdev) {
			ldev->pf[i].netdev = NULL;
			break;
		}
	}
	spin_unlock(&lag_lock);
}

static void mlx5_ldev_add_mdev(struct mlx5_lag *ldev,
			       struct mlx5_core_dev *dev)
{
	unsigned int fn = PCI_FUNC(dev->pdev->devfn);

	if (fn >= MLX5_MAX_PORTS)
		return;

	ldev->pf[fn].dev = dev;
	dev->priv.lag = ldev;
}

/* Must be called with intf_mutex held */
static void mlx5_ldev_remove_mdev(struct mlx5_lag *ldev,
				  struct mlx5_core_dev *dev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev == dev)
			break;

	if (i == MLX5_MAX_PORTS)
		return;

	ldev->pf[i].dev = NULL;
	dev->priv.lag = NULL;
}

/* Must be called with intf_mutex held */
static int __mlx5_lag_dev_add_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = NULL;
	struct mlx5_core_dev *tmp_dev;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    !MLX5_CAP_GEN(dev, lag_master) ||
	    MLX5_CAP_GEN(dev, num_lag_ports) != MLX5_MAX_PORTS)
		return 0;

	tmp_dev = mlx5_get_next_phys_dev(dev);
	if (tmp_dev)
		ldev = tmp_dev->priv.lag;

	if (!ldev) {
		ldev = mlx5_lag_dev_alloc(dev);
		if (!ldev) {
			mlx5_core_err(dev, "Failed to alloc lag dev\n");
			return 0;
		}
	} else {
		if (ldev->mode_changes_in_progress)
			return -EAGAIN;
		mlx5_ldev_get(ldev);
	}

	mlx5_ldev_add_mdev(ldev, dev);

	return 0;
}

void mlx5_lag_remove_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

recheck:
	mlx5_dev_list_lock();
	if (ldev->mode_changes_in_progress) {
		mlx5_dev_list_unlock();
		msleep(100);
		goto recheck;
	}
	mlx5_ldev_remove_mdev(ldev, dev);
	mlx5_dev_list_unlock();
	mlx5_ldev_put(ldev);
}

void mlx5_lag_add_mdev(struct mlx5_core_dev *dev)
{
	int err;

recheck:
	mlx5_dev_list_lock();
	err = __mlx5_lag_dev_add_mdev(dev);
	if (err) {
		mlx5_dev_list_unlock();
		msleep(100);
		goto recheck;
	}
	mlx5_dev_list_unlock();
}

/* Must be called with intf_mutex held */
void mlx5_lag_remove_netdev(struct mlx5_core_dev *dev,
			    struct net_device *netdev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	mlx5_ldev_remove_netdev(ldev, netdev);
	ldev->flags &= ~MLX5_LAG_FLAG_READY;

	if (__mlx5_lag_is_active(ldev))
		mlx5_queue_bond_work(ldev, 0);
}

/* Must be called with intf_mutex held */
void mlx5_lag_add_netdev(struct mlx5_core_dev *dev,
			 struct net_device *netdev)
{
	struct mlx5_lag *ldev;
	int i;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return;

	mlx5_ldev_add_netdev(ldev, dev, netdev);

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (!ldev->pf[i].dev)
			break;

	if (i >= MLX5_MAX_PORTS)
		ldev->flags |= MLX5_LAG_FLAG_READY;
	mlx5_queue_bond_work(ldev, 0);
}

bool mlx5_lag_is_roce(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	res  = ldev && __mlx5_lag_is_roce(ldev);
	spin_unlock(&lag_lock);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_roce);

bool mlx5_lag_is_active(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	res  = ldev && __mlx5_lag_is_active(ldev);
	spin_unlock(&lag_lock);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_active);

bool mlx5_lag_is_master(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	res = ldev && __mlx5_lag_is_active(ldev) &&
		dev == ldev->pf[MLX5_LAG_P1].dev;
	spin_unlock(&lag_lock);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_master);

bool mlx5_lag_is_sriov(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	res  = ldev && __mlx5_lag_is_sriov(ldev);
	spin_unlock(&lag_lock);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_sriov);

bool mlx5_lag_is_shared_fdb(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	res = ldev && __mlx5_lag_is_sriov(ldev) && ldev->shared_fdb;
	spin_unlock(&lag_lock);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_shared_fdb);

void mlx5_lag_disable_change(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *dev0;
	struct mlx5_core_dev *dev1;
	struct mlx5_lag *ldev;

	mlx5_dev_list_lock();

	ldev = mlx5_lag_dev(dev);
	dev0 = ldev->pf[MLX5_LAG_P1].dev;
	dev1 = ldev->pf[MLX5_LAG_P2].dev;

	ldev->mode_changes_in_progress++;
	if (__mlx5_lag_is_active(ldev)) {
		mlx5_lag_lock_eswitches(dev0, dev1);
		mlx5_disable_lag(ldev);
		mlx5_lag_unlock_eswitches(dev0, dev1);
	}
	mlx5_dev_list_unlock();
}

void mlx5_lag_enable_change(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	mlx5_dev_list_lock();
	ldev = mlx5_lag_dev(dev);
	ldev->mode_changes_in_progress--;
	mlx5_dev_list_unlock();
	mlx5_queue_bond_work(ldev, 0);
}

struct net_device *mlx5_lag_get_roce_netdev(struct mlx5_core_dev *dev)
{
	struct net_device *ndev = NULL;
	struct mlx5_lag *ldev;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);

	if (!(ldev && __mlx5_lag_is_roce(ldev)))
		goto unlock;

	if (ldev->tracker.tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		ndev = ldev->tracker.netdev_state[MLX5_LAG_P1].tx_enabled ?
		       ldev->pf[MLX5_LAG_P1].netdev :
		       ldev->pf[MLX5_LAG_P2].netdev;
	} else {
		ndev = ldev->pf[MLX5_LAG_P1].netdev;
	}
	if (ndev)
		dev_hold(ndev);

unlock:
	spin_unlock(&lag_lock);

	return ndev;
}
EXPORT_SYMBOL(mlx5_lag_get_roce_netdev);

u8 mlx5_lag_get_slave_port(struct mlx5_core_dev *dev,
			   struct net_device *slave)
{
	struct mlx5_lag *ldev;
	u8 port = 0;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	if (!(ldev && __mlx5_lag_is_roce(ldev)))
		goto unlock;

	if (ldev->pf[MLX5_LAG_P1].netdev == slave)
		port = MLX5_LAG_P1;
	else
		port = MLX5_LAG_P2;

	port = ldev->v2p_map[port];

unlock:
	spin_unlock(&lag_lock);
	return port;
}
EXPORT_SYMBOL(mlx5_lag_get_slave_port);

struct mlx5_core_dev *mlx5_lag_get_peer_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *peer_dev = NULL;
	struct mlx5_lag *ldev;

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		goto unlock;

	peer_dev = ldev->pf[MLX5_LAG_P1].dev == dev ?
			   ldev->pf[MLX5_LAG_P2].dev :
			   ldev->pf[MLX5_LAG_P1].dev;

unlock:
	spin_unlock(&lag_lock);
	return peer_dev;
}
EXPORT_SYMBOL(mlx5_lag_get_peer_mdev);

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

	spin_lock(&lag_lock);
	ldev = mlx5_lag_dev(dev);
	if (ldev && __mlx5_lag_is_active(ldev)) {
		num_ports = MLX5_MAX_PORTS;
		mdev[MLX5_LAG_P1] = ldev->pf[MLX5_LAG_P1].dev;
		mdev[MLX5_LAG_P2] = ldev->pf[MLX5_LAG_P2].dev;
	} else {
		num_ports = 1;
		mdev[MLX5_LAG_P1] = dev;
	}
	spin_unlock(&lag_lock);

	for (i = 0; i < num_ports; ++i) {
		u32 in[MLX5_ST_SZ_DW(query_cong_statistics_in)] = {};

		MLX5_SET(query_cong_statistics_in, in, opcode,
			 MLX5_CMD_OP_QUERY_CONG_STATISTICS);
		ret = mlx5_cmd_exec_inout(mdev[i], query_cong_statistics, in,
					  out);
		if (ret)
			goto free;

		for (j = 0; j < num_counters; ++j)
			values[j] += be64_to_cpup((__be64 *)(out + offsets[j]));
	}

free:
	kvfree(out);
	return ret;
}
EXPORT_SYMBOL(mlx5_lag_query_cong_counters);
