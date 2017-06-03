/*
 *  drivers/extcon/devres.c - EXTCON device's resource management
 *
 * Copyright (C) 2016 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "extcon.h"

static int devm_extcon_dev_match(struct device *dev, void *res, void *data)
{
	struct extcon_dev **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

static void devm_extcon_dev_release(struct device *dev, void *res)
{
	extcon_dev_free(*(struct extcon_dev **)res);
}


static void devm_extcon_dev_unreg(struct device *dev, void *res)
{
	extcon_dev_unregister(*(struct extcon_dev **)res);
}

struct extcon_dev_notifier_devres {
	struct extcon_dev *edev;
	unsigned int id;
	struct notifier_block *nb;
};

static void devm_extcon_dev_notifier_unreg(struct device *dev, void *res)
{
	struct extcon_dev_notifier_devres *this = res;

	extcon_unregister_notifier(this->edev, this->id, this->nb);
}

static void devm_extcon_dev_notifier_all_unreg(struct device *dev, void *res)
{
	struct extcon_dev_notifier_devres *this = res;

	extcon_unregister_notifier_all(this->edev, this->nb);
}

/**
 * devm_extcon_dev_allocate - Allocate managed extcon device
 * @dev:		device owning the extcon device being created
 * @supported_cable:	Array of supported extcon ending with EXTCON_NONE.
 *			If supported_cable is NULL, cable name related APIs
 *			are disabled.
 *
 * This function manages automatically the memory of extcon device using device
 * resource management and simplify the control of freeing the memory of extcon
 * device.
 *
 * Returns the pointer memory of allocated extcon_dev if success
 * or ERR_PTR(err) if fail
 */
struct extcon_dev *devm_extcon_dev_allocate(struct device *dev,
					const unsigned int *supported_cable)
{
	struct extcon_dev **ptr, *edev;

	ptr = devres_alloc(devm_extcon_dev_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	edev = extcon_dev_allocate(supported_cable);
	if (IS_ERR(edev)) {
		devres_free(ptr);
		return edev;
	}

	edev->dev.parent = dev;

	*ptr = edev;
	devres_add(dev, ptr);

	return edev;
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_allocate);

/**
 * devm_extcon_dev_free() - Resource-managed extcon_dev_unregister()
 * @dev:	device the extcon belongs to
 * @edev:	the extcon device to unregister
 *
 * Free the memory that is allocated with devm_extcon_dev_allocate()
 * function.
 */
void devm_extcon_dev_free(struct device *dev, struct extcon_dev *edev)
{
	WARN_ON(devres_release(dev, devm_extcon_dev_release,
			       devm_extcon_dev_match, edev));
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_free);

/**
 * devm_extcon_dev_register() - Resource-managed extcon_dev_register()
 * @dev:	device to allocate extcon device
 * @edev:	the new extcon device to register
 *
 * Managed extcon_dev_register() function. If extcon device is attached with
 * this function, that extcon device is automatically unregistered on driver
 * detach. Internally this function calls extcon_dev_register() function.
 * To get more information, refer that function.
 *
 * If extcon device is registered with this function and the device needs to be
 * unregistered separately, devm_extcon_dev_unregister() should be used.
 *
 * Returns 0 if success or negaive error number if failure.
 */
int devm_extcon_dev_register(struct device *dev, struct extcon_dev *edev)
{
	struct extcon_dev **ptr;
	int ret;

	ptr = devres_alloc(devm_extcon_dev_unreg, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = extcon_dev_register(edev);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	*ptr = edev;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_register);

/**
 * devm_extcon_dev_unregister() - Resource-managed extcon_dev_unregister()
 * @dev:	device the extcon belongs to
 * @edev:	the extcon device to unregister
 *
 * Unregister extcon device that is registered with devm_extcon_dev_register()
 * function.
 */
void devm_extcon_dev_unregister(struct device *dev, struct extcon_dev *edev)
{
	WARN_ON(devres_release(dev, devm_extcon_dev_unreg,
			       devm_extcon_dev_match, edev));
}
EXPORT_SYMBOL_GPL(devm_extcon_dev_unregister);

/**
 * devm_extcon_register_notifier() - Resource-managed extcon_register_notifier()
 * @dev:	device to allocate extcon device
 * @edev:	the extcon device that has the external connecotr.
 * @id:		the unique id of each external connector in extcon enumeration.
 * @nb:		a notifier block to be registered.
 *
 * This function manages automatically the notifier of extcon device using
 * device resource management and simplify the control of unregistering
 * the notifier of extcon device.
 *
 * Note that the second parameter given to the callback of nb (val) is
 * "old_state", not the current state. The current state can be retrieved
 * by looking at the third pameter (edev pointer)'s state value.
 *
 * Returns 0 if success or negaive error number if failure.
 */
int devm_extcon_register_notifier(struct device *dev, struct extcon_dev *edev,
				unsigned int id, struct notifier_block *nb)
{
	struct extcon_dev_notifier_devres *ptr;
	int ret;

	ptr = devres_alloc(devm_extcon_dev_notifier_unreg, sizeof(*ptr),
				GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = extcon_register_notifier(edev, id, nb);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	ptr->edev = edev;
	ptr->id = id;
	ptr->nb = nb;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_extcon_register_notifier);

/**
 * devm_extcon_unregister_notifier()
			- Resource-managed extcon_unregister_notifier()
 * @dev:	device to allocate extcon device
 * @edev:	the extcon device that has the external connecotr.
 * @id:		the unique id of each external connector in extcon enumeration.
 * @nb:		a notifier block to be registered.
 */
void devm_extcon_unregister_notifier(struct device *dev,
				struct extcon_dev *edev, unsigned int id,
				struct notifier_block *nb)
{
	WARN_ON(devres_release(dev, devm_extcon_dev_notifier_unreg,
			       devm_extcon_dev_match, edev));
}
EXPORT_SYMBOL(devm_extcon_unregister_notifier);

/**
 * devm_extcon_register_notifier_all()
 *		- Resource-managed extcon_register_notifier_all()
 * @dev:	device to allocate extcon device
 * @edev:	the extcon device that has the external connecotr.
 * @nb:		a notifier block to be registered.
 *
 * This function manages automatically the notifier of extcon device using
 * device resource management and simplify the control of unregistering
 * the notifier of extcon device. To get more information, refer that function.
 *
 * Returns 0 if success or negaive error number if failure.
 */
int devm_extcon_register_notifier_all(struct device *dev, struct extcon_dev *edev,
				struct notifier_block *nb)
{
	struct extcon_dev_notifier_devres *ptr;
	int ret;

	ptr = devres_alloc(devm_extcon_dev_notifier_all_unreg, sizeof(*ptr),
				GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = extcon_register_notifier_all(edev, nb);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	ptr->edev = edev;
	ptr->nb = nb;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_extcon_register_notifier_all);

/**
 * devm_extcon_unregister_notifier_all()
 *		- Resource-managed extcon_unregister_notifier_all()
 * @dev:	device to allocate extcon device
 * @edev:	the extcon device that has the external connecotr.
 * @nb:		a notifier block to be registered.
 */
void devm_extcon_unregister_notifier_all(struct device *dev,
				struct extcon_dev *edev,
				struct notifier_block *nb)
{
	WARN_ON(devres_release(dev, devm_extcon_dev_notifier_all_unreg,
			       devm_extcon_dev_match, edev));
}
EXPORT_SYMBOL(devm_extcon_unregister_notifier_all);
