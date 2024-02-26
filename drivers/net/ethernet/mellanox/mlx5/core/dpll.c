// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/dpll.h>
#include <linux/mlx5/driver.h>

/* This structure represents a reference to DPLL, one is created
 * per mdev instance.
 */
struct mlx5_dpll {
	struct dpll_device *dpll;
	struct dpll_pin *dpll_pin;
	struct mlx5_core_dev *mdev;
	struct workqueue_struct *wq;
	struct delayed_work work;
	struct {
		bool valid;
		enum dpll_lock_status lock_status;
		enum dpll_pin_state pin_state;
	} last;
	struct notifier_block mdev_nb;
	struct net_device *tracking_netdev;
};

static int mlx5_dpll_clock_id_get(struct mlx5_core_dev *mdev, u64 *clock_id)
{
	u32 out[MLX5_ST_SZ_DW(msecq_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(msecq_reg)] = {};
	int err;

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_MSECQ, 0, 0);
	if (err)
		return err;
	*clock_id = MLX5_GET64(msecq_reg, out, local_clock_identity);
	return 0;
}

struct mlx5_dpll_synce_status {
	enum mlx5_msees_admin_status admin_status;
	enum mlx5_msees_oper_status oper_status;
	bool ho_acq;
	bool oper_freq_measure;
	s32 frequency_diff;
};

static int
mlx5_dpll_synce_status_get(struct mlx5_core_dev *mdev,
			   struct mlx5_dpll_synce_status *synce_status)
{
	u32 out[MLX5_ST_SZ_DW(msees_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(msees_reg)] = {};
	int err;

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_MSEES, 0, 0);
	if (err)
		return err;
	synce_status->admin_status = MLX5_GET(msees_reg, out, admin_status);
	synce_status->oper_status = MLX5_GET(msees_reg, out, oper_status);
	synce_status->ho_acq = MLX5_GET(msees_reg, out, ho_acq);
	synce_status->oper_freq_measure = MLX5_GET(msees_reg, out, oper_freq_measure);
	synce_status->frequency_diff = MLX5_GET(msees_reg, out, frequency_diff);
	return 0;
}

static int
mlx5_dpll_synce_status_set(struct mlx5_core_dev *mdev,
			   enum mlx5_msees_admin_status admin_status)
{
	u32 out[MLX5_ST_SZ_DW(msees_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(msees_reg)] = {};

	MLX5_SET(msees_reg, in, field_select,
		 MLX5_MSEES_FIELD_SELECT_ENABLE |
		 MLX5_MSEES_FIELD_SELECT_ADMIN_FREQ_MEASURE |
		 MLX5_MSEES_FIELD_SELECT_ADMIN_STATUS);
	MLX5_SET(msees_reg, in, admin_status, admin_status);
	MLX5_SET(msees_reg, in, admin_freq_measure, true);
	return mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out),
				    MLX5_REG_MSEES, 0, 1);
}

static enum dpll_lock_status
mlx5_dpll_lock_status_get(struct mlx5_dpll_synce_status *synce_status)
{
	switch (synce_status->oper_status) {
	case MLX5_MSEES_OPER_STATUS_SELF_TRACK:
		fallthrough;
	case MLX5_MSEES_OPER_STATUS_OTHER_TRACK:
		return synce_status->ho_acq ? DPLL_LOCK_STATUS_LOCKED_HO_ACQ :
					      DPLL_LOCK_STATUS_LOCKED;
	case MLX5_MSEES_OPER_STATUS_HOLDOVER:
		fallthrough;
	case MLX5_MSEES_OPER_STATUS_FAIL_HOLDOVER:
		return DPLL_LOCK_STATUS_HOLDOVER;
	default:
		return DPLL_LOCK_STATUS_UNLOCKED;
	}
}

static enum dpll_pin_state
mlx5_dpll_pin_state_get(struct mlx5_dpll_synce_status *synce_status)
{
	return (synce_status->admin_status == MLX5_MSEES_ADMIN_STATUS_TRACK &&
		(synce_status->oper_status == MLX5_MSEES_OPER_STATUS_SELF_TRACK ||
		 synce_status->oper_status == MLX5_MSEES_OPER_STATUS_OTHER_TRACK)) ?
	       DPLL_PIN_STATE_CONNECTED : DPLL_PIN_STATE_DISCONNECTED;
}

