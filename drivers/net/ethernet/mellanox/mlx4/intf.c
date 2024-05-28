/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <net/devlink.h>

#include "mlx4.h"

static DEFINE_MUTEX(intf_mutex);
static DEFINE_IDA(mlx4_adev_ida);

static bool is_eth_supported(struct mlx4_dev *dev)
{
	for (int port = 1; port <= dev->caps.num_ports; port++)
		if (dev->caps.port_type[port] == MLX4_PORT_TYPE_ETH)
			return true;

	return false;
}

static bool is_ib_supported(struct mlx4_dev *dev)
{
	for (int port = 1; port <= dev->caps.num_ports; port++)
		if (dev->caps.port_type[port] == MLX4_PORT_TYPE_IB)
			return true;

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_IBOE)
		return true;

	return false;
}

static const struct mlx4_adev_device {
	const char *suffix;
	bool (*is_supported)(struct mlx4_dev *dev);
} mlx4_adev_devices[] = {
	{ "eth", is_eth_supported },
	{ "ib", is_ib_supported },
};

int mlx4_adev_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->adev_idx = ida_alloc(&mlx4_adev_ida, GFP_KERNEL);
	if (priv->adev_idx < 0)
		return priv->adev_idx;

	priv->adev = kcalloc(ARRAY_SIZE(mlx4_adev_devices),
			     sizeof(struct mlx4_adev *), GFP_KERNEL);
	if (!priv->adev) {
		ida_free(&mlx4_adev_ida, priv->adev_idx);
		return -ENOMEM;
	}

	return 0;
}

void mlx4_adev_cleanup(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	kfree(priv->adev);
	ida_free(&mlx4_adev_ida, priv->adev_idx);
}

static void adev_release(struct device *dev)
{
	struct mlx4_adev *mlx4_adev =
		container_of(dev, struct mlx4_adev, adev.dev);
	struct mlx4_priv *priv = mlx4_priv(mlx4_adev->mdev);
	int idx = mlx4_adev->idx;

	kfree(mlx4_adev);
	priv->adev[idx] = NULL;
}

static struct mlx4_adev *add_adev(struct mlx4_dev *dev, int idx)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	const char *suffix = mlx4_adev_devices[idx].suffix;
	struct auxiliary_device *adev;
	struct mlx4_adev *madev;
	int ret;

	madev = kzalloc(sizeof(*madev), GFP_KERNEL);
	if (!madev)
		return ERR_PTR(-ENOMEM);

	adev = &madev->adev;
	adev->id = priv->adev_idx;
	adev->name = suffix;
	adev->dev.parent = &dev->persist->pdev->dev;
	adev->dev.release = adev_release;
	madev->mdev = dev;
	madev->idx = idx;

	ret = auxiliary_device_init(adev);
	if (ret) {
		kfree(madev);
		return ERR_PTR(ret);
	}

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ERR_PTR(ret);
	}
	return madev;
}

static void del_adev(struct auxiliary_device *adev)
{
	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

int mlx4_register_auxiliary_driver(struct mlx4_adrv *madrv)
{
	return auxiliary_driver_register(&madrv->adrv);
}
EXPORT_SYMBOL_GPL(mlx4_register_auxiliary_driver);

void mlx4_unregister_auxiliary_driver(struct mlx4_adrv *madrv)
{
	auxiliary_driver_unregister(&madrv->adrv);
}
EXPORT_SYMBOL_GPL(mlx4_unregister_auxiliary_driver);

int mlx4_do_bond(struct mlx4_dev *dev, bool enable)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i, ret;

	if (!(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_PORT_REMAP))
		return -EOPNOTSUPP;

	ret = mlx4_disable_rx_port_check(dev, enable);
	if (ret) {
		mlx4_err(dev, "Fail to %s rx port check\n",
			 enable ? "enable" : "disable");
		return ret;
	}
	if (enable) {
		dev->flags |= MLX4_FLAG_BONDED;
	} else {
		ret = mlx4_virt2phy_port_map(dev, 1, 2);
		if (ret) {
			mlx4_err(dev, "Fail to reset port map\n");
			return ret;
		}
		dev->flags &= ~MLX4_FLAG_BONDED;
	}

	mutex_lock(&intf_mutex);

	for (i = 0; i < ARRAY_SIZE(mlx4_adev_devices); i++) {
		struct mlx4_adev *madev = priv->adev[i];
		struct mlx4_adrv *madrv;
		enum mlx4_protocol protocol;

		if (!madev)
			continue;

		device_lock(&madev->adev.dev);
		if (!madev->adev.dev.driver) {
			device_unlock(&madev->adev.dev);
			continue;
		}

		madrv = container_of(madev->adev.dev.driver, struct mlx4_adrv,
				     adrv.driver);
		if (!(madrv->flags & MLX4_INTFF_BONDING)) {
			device_unlock(&madev->adev.dev);
			continue;
		}

		if (mlx4_is_mfunc(dev)) {
			mlx4_dbg(dev,
				 "SRIOV, disabled HA mode for intf proto %d\n",
				 madrv->protocol);
			device_unlock(&madev->adev.dev);
			continue;
		}

		protocol = madrv->protocol;
		device_unlock(&madev->adev.dev);

		del_adev(&madev->adev);
		priv->adev[i] = add_adev(dev, i);
		if (IS_ERR(priv->adev[i])) {
			mlx4_warn(dev, "Device[%d] (%s) failed to load\n", i,
				  mlx4_adev_devices[i].suffix);
			priv->adev[i] = NULL;
			continue;
		}

		mlx4_dbg(dev,
			 "Interface for protocol %d restarted with bonded mode %s\n",
			 protocol, enable ? "enabled" : "disabled");
	}

	mutex_unlock(&intf_mutex);

	return 0;
}

