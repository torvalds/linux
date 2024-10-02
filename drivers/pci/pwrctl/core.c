// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-pwrctl.h>
#include <linux/property.h>
#include <linux/slab.h>

static int pci_pwrctl_notify(struct notifier_block *nb, unsigned long action,
			     void *data)
{
	struct pci_pwrctl *pwrctl = container_of(nb, struct pci_pwrctl, nb);
	struct device *dev = data;

	if (dev_fwnode(dev) != dev_fwnode(pwrctl->dev))
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
	case BUS_NOTIFY_BOUND_DRIVER:
		pwrctl->link = device_link_add(dev, pwrctl->dev,
					       DL_FLAG_AUTOREMOVE_CONSUMER);
		if (!pwrctl->link)
			dev_err(pwrctl->dev, "Failed to add device link\n");
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		if (pwrctl->link)
			device_link_remove(dev, pwrctl->dev);
		break;
	}

	return NOTIFY_DONE;
}

static void rescan_work_func(struct work_struct *work)
{
	struct pci_pwrctl *pwrctl = container_of(work, struct pci_pwrctl, work);

	pci_lock_rescan_remove();
	pci_rescan_bus(to_pci_dev(pwrctl->dev->parent)->bus);
	pci_unlock_rescan_remove();
}

/**
 * pci_pwrctl_init() - Initialize the PCI power control context struct
 *
 * @pwrctl: PCI power control data
 * @dev: Parent device
 */
void pci_pwrctl_init(struct pci_pwrctl *pwrctl, struct device *dev)
{
	pwrctl->dev = dev;
	INIT_WORK(&pwrctl->work, rescan_work_func);
}
EXPORT_SYMBOL_GPL(pci_pwrctl_init);

/**
 * pci_pwrctl_device_set_ready() - Notify the pwrctl subsystem that the PCI
 * device is powered-up and ready to be detected.
 *
 * @pwrctl: PCI power control data.
 *
 * Returns:
 * 0 on success, negative error number on error.
 *
 * Note:
 * This function returning 0 doesn't mean the device was detected. It means,
 * that the bus rescan was successfully started. The device will get bound to
 * its PCI driver asynchronously.
 */
int pci_pwrctl_device_set_ready(struct pci_pwrctl *pwrctl)
{
	int ret;

	if (!pwrctl->dev)
		return -ENODEV;

	pwrctl->nb.notifier_call = pci_pwrctl_notify;
	ret = bus_register_notifier(&pci_bus_type, &pwrctl->nb);
	if (ret)
		return ret;

	schedule_work(&pwrctl->work);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_pwrctl_device_set_ready);

/**
 * pci_pwrctl_device_unset_ready() - Notify the pwrctl subsystem that the PCI
 * device is about to be powered-down.
 *
 * @pwrctl: PCI power control data.
 */
void pci_pwrctl_device_unset_ready(struct pci_pwrctl *pwrctl)
{
	/*
	 * We don't have to delete the link here. Typically, this function
	 * is only called when the power control device is being detached. If
	 * it is being detached then the child PCI device must have already
	 * been unbound too or the device core wouldn't let us unbind.
	 */
	bus_unregister_notifier(&pci_bus_type, &pwrctl->nb);
}
EXPORT_SYMBOL_GPL(pci_pwrctl_device_unset_ready);

static void devm_pci_pwrctl_device_unset_ready(void *data)
{
	struct pci_pwrctl *pwrctl = data;

	pci_pwrctl_device_unset_ready(pwrctl);
}

/**
 * devm_pci_pwrctl_device_set_ready - Managed variant of
 * pci_pwrctl_device_set_ready().
 *
 * @dev: Device managing this pwrctl provider.
 * @pwrctl: PCI power control data.
 *
 * Returns:
 * 0 on success, negative error number on error.
 */
int devm_pci_pwrctl_device_set_ready(struct device *dev,
				     struct pci_pwrctl *pwrctl)
{
	int ret;

	ret = pci_pwrctl_device_set_ready(pwrctl);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev,
					devm_pci_pwrctl_device_unset_ready,
					pwrctl);
}
EXPORT_SYMBOL_GPL(devm_pci_pwrctl_device_set_ready);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("PCI Device Power Control core driver");
MODULE_LICENSE("GPL");
