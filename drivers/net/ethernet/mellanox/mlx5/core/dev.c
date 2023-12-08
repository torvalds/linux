/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/driver.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/mlx5_ifc_vdpa.h>
#include <linux/mlx5/vport.h>
#include "mlx5_core.h"

/* intf dev list mutex */
static DEFINE_MUTEX(mlx5_intf_mutex);
static DEFINE_IDA(mlx5_adev_ida);

static bool is_eth_rep_supported(struct mlx5_core_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MLX5_ESWITCH))
		return false;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return false;

	if (!is_mdev_switchdev_mode(dev))
		return false;

	return true;
}

bool mlx5_eth_supported(struct mlx5_core_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MLX5_CORE_EN))
		return false;

	if (MLX5_CAP_GEN(dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return false;

	if (!MLX5_CAP_GEN(dev, eth_net_offloads)) {
		mlx5_core_warn(dev, "Missing eth_net_offloads capability\n");
		return false;
	}

	if (!MLX5_CAP_GEN(dev, nic_flow_table)) {
		mlx5_core_warn(dev, "Missing nic_flow_table capability\n");
		return false;
	}

	if (!MLX5_CAP_ETH(dev, csum_cap)) {
		mlx5_core_warn(dev, "Missing csum_cap capability\n");
		return false;
	}

	if (!MLX5_CAP_ETH(dev, max_lso_cap)) {
		mlx5_core_warn(dev, "Missing max_lso_cap capability\n");
		return false;
	}

	if (!MLX5_CAP_ETH(dev, vlan_cap)) {
		mlx5_core_warn(dev, "Missing vlan_cap capability\n");
		return false;
	}

	if (!MLX5_CAP_ETH(dev, rss_ind_tbl_cap)) {
		mlx5_core_warn(dev, "Missing rss_ind_tbl_cap capability\n");
		return false;
	}

	if (MLX5_CAP_FLOWTABLE(dev,
			       flow_table_properties_nic_receive.max_ft_level) < 3) {
		mlx5_core_warn(dev, "max_ft_level < 3\n");
		return false;
	}

	if (!MLX5_CAP_ETH(dev, self_lb_en_modifiable))
		mlx5_core_warn(dev, "Self loop back prevention is not supported\n");
	if (!MLX5_CAP_GEN(dev, cq_moderation))
		mlx5_core_warn(dev, "CQ moderation is not supported\n");

	return true;
}

static bool is_eth_enabled(struct mlx5_core_dev *dev)
{
	union devlink_param_value val;
	int err;

	err = devlink_param_driverinit_value_get(priv_to_devlink(dev),
						 DEVLINK_PARAM_GENERIC_ID_ENABLE_ETH,
						 &val);
	return err ? false : val.vbool;
}

bool mlx5_vnet_supported(struct mlx5_core_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MLX5_VDPA_NET))
		return false;

	if (mlx5_core_is_pf(dev))
		return false;

	if (!(MLX5_CAP_GEN_64(dev, general_obj_types) &
	      MLX5_GENERAL_OBJ_TYPES_CAP_VIRTIO_NET_Q))
		return false;

	if (!(MLX5_CAP_DEV_VDPA_EMULATION(dev, event_mode) &
	      MLX5_VIRTIO_Q_EVENT_MODE_QP_MODE))
		return false;

	if (!MLX5_CAP_DEV_VDPA_EMULATION(dev, eth_frame_offload_type))
		return false;

	return true;
}

static bool is_vnet_enabled(struct mlx5_core_dev *dev)
{
	union devlink_param_value val;
	int err;

	err = devlink_param_driverinit_value_get(priv_to_devlink(dev),
						 DEVLINK_PARAM_GENERIC_ID_ENABLE_VNET,
						 &val);
	return err ? false : val.vbool;
}

static bool is_ib_rep_supported(struct mlx5_core_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MLX5_INFINIBAND))
		return false;

	if (dev->priv.flags & MLX5_PRIV_FLAGS_DISABLE_IB_ADEV)
		return false;

	if (!is_eth_rep_supported(dev))
		return false;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return false;

	if (!is_mdev_switchdev_mode(dev))
		return false;

	if (mlx5_core_mp_enabled(dev))
		return false;

	return true;
}