static int
mlx5_dpll_pin_ffo_get(struct mlx5_dpll_synce_status *synce_status,
		      s64 *ffo)
{
	if (!synce_status->oper_freq_measure)
		return -ENODATA;
	*ffo = synce_status->frequency_diff;
	return 0;
}

static int mlx5_dpll_device_lock_status_get(const struct dpll_device *dpll,
					    void *priv,
					    enum dpll_lock_status *status,
					    struct netlink_ext_ack *extack)
{
	struct mlx5_dpll_synce_status synce_status;
	struct mlx5_dpll *mdpll = priv;
	int err;

	err = mlx5_dpll_synce_status_get(mdpll->mdev, &synce_status);
	if (err)
		return err;
	*status = mlx5_dpll_lock_status_get(&synce_status);
	return 0;
}

static int mlx5_dpll_device_mode_get(const struct dpll_device *dpll,
				     void *priv, enum dpll_mode *mode,
				     struct netlink_ext_ack *extack)
{
	*mode = DPLL_MODE_MANUAL;
	return 0;
}

static const struct dpll_device_ops mlx5_dpll_device_ops = {
	.lock_status_get = mlx5_dpll_device_lock_status_get,
	.mode_get = mlx5_dpll_device_mode_get,
};

static int mlx5_dpll_pin_direction_get(const struct dpll_pin *pin,
				       void *pin_priv,
				       const struct dpll_device *dpll,
				       void *dpll_priv,
				       enum dpll_pin_direction *direction,
				       struct netlink_ext_ack *extack)
{
	*direction = DPLL_PIN_DIRECTION_INPUT;
	return 0;
}

static int mlx5_dpll_state_on_dpll_get(const struct dpll_pin *pin,
				       void *pin_priv,
				       const struct dpll_device *dpll,
				       void *dpll_priv,
				       enum dpll_pin_state *state,
				       struct netlink_ext_ack *extack)
{
	struct mlx5_dpll_synce_status synce_status;
	struct mlx5_dpll *mdpll = pin_priv;
	int err;

	err = mlx5_dpll_synce_status_get(mdpll->mdev, &synce_status);
	if (err)
		return err;
	*state = mlx5_dpll_pin_state_get(&synce_status);
	return 0;
}

static int mlx5_dpll_state_on_dpll_set(const struct dpll_pin *pin,
				       void *pin_priv,
				       const struct dpll_device *dpll,
				       void *dpll_priv,
				       enum dpll_pin_state state,
				       struct netlink_ext_ack *extack)
{
	struct mlx5_dpll *mdpll = pin_priv;

	return mlx5_dpll_synce_status_set(mdpll->mdev,
					  state == DPLL_PIN_STATE_CONNECTED ?
					  MLX5_MSEES_ADMIN_STATUS_TRACK :
					  MLX5_MSEES_ADMIN_STATUS_FREE_RUNNING);
}

static int mlx5_dpll_ffo_get(const struct dpll_pin *pin, void *pin_priv,
			     const struct dpll_device *dpll, void *dpll_priv,
			     s64 *ffo, struct netlink_ext_ack *extack)
{
	struct mlx5_dpll_synce_status synce_status;
	struct mlx5_dpll *mdpll = pin_priv;
	int err;

	err = mlx5_dpll_synce_status_get(mdpll->mdev, &synce_status);
	if (err)
		return err;
	return mlx5_dpll_pin_ffo_get(&synce_status, ffo);
}

static const struct dpll_pin_ops mlx5_dpll_pins_ops = {
	.direction_get = mlx5_dpll_pin_direction_get,
	.state_on_dpll_get = mlx5_dpll_state_on_dpll_get,
	.state_on_dpll_set = mlx5_dpll_state_on_dpll_set,
	.ffo_get = mlx5_dpll_ffo_get,
};

static const struct dpll_pin_properties mlx5_dpll_pin_properties = {
	.type = DPLL_PIN_TYPE_SYNCE_ETH_PORT,
	.capabilities = DPLL_PIN_CAPABILITIES_STATE_CAN_CHANGE,
};

