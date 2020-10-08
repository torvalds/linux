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
#include "mlx5_core.h"

static LIST_HEAD(intf_list);
static LIST_HEAD(mlx5_dev_list);
/* intf dev list mutex */
static DEFINE_MUTEX(mlx5_intf_mutex);
static DEFINE_IDA(mlx5_adev_ida);

struct mlx5_device_context {
	struct list_head	list;
	struct mlx5_interface  *intf;
	void		       *context;
	unsigned long		state;
};

enum {
	MLX5_INTERFACE_ADDED,
	MLX5_INTERFACE_ATTACHED,
};

static const struct mlx5_adev_device {
	const char *suffix;
	bool (*is_supported)(struct mlx5_core_dev *dev);
} mlx5_adev_devices[1] = {};

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

void mlx5_add_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	if (!mlx5_lag_intf_add(intf, priv))
		return;

	dev_ctx = kzalloc(sizeof(*dev_ctx), GFP_KERNEL);
	if (!dev_ctx)
		return;

	dev_ctx->intf = intf;

	dev_ctx->context = intf->add(dev);
	if (dev_ctx->context) {
		set_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state);
		if (intf->attach)
			set_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state);

		spin_lock_irq(&priv->ctx_lock);
		list_add_tail(&dev_ctx->list, &priv->ctx_list);
		spin_unlock_irq(&priv->ctx_lock);
	}

	if (!dev_ctx->context)
		kfree(dev_ctx);
}

static struct mlx5_device_context *mlx5_get_device(struct mlx5_interface *intf,
						   struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf == intf)
			return dev_ctx;
	return NULL;
}

void mlx5_remove_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = mlx5_get_device(intf, priv);
	if (!dev_ctx)
		return;

	spin_lock_irq(&priv->ctx_lock);
	list_del(&dev_ctx->list);
	spin_unlock_irq(&priv->ctx_lock);

	if (test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
		intf->remove(dev, dev_ctx->context);

	kfree(dev_ctx);
}

static void mlx5_attach_interface(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = mlx5_get_device(intf, priv);
	if (!dev_ctx)
		return;

	if (intf->attach) {
		if (test_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state))
			return;
		if (intf->attach(dev, dev_ctx->context))
			return;
		set_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state);
	} else {
		if (test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
			return;
		dev_ctx->context = intf->add(dev);
		if (!dev_ctx->context)
			return;
		set_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state);
	}
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
	struct mlx5_interface *intf;
	int ret = 0, i;

	mutex_lock(&mlx5_intf_mutex);
	for (i = 0; i < ARRAY_SIZE(mlx5_adev_devices); i++) {
		if (!priv->adev[i]) {
			bool is_supported = false;

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

	list_for_each_entry(intf, &intf_list, list)
		mlx5_attach_interface(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);
	return ret;
}

static void mlx5_detach_interface(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = mlx5_get_device(intf, priv);
	if (!dev_ctx)
		return;

	if (intf->detach) {
		if (!test_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state))
			return;
		intf->detach(dev, dev_ctx->context);
		clear_bit(MLX5_INTERFACE_ATTACHED, &dev_ctx->state);
	} else {
		if (!test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
			return;
		intf->remove(dev, dev_ctx->context);
		clear_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state);
	}
}

void mlx5_detach_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct auxiliary_device *adev;
	struct auxiliary_driver *adrv;
	struct mlx5_interface *intf;
	pm_message_t pm = {};
	int i;

	mutex_lock(&mlx5_intf_mutex);
	for (i = ARRAY_SIZE(mlx5_adev_devices) - 1; i >= 0; i--) {
		if (!priv->adev[i])
			continue;

		adev = &priv->adev[i]->adev;
		adrv = to_auxiliary_drv(adev->dev.driver);

		if (adrv->suspend) {
			adrv->suspend(adev, pm);
			continue;
		}

		del_adev(&priv->adev[i]->adev);
		priv->adev[i] = NULL;
	}

	list_for_each_entry(intf, &intf_list, list)
		mlx5_detach_interface(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);
}

bool mlx5_device_registered(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv;
	bool found = false;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list)
		if (priv == &dev->priv)
			found = true;
	mutex_unlock(&mlx5_intf_mutex);

	return found;
}

int mlx5_register_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;
	int ret;

	mutex_lock(&mlx5_intf_mutex);
	dev->priv.flags &= ~MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV;
	ret = mlx5_rescan_drivers_locked(dev);
	mutex_unlock(&mlx5_intf_mutex);
	if (ret)
		goto add_err;

	mutex_lock(&mlx5_intf_mutex);
	list_add_tail(&priv->dev_list, &mlx5_dev_list);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_add_device(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);

	return 0;

add_err:
	mlx5_unregister_device(dev);
	return ret;
}

void mlx5_unregister_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry_reverse(intf, &intf_list, list)
		mlx5_remove_device(intf, priv);
	list_del(&priv->dev_list);

	dev->priv.flags |= MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV;
	mlx5_rescan_drivers_locked(dev);
	mutex_unlock(&mlx5_intf_mutex);
}