static bool is_mp_supported(struct mlx5_core_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MLX5_INFINIBAND))
		return false;

	if (dev->priv.flags & MLX5_PRIV_FLAGS_DISABLE_IB_ADEV)
		return false;

	if (is_ib_rep_supported(dev))
		return false;

	if (MLX5_CAP_GEN(dev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return false;

	if (!mlx5_core_is_mp_slave(dev))
		return false;

	return true;
}

bool mlx5_rdma_supported(struct mlx5_core_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MLX5_INFINIBAND))
		return false;

	if (dev->priv.flags & MLX5_PRIV_FLAGS_DISABLE_IB_ADEV)
		return false;

	if (is_ib_rep_supported(dev))
		return false;

	if (is_mp_supported(dev))
		return false;

	return true;
}

static bool is_ib_enabled(struct mlx5_core_dev *dev)
{
	union devlink_param_value val;
	int err;

	err = devlink_param_driverinit_value_get(priv_to_devlink(dev),
						 DEVLINK_PARAM_GENERIC_ID_ENABLE_RDMA,
						 &val);
	return err ? false : val.vbool;
}

enum {
	MLX5_INTERFACE_PROTOCOL_ETH,
	MLX5_INTERFACE_PROTOCOL_ETH_REP,

	MLX5_INTERFACE_PROTOCOL_IB,
	MLX5_INTERFACE_PROTOCOL_IB_REP,
	MLX5_INTERFACE_PROTOCOL_MPIB,

	MLX5_INTERFACE_PROTOCOL_VNET,
};

static const struct mlx5_adev_device {
	const char *suffix;
	bool (*is_supported)(struct mlx5_core_dev *dev);
	bool (*is_enabled)(struct mlx5_core_dev *dev);
} mlx5_adev_devices[] = {
	[MLX5_INTERFACE_PROTOCOL_VNET] = { .suffix = "vnet",
					   .is_supported = &mlx5_vnet_supported,
					   .is_enabled = &is_vnet_enabled },
	[MLX5_INTERFACE_PROTOCOL_IB] = { .suffix = "rdma",
					 .is_supported = &mlx5_rdma_supported,
					 .is_enabled = &is_ib_enabled },
	[MLX5_INTERFACE_PROTOCOL_ETH] = { .suffix = "eth",
					  .is_supported = &mlx5_eth_supported,
					  .is_enabled = &is_eth_enabled },
	[MLX5_INTERFACE_PROTOCOL_ETH_REP] = { .suffix = "eth-rep",
					   .is_supported = &is_eth_rep_supported },
	[MLX5_INTERFACE_PROTOCOL_IB_REP] = { .suffix = "rdma-rep",
					   .is_supported = &is_ib_rep_supported },
	[MLX5_INTERFACE_PROTOCOL_MPIB] = { .suffix = "multiport",
					   .is_supported = &is_mp_supported },
};

int mlx5_adev_idx_alloc(void)
{
	return ida_alloc(&mlx5_adev_ida, GFP_KERNEL);
}

void mlx5_adev_idx_free(int idx)
{
	ida_free(&mlx5_adev_ida, idx);
}

int mlx5_adev_init(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	priv->adev = kcalloc(ARRAY_SIZE(mlx5_adev_devices),
			     sizeof(struct mlx5_adev *), GFP_KERNEL);
	if (!priv->adev)
		return -ENOMEM;

	return 0;
}

void mlx5_adev_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	kfree(priv->adev);
}

static void adev_release(struct device *dev)
{
	struct mlx5_adev *mlx5_adev =
		container_of(dev, struct mlx5_adev, adev.dev);
	struct mlx5_priv *priv = &mlx5_adev->mdev->priv;
	int idx = mlx5_adev->idx;

	kfree(mlx5_adev);
	priv->adev[idx] = NULL;
}

static struct mlx5_adev *add_adev(struct mlx5_core_dev *dev, int idx)
{
	const char *suffix = mlx5_adev_devices[idx].suffix;
	struct auxiliary_device *adev;
	struct mlx5_adev *madev;
	int ret;

	madev = kzalloc(sizeof(*madev), GFP_KERNEL);
	if (!madev)
		return ERR_PTR(-ENOMEM);

	adev = &madev->adev;
	adev->id = dev->priv.adev_idx;
	adev->name = suffix;
	adev->dev.parent = dev->device;
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

int mlx5_attach_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct auxiliary_device *adev;
	struct auxiliary_driver *adrv;
	int ret = 0, i;