#define MLX5_DPLL_PERIODIC_WORK_INTERVAL 500 /* ms */

static void mlx5_dpll_periodic_work_queue(struct mlx5_dpll *mdpll)
{
	queue_delayed_work(mdpll->wq, &mdpll->work,
			   msecs_to_jiffies(MLX5_DPLL_PERIODIC_WORK_INTERVAL));
}

static void mlx5_dpll_periodic_work(struct work_struct *work)
{
	struct mlx5_dpll *mdpll = container_of(work, struct mlx5_dpll,
					       work.work);
	struct mlx5_dpll_synce_status synce_status;
	enum dpll_lock_status lock_status;
	enum dpll_pin_state pin_state;
	int err;

	err = mlx5_dpll_synce_status_get(mdpll->mdev, &synce_status);
	if (err)
		goto err_out;
	lock_status = mlx5_dpll_lock_status_get(&synce_status);
	pin_state = mlx5_dpll_pin_state_get(&synce_status);

	if (!mdpll->last.valid)
		goto invalid_out;

	if (mdpll->last.lock_status != lock_status)
		dpll_device_change_ntf(mdpll->dpll);
	if (mdpll->last.pin_state != pin_state)
		dpll_pin_change_ntf(mdpll->dpll_pin);

invalid_out:
	mdpll->last.lock_status = lock_status;
	mdpll->last.pin_state = pin_state;
	mdpll->last.valid = true;
err_out:
	mlx5_dpll_periodic_work_queue(mdpll);
}

static void mlx5_dpll_netdev_dpll_pin_set(struct mlx5_dpll *mdpll,
					  struct net_device *netdev)
{
	if (mdpll->tracking_netdev)
		return;
	netdev_dpll_pin_set(netdev, mdpll->dpll_pin);
	mdpll->tracking_netdev = netdev;
}

static void mlx5_dpll_netdev_dpll_pin_clear(struct mlx5_dpll *mdpll)
{
	if (!mdpll->tracking_netdev)
		return;
	netdev_dpll_pin_clear(mdpll->tracking_netdev);
	mdpll->tracking_netdev = NULL;
}

