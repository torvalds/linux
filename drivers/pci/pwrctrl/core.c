// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#define dev_fmt(fmt) "pwrctrl: " fmt

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "../pci.h"

static int pci_pwrctrl_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct pci_pwrctrl *pwrctrl = container_of(nb, struct pci_pwrctrl, nb);
	struct device *dev = data;

	if (dev_fwnode(dev) != dev_fwnode(pwrctrl->dev))
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		/*
		 * We will have two struct device objects bound to two different
		 * drivers on different buses but consuming the same DT node. We
		 * must not bind the pins twice in this case but only once for
		 * the first device to be added.
		 *
		 * If we got here then the PCI device is the second after the
		 * power control platform device. Mark its OF node as reused.
		 */
		dev->of_node_reused = true;
		break;
	}

	return NOTIFY_DONE;
}

static void rescan_work_func(struct work_struct *work)
{
	struct pci_pwrctrl *pwrctrl = container_of(work,
						   struct pci_pwrctrl, work);

	pci_lock_rescan_remove();
	pci_rescan_bus(to_pci_host_bridge(pwrctrl->dev->parent)->bus);
	pci_unlock_rescan_remove();
}

/**
 * pci_pwrctrl_init() - Initialize the PCI power control context struct
 *
 * @pwrctrl: PCI power control data
 * @dev: Parent device
 */
void pci_pwrctrl_init(struct pci_pwrctrl *pwrctrl, struct device *dev)
{
	pwrctrl->dev = dev;
	INIT_WORK(&pwrctrl->work, rescan_work_func);
}
EXPORT_SYMBOL_GPL(pci_pwrctrl_init);

/**
 * pci_pwrctrl_device_set_ready() - Notify the pwrctrl subsystem that the PCI
 * device is powered-up and ready to be detected.
 *
 * @pwrctrl: PCI power control data.
 *
 * Returns:
 * 0 on success, negative error number on error.
 *
 * Note:
 * This function returning 0 doesn't mean the device was detected. It means,
 * that the bus rescan was successfully started. The device will get bound to
 * its PCI driver asynchronously.
 */
int pci_pwrctrl_device_set_ready(struct pci_pwrctrl *pwrctrl)
{
	int ret;

	if (!pwrctrl->dev)
		return -ENODEV;

	pwrctrl->nb.notifier_call = pci_pwrctrl_notify;
	ret = bus_register_notifier(&pci_bus_type, &pwrctrl->nb);
	if (ret)
		return ret;

	schedule_work(&pwrctrl->work);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_pwrctrl_device_set_ready);

/**
 * pci_pwrctrl_device_unset_ready() - Notify the pwrctrl subsystem that the PCI
 * device is about to be powered-down.
 *
 * @pwrctrl: PCI power control data.
 */
void pci_pwrctrl_device_unset_ready(struct pci_pwrctrl *pwrctrl)
{
	cancel_work_sync(&pwrctrl->work);

	/*
	 * We don't have to delete the link here. Typically, this function
	 * is only called when the power control device is being detached. If
	 * it is being detached then the child PCI device must have already
	 * been unbound too or the device core wouldn't let us unbind.
	 */
	bus_unregister_notifier(&pci_bus_type, &pwrctrl->nb);
}
EXPORT_SYMBOL_GPL(pci_pwrctrl_device_unset_ready);

static void devm_pci_pwrctrl_device_unset_ready(void *data)
{
	struct pci_pwrctrl *pwrctrl = data;

	pci_pwrctrl_device_unset_ready(pwrctrl);
}

/**
 * devm_pci_pwrctrl_device_set_ready - Managed variant of
 * pci_pwrctrl_device_set_ready().
 *
 * @dev: Device managing this pwrctrl provider.
 * @pwrctrl: PCI power control data.
 *
 * Returns:
 * 0 on success, negative error number on error.
 */
int devm_pci_pwrctrl_device_set_ready(struct device *dev,
				      struct pci_pwrctrl *pwrctrl)
{
	int ret;

	ret = pci_pwrctrl_device_set_ready(pwrctrl);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev,
					devm_pci_pwrctrl_device_unset_ready,
					pwrctrl);
}
EXPORT_SYMBOL_GPL(devm_pci_pwrctrl_device_set_ready);

static int pci_pwrctrl_create_device(struct device_node *np,
				     struct device *parent)
{
	struct platform_device *pdev;
	int ret;

	for_each_available_child_of_node_scoped(np, child) {
		ret = pci_pwrctrl_create_device(child, parent);
		if (ret)
			return ret;
	}

	/* Bail out if the platform device is already available for the node */
	pdev = of_find_device_by_node(np);
	if (pdev) {
		platform_device_put(pdev);
		return 0;
	}

	/*
	 * Sanity check to make sure that the node has the compatible property
	 * to allow driver binding.
	 */
	if (!of_property_present(np, "compatible"))
		return 0;

	/*
	 * Check whether the pwrctrl device really needs to be created or not.
	 * This is decided based on at least one of the power supplies being
	 * defined in the devicetree node of the device.
	 */
	if (!of_pci_supply_present(np)) {
		dev_dbg(parent, "Skipping OF node: %s\n", np->name);
		return 0;
	}

	/* Now create the pwrctrl device */
	pdev = of_platform_device_create(np, NULL, parent);
	if (!pdev) {
		dev_err(parent, "Failed to create pwrctrl device for node: %s\n", np->name);
		return -EINVAL;
	}

	return 0;
}

/**
 * pci_pwrctrl_create_devices - Create pwrctrl devices
 *
 * @parent: PCI host controller device
 *
 * Recursively create pwrctrl devices for the devicetree hierarchy below
 * the specified PCI host controller in a depth first manner. On error, all
 * created devices will be destroyed.
 *
 * Return: 0 on success, negative error number on error.
 */
int pci_pwrctrl_create_devices(struct device *parent)
{
	int ret;

	for_each_available_child_of_node_scoped(parent->of_node, child) {
		ret = pci_pwrctrl_create_device(child, parent);
		if (ret) {
			pci_pwrctrl_destroy_devices(parent);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pci_pwrctrl_create_devices);

static void pci_pwrctrl_destroy_device(struct device_node *np)
{
	struct platform_device *pdev;

	for_each_available_child_of_node_scoped(np, child)
		pci_pwrctrl_destroy_device(child);

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return;

	of_device_unregister(pdev);
	platform_device_put(pdev);

	of_node_clear_flag(np, OF_POPULATED);
}

/**
 * pci_pwrctrl_destroy_devices - Destroy pwrctrl devices
 *
 * @parent: PCI host controller device
 *
 * Recursively destroy pwrctrl devices for the devicetree hierarchy below
 * the specified PCI host controller in a depth first manner.
 */
void pci_pwrctrl_destroy_devices(struct device *parent)
{
	struct device_node *np = parent->of_node;

	for_each_available_child_of_node_scoped(np, child)
		pci_pwrctrl_destroy_device(child);
}
EXPORT_SYMBOL_GPL(pci_pwrctrl_destroy_devices);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("PCI Device Power Control core driver");
MODULE_LICENSE("GPL");