	devl_assert_locked(priv_to_devlink(dev));
	mutex_lock(&mlx5_intf_mutex);
	priv->flags &= ~MLX5_PRIV_FLAGS_DETACH;
	priv->flags |= MLX5_PRIV_FLAGS_MLX5E_LOCKED_FLOW;
	for (i = 0; i < ARRAY_SIZE(mlx5_adev_devices); i++) {
		if (!priv->adev[i]) {
			bool is_supported = false;

			if (mlx5_adev_devices[i].is_enabled) {
				bool enabled;

				enabled = mlx5_adev_devices[i].is_enabled(dev);
				if (!enabled)
					continue;
			}

			if (mlx5_adev_devices[i].is_supported)
				is_supported = mlx5_adev_devices[i].is_supported(dev);

			if (!is_supported)
				continue;

			priv->adev[i] = add_adev(dev, i);
			if (IS_ERR(priv->adev[i])) {
				ret = PTR_ERR(priv->adev[i]);
				priv->adev[i] = NULL;
			}
		} else {
			adev = &priv->adev[i]->adev;

			/* Pay attention that this is not PCI driver that
			 * mlx5_core_dev is connected, but auxiliary driver.
			 *
			 * Here we can race of module unload with devlink
			 * reload, but we don't need to take extra lock because
			 * we are holding global mlx5_intf_mutex.
			 */
			if (!adev->dev.driver)
				continue;
			adrv = to_auxiliary_drv(adev->dev.driver);

			if (adrv->resume)
				ret = adrv->resume(adev);
		}
		if (ret) {
			mlx5_core_warn(dev, "Device[%d] (%s) failed to load\n",
				       i, mlx5_adev_devices[i].suffix);

			break;
		}
	}
	priv->flags &= ~MLX5_PRIV_FLAGS_MLX5E_LOCKED_FLOW;
	mutex_unlock(&mlx5_intf_mutex);
	return ret;
}

void mlx5_detach_device(struct mlx5_core_dev *dev, bool suspend)
{
	struct mlx5_priv *priv = &dev->priv;
	struct auxiliary_device *adev;
	struct auxiliary_driver *adrv;
	pm_message_t pm = {};
	int i;

	devl_assert_locked(priv_to_devlink(dev));
	mutex_lock(&mlx5_intf_mutex);
	priv->flags |= MLX5_PRIV_FLAGS_MLX5E_LOCKED_FLOW;
	for (i = ARRAY_SIZE(mlx5_adev_devices) - 1; i >= 0; i--) {
		if (!priv->adev[i])
			continue;

		if (mlx5_adev_devices[i].is_enabled) {
			bool enabled;

			enabled = mlx5_adev_devices[i].is_enabled(dev);
			if (!enabled)
				goto skip_suspend;
		}

		adev = &priv->adev[i]->adev;
		/* Auxiliary driver was unbind manually through sysfs */
		if (!adev->dev.driver)
			goto skip_suspend;

		adrv = to_auxiliary_drv(adev->dev.driver);

		if (adrv->suspend && suspend) {
			adrv->suspend(adev, pm);
			continue;
		}

skip_suspend:
		del_adev(&priv->adev[i]->adev);
		priv->adev[i] = NULL;
	}
	priv->flags &= ~MLX5_PRIV_FLAGS_MLX5E_LOCKED_FLOW;
	priv->flags |= MLX5_PRIV_FLAGS_DETACH;
	mutex_unlock(&mlx5_intf_mutex);
}

int mlx5_register_device(struct mlx5_core_dev *dev)
{
	int ret;

	devl_assert_locked(priv_to_devlink(dev));
	mutex_lock(&mlx5_intf_mutex);
	dev->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV;
	ret = mlx5_rescan_drivers_locked(dev);
	mutex_unlock(&mlx5_intf_mutex);
	if (ret)
		mlx5_unregister_device(dev);

	return ret;
}

void mlx5_unregister_device(struct mlx5_core_dev *dev)
{
	devl_assert_locked(priv_to_devlink(dev));
	mutex_lock(&mlx5_intf_mutex);
	dev->priv.flags = MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV;
	mlx5_rescan_drivers_locked(dev);
	mutex_unlock(&mlx5_intf_mutex);
}

static int add_drivers(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(mlx5_adev_devices); i++) {
		bool is_supported = false;

		if (priv->adev[i])
			continue;

		if (mlx5_adev_devices[i].is_supported)
			is_supported = mlx5_adev_devices[i].is_supported(dev);

		if (!is_supported)
			continue;

		priv->adev[i] = add_adev(dev, i);
		if (IS_ERR(priv->adev[i])) {
			mlx5_core_warn(dev, "Device[%d] (%s) failed to load\n",
				       i, mlx5_adev_devices[i].suffix);
			/* We continue to rescan drivers and leave to the caller
			 * to make decision if to release everything or continue.
			 */
			ret = PTR_ERR(priv->adev[i]);
			priv->adev[i] = NULL;
		}
	}
	return ret;
}