static int mlx5_dpll_mdev_notifier_event(struct notifier_block *nb,
					 unsigned long event, void *data)
{
	struct mlx5_dpll *mdpll = container_of(nb, struct mlx5_dpll, mdev_nb);
	struct net_device *netdev = data;

	switch (event) {
	case MLX5_DRIVER_EVENT_UPLINK_NETDEV:
		if (netdev)
			mlx5_dpll_netdev_dpll_pin_set(mdpll, netdev);
		else
			mlx5_dpll_netdev_dpll_pin_clear(mdpll);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static void mlx5_dpll_mdev_netdev_track(struct mlx5_dpll *mdpll,
					struct mlx5_core_dev *mdev)
{
	mdpll->mdev_nb.notifier_call = mlx5_dpll_mdev_notifier_event;
	mlx5_blocking_notifier_register(mdev, &mdpll->mdev_nb);
	mlx5_core_uplink_netdev_event_replay(mdev);
}

static void mlx5_dpll_mdev_netdev_untrack(struct mlx5_dpll *mdpll,
					  struct mlx5_core_dev *mdev)
{
	mlx5_blocking_notifier_unregister(mdev, &mdpll->mdev_nb);
	mlx5_dpll_netdev_dpll_pin_clear(mdpll);
}

static int mlx5_dpll_probe(struct auxiliary_device *adev,
			   const struct auxiliary_device_id *id)
{
	struct mlx5_adev *edev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = edev->mdev;
	struct mlx5_dpll *mdpll;
	u64 clock_id;
	int err;

	err = mlx5_dpll_synce_status_set(mdev,
					 MLX5_MSEES_ADMIN_STATUS_FREE_RUNNING);
	if (err)
		return err;

	err = mlx5_dpll_clock_id_get(mdev, &clock_id);
	if (err)
		return err;

	mdpll = kzalloc(sizeof(*mdpll), GFP_KERNEL);
	if (!mdpll)
		return -ENOMEM;
	mdpll->mdev = mdev;
	auxiliary_set_drvdata(adev, mdpll);

	/* Multiple mdev instances might share one DPLL device. */
	mdpll->dpll = dpll_device_get(clock_id, 0, THIS_MODULE);
	if (IS_ERR(mdpll->dpll)) {
		err = PTR_ERR(mdpll->dpll);
		goto err_free_mdpll;
	}

	err = dpll_device_register(mdpll->dpll, DPLL_TYPE_EEC,
				   &mlx5_dpll_device_ops, mdpll);
	if (err)
		goto err_put_dpll_device;

	/* Multiple mdev instances might share one DPLL pin. */
	mdpll->dpll_pin = dpll_pin_get(clock_id, mlx5_get_dev_index(mdev),
				       THIS_MODULE, &mlx5_dpll_pin_properties);
	if (IS_ERR(mdpll->dpll_pin)) {
		err = PTR_ERR(mdpll->dpll_pin);
		goto err_unregister_dpll_device;
	}

	err = dpll_pin_register(mdpll->dpll, mdpll->dpll_pin,
				&mlx5_dpll_pins_ops, mdpll);
	if (err)
		goto err_put_dpll_pin;

	mdpll->wq = create_singlethread_workqueue("mlx5_dpll");
	if (!mdpll->wq) {
		err = -ENOMEM;
		goto err_unregister_dpll_pin;
	}

	mlx5_dpll_mdev_netdev_track(mdpll, mdev);

	INIT_DELAYED_WORK(&mdpll->work, &mlx5_dpll_periodic_work);
	mlx5_dpll_periodic_work_queue(mdpll);

	return 0;

err_unregister_dpll_pin:
	dpll_pin_unregister(mdpll->dpll, mdpll->dpll_pin,
			    &mlx5_dpll_pins_ops, mdpll);
err_put_dpll_pin:
	dpll_pin_put(mdpll->dpll_pin);
err_unregister_dpll_device:
	dpll_device_unregister(mdpll->dpll, &mlx5_dpll_device_ops, mdpll);
err_put_dpll_device:
	dpll_device_put(mdpll->dpll);
err_free_mdpll:
	kfree(mdpll);
	return err;
}

static void mlx5_dpll_remove(struct auxiliary_device *adev)
{
	struct mlx5_dpll *mdpll = auxiliary_get_drvdata(adev);
	struct mlx5_core_dev *mdev = mdpll->mdev;

	cancel_delayed_work_sync(&mdpll->work);
	mlx5_dpll_mdev_netdev_untrack(mdpll, mdev);
	destroy_workqueue(mdpll->wq);
	dpll_pin_unregister(mdpll->dpll, mdpll->dpll_pin,
			    &mlx5_dpll_pins_ops, mdpll);
	dpll_pin_put(mdpll->dpll_pin);
	dpll_device_unregister(mdpll->dpll, &mlx5_dpll_device_ops, mdpll);
	dpll_device_put(mdpll->dpll);
	kfree(mdpll);

	mlx5_dpll_synce_status_set(mdev,
				   MLX5_MSEES_ADMIN_STATUS_FREE_RUNNING);
}

static int mlx5_dpll_suspend(struct auxiliary_device *adev, pm_message_t state)
{
	return 0;
}

static int mlx5_dpll_resume(struct auxiliary_device *adev)
{
	return 0;
}

static const struct auxiliary_device_id mlx5_dpll_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".dpll", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx5_dpll_id_table);

static struct auxiliary_driver mlx5_dpll_driver = {
	.name = "dpll",
	.probe = mlx5_dpll_probe,
	.remove = mlx5_dpll_remove,
	.suspend = mlx5_dpll_suspend,
	.resume = mlx5_dpll_resume,
	.id_table = mlx5_dpll_id_table,
};

static int __init mlx5_dpll_init(void)
{
	return auxiliary_driver_register(&mlx5_dpll_driver);
}

static void __exit mlx5_dpll_exit(void)
{
	auxiliary_driver_unregister(&mlx5_dpll_driver);
}

module_init(mlx5_dpll_init);
module_exit(mlx5_dpll_exit);

MODULE_AUTHOR("Jiri Pirko <jiri@nvidia.com>");
MODULE_DESCRIPTION("Mellanox 5th generation network adapters (ConnectX series) DPLL driver");
MODULE_LICENSE("Dual BSD/GPL");