void mlx4_dispatch_event(struct mlx4_dev *dev, enum mlx4_dev_event type,
			 void *param)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	atomic_notifier_call_chain(&priv->event_nh, type, param);
}

int mlx4_register_event_notifier(struct mlx4_dev *dev,
				 struct notifier_block *nb)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return atomic_notifier_chain_register(&priv->event_nh, nb);
}
EXPORT_SYMBOL(mlx4_register_event_notifier);

int mlx4_unregister_event_notifier(struct mlx4_dev *dev,
				   struct notifier_block *nb)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return atomic_notifier_chain_unregister(&priv->event_nh, nb);
}
EXPORT_SYMBOL(mlx4_unregister_event_notifier);

static int add_drivers(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(mlx4_adev_devices); i++) {
		bool is_supported = false;

		if (priv->adev[i])
			continue;

		if (mlx4_adev_devices[i].is_supported)
			is_supported = mlx4_adev_devices[i].is_supported(dev);

		if (!is_supported)
			continue;

		priv->adev[i] = add_adev(dev, i);
		if (IS_ERR(priv->adev[i])) {
			mlx4_warn(dev, "Device[%d] (%s) failed to load\n", i,
				  mlx4_adev_devices[i].suffix);
			/* We continue to rescan drivers and leave to the caller
			 * to make decision if to release everything or
			 * continue. */
			ret = PTR_ERR(priv->adev[i]);
			priv->adev[i] = NULL;
		}
	}
	return ret;
}

static void delete_drivers(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	bool delete_all;
	int i;

	delete_all = !(dev->persist->interface_state & MLX4_INTERFACE_STATE_UP);

	for (i = ARRAY_SIZE(mlx4_adev_devices) - 1; i >= 0; i--) {
		bool is_supported = false;

		if (!priv->adev[i])
			continue;

		if (mlx4_adev_devices[i].is_supported && !delete_all)
			is_supported = mlx4_adev_devices[i].is_supported(dev);

		if (is_supported)
			continue;

		del_adev(&priv->adev[i]->adev);
		priv->adev[i] = NULL;
	}
}

/* This function is used after mlx4_dev is reconfigured.
 */
static int rescan_drivers_locked(struct mlx4_dev *dev)
{
	lockdep_assert_held(&intf_mutex);

	delete_drivers(dev);
	if (!(dev->persist->interface_state & MLX4_INTERFACE_STATE_UP))
		return 0;

	return add_drivers(dev);
}

int mlx4_register_device(struct mlx4_dev *dev)
{
	int ret;

	mutex_lock(&intf_mutex);

	dev->persist->interface_state |= MLX4_INTERFACE_STATE_UP;

	ret = rescan_drivers_locked(dev);

	mutex_unlock(&intf_mutex);

	if (ret) {
		mlx4_unregister_device(dev);
		return ret;
	}

	mlx4_start_catas_poll(dev);

	return ret;
}

void mlx4_unregister_device(struct mlx4_dev *dev)
{
	if (!(dev->persist->interface_state & MLX4_INTERFACE_STATE_UP))
		return;

	mlx4_stop_catas_poll(dev);
	if (dev->persist->interface_state & MLX4_INTERFACE_STATE_DELETION &&
	    mlx4_is_slave(dev)) {
		/* In mlx4_remove_one on a VF */
		u32 slave_read =
			swab32(readl(&mlx4_priv(dev)->mfunc.comm->slave_read));

		if (mlx4_comm_internal_err(slave_read)) {
			mlx4_dbg(dev, "%s: comm channel is down, entering error state.\n",
				 __func__);
			mlx4_enter_error_state(dev->persist);
		}
	}
	mutex_lock(&intf_mutex);

	dev->persist->interface_state &= ~MLX4_INTERFACE_STATE_UP;

	rescan_drivers_locked(dev);

	mutex_unlock(&intf_mutex);
}

struct devlink_port *mlx4_get_devlink_port(struct mlx4_dev *dev, int port)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];

	return &info->devlink_port;
}
EXPORT_SYMBOL_GPL(mlx4_get_devlink_port);