static void delete_drivers(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	bool delete_all;
	int i;

	delete_all = priv->flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV;

	for (i = ARRAY_SIZE(mlx5_adev_devices) - 1; i >= 0; i--) {
		bool is_supported = false;

		if (!priv->adev[i])
			continue;

		if (mlx5_adev_devices[i].is_enabled) {
			bool enabled;

			enabled = mlx5_adev_devices[i].is_enabled(dev);
			if (!enabled)
				goto del_adev;
		}

		if (mlx5_adev_devices[i].is_supported && !delete_all)
			is_supported = mlx5_adev_devices[i].is_supported(dev);

		if (is_supported)
			continue;

del_adev:
		del_adev(&priv->adev[i]->adev);
		priv->adev[i] = NULL;
	}
}

/* This function is used after mlx5_core_dev is reconfigured.
 */
int mlx5_rescan_drivers_locked(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	int err = 0;

	lockdep_assert_held(&mlx5_intf_mutex);
	if (priv->flags & MLX5_PRIV_FLAGS_DETACH)
		return 0;

	priv->flags |= MLX5_PRIV_FLAGS_MLX5E_LOCKED_FLOW;
	delete_drivers(dev);
	if (priv->flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)
		goto out;

	err = add_drivers(dev);

out:
	priv->flags &= ~MLX5_PRIV_FLAGS_MLX5E_LOCKED_FLOW;
	return err;
}

bool mlx5_same_hw_devs(struct mlx5_core_dev *dev, struct mlx5_core_dev *peer_dev)
{
	u64 fsystem_guid, psystem_guid;

	fsystem_guid = mlx5_query_nic_system_image_guid(dev);
	psystem_guid = mlx5_query_nic_system_image_guid(peer_dev);

	return (fsystem_guid && psystem_guid && fsystem_guid == psystem_guid);
}

static u32 mlx5_gen_pci_id(const struct mlx5_core_dev *dev)
{
	return (u32)((pci_domain_nr(dev->pdev->bus) << 16) |
		     (dev->pdev->bus->number << 8) |
		     PCI_SLOT(dev->pdev->devfn));
}

static int _next_phys_dev(struct mlx5_core_dev *mdev,
			  const struct mlx5_core_dev *curr)
{
	if (!mlx5_core_is_pf(mdev))
		return 0;

	if (mdev == curr)
		return 0;

	if (!mlx5_same_hw_devs(mdev, (struct mlx5_core_dev *)curr) &&
	    mlx5_gen_pci_id(mdev) != mlx5_gen_pci_id(curr))
		return 0;

	return 1;
}

static void *pci_get_other_drvdata(struct device *this, struct device *other)
{
	if (this->driver != other->driver)
		return NULL;

	return pci_get_drvdata(to_pci_dev(other));
}

static int next_phys_dev_lag(struct device *dev, const void *data)
{
	struct mlx5_core_dev *mdev, *this = (struct mlx5_core_dev *)data;

	mdev = pci_get_other_drvdata(this->device, dev);
	if (!mdev)
		return 0;

	if (!MLX5_CAP_GEN(mdev, vport_group_manager) ||
	    !MLX5_CAP_GEN(mdev, lag_master) ||
	    (MLX5_CAP_GEN(mdev, num_lag_ports) > MLX5_MAX_PORTS ||
	     MLX5_CAP_GEN(mdev, num_lag_ports) <= 1))
		return 0;

	return _next_phys_dev(mdev, data);
}

static struct mlx5_core_dev *mlx5_get_next_dev(struct mlx5_core_dev *dev,
					       int (*match)(struct device *dev, const void *data))
{
	struct device *next;

	if (!mlx5_core_is_pf(dev))
		return NULL;

	next = bus_find_device(&pci_bus_type, NULL, dev, match);
	if (!next)
		return NULL;

	put_device(next);
	return pci_get_drvdata(to_pci_dev(next));
}

/* Must be called with intf_mutex held */
struct mlx5_core_dev *mlx5_get_next_phys_dev_lag(struct mlx5_core_dev *dev)
{
	lockdep_assert_held(&mlx5_intf_mutex);
	return mlx5_get_next_dev(dev, &next_phys_dev_lag);
}

void mlx5_dev_list_lock(void)
{
	mutex_lock(&mlx5_intf_mutex);
}
void mlx5_dev_list_unlock(void)
{
	mutex_unlock(&mlx5_intf_mutex);
}

int mlx5_dev_list_trylock(void)
{
	return mutex_trylock(&mlx5_intf_mutex);
}