int mlx5_register_interface(struct mlx5_interface *intf)
{
	struct mlx5_priv *priv;

	if (!intf->add || !intf->remove)
		return -EINVAL;

	mutex_lock(&mlx5_intf_mutex);
	list_add_tail(&intf->list, &intf_list);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list)
		mlx5_add_device(intf, priv);
	mutex_unlock(&mlx5_intf_mutex);

	return 0;
}
EXPORT_SYMBOL(mlx5_register_interface);

void mlx5_unregister_interface(struct mlx5_interface *intf)
{
	struct mlx5_priv *priv;

	mutex_lock(&mlx5_intf_mutex);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list)
		mlx5_remove_device(intf, priv);
	list_del(&intf->list);
	mutex_unlock(&mlx5_intf_mutex);
}
EXPORT_SYMBOL(mlx5_unregister_interface);

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

		if (mlx5_adev_devices[i].is_supported && !delete_all)
			is_supported = mlx5_adev_devices[i].is_supported(dev);

		if (is_supported)
			continue;

		del_adev(&priv->adev[i]->adev);
		priv->adev[i] = NULL;
	}
}

/* This function is used after mlx5_core_dev is reconfigured.
 */
int mlx5_rescan_drivers_locked(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	lockdep_assert_held(&mlx5_intf_mutex);

	delete_drivers(dev);
	if (priv->flags & MLX5_PRIV_FLAGS_DISABLE_ALL_ADEV)
		return 0;

	return add_drivers(dev);
}

/* Must be called with intf_mutex held */
static bool mlx5_has_added_dev_by_protocol(struct mlx5_core_dev *mdev, int protocol)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_interface *intf;
	bool found = false;

	list_for_each_entry(intf, &intf_list, list) {
		if (intf->protocol == protocol) {
			dev_ctx = mlx5_get_device(intf, &mdev->priv);
			if (dev_ctx && test_bit(MLX5_INTERFACE_ADDED, &dev_ctx->state))
				found = true;
			break;
		}
	}

	return found;
}

void mlx5_reload_interface(struct mlx5_core_dev *mdev, int protocol)
{
	mutex_lock(&mlx5_intf_mutex);
	if (mlx5_has_added_dev_by_protocol(mdev, protocol)) {
		mlx5_remove_dev_by_protocol(mdev, protocol);
		mlx5_add_dev_by_protocol(mdev, protocol);
	}
	mutex_unlock(&mlx5_intf_mutex);
}

/* Must be called with intf_mutex held */
void mlx5_add_dev_by_protocol(struct mlx5_core_dev *dev, int protocol)
{
	struct mlx5_interface *intf;

	list_for_each_entry(intf, &intf_list, list)
		if (intf->protocol == protocol) {
			mlx5_add_device(intf, &dev->priv);
			break;
		}
}

/* Must be called with intf_mutex held */
void mlx5_remove_dev_by_protocol(struct mlx5_core_dev *dev, int protocol)
{
	struct mlx5_interface *intf;

	list_for_each_entry(intf, &intf_list, list)
		if (intf->protocol == protocol) {
			mlx5_remove_device(intf, &dev->priv);
			break;
		}
}

static u32 mlx5_gen_pci_id(const struct mlx5_core_dev *dev)
{
	return (u32)((pci_domain_nr(dev->pdev->bus) << 16) |
		     (dev->pdev->bus->number << 8) |
		     PCI_SLOT(dev->pdev->devfn));
}

static int next_phys_dev(struct device *dev, const void *data)
{
	struct mlx5_adev *madev = container_of(dev, struct mlx5_adev, adev.dev);
	struct mlx5_core_dev *mdev = madev->mdev;
	const struct mlx5_core_dev *curr = data;

	if (!mlx5_core_is_pf(mdev))
		return 0;

	if (mdev == curr)
		return 0;

	if (mlx5_gen_pci_id(mdev) != mlx5_gen_pci_id(curr))
		return 0;

	return 1;
}

/* This function is called with two flows:
 * 1. During initialization of mlx5_core_dev and we don't need to lock it.
 * 2. During LAG configure stage and caller holds &mlx5_intf_mutex.
 */
struct mlx5_core_dev *mlx5_get_next_phys_dev(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *res = NULL;
	struct mlx5_core_dev *tmp_dev;
	struct auxiliary_device *adev;
	struct mlx5_adev *madev;
	struct mlx5_priv *priv;
	u32 pci_id;

	if (!mlx5_core_is_pf(dev))
		return NULL;

	adev = auxiliary_find_device(NULL, dev, &next_phys_dev);
	if (adev) {
		madev = container_of(adev, struct mlx5_adev, adev);

		put_device(&adev->dev);
		return madev->mdev;
	}

	pci_id = mlx5_gen_pci_id(dev);
	list_for_each_entry(priv, &mlx5_dev_list, dev_list) {
		tmp_dev = container_of(priv, struct mlx5_core_dev, priv);
		if (!mlx5_core_is_pf(tmp_dev))
			continue;

		if ((dev != tmp_dev) && (mlx5_gen_pci_id(tmp_dev) == pci_id)) {
			res = tmp_dev;
			break;
		}
	}

	return res;
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
