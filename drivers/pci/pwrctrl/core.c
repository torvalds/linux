// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-pwrctrl.h>
#include <linux/property.h>
#include <linux/slab.h>

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

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("PCI Device Power Control core driver");
MODULE_LICENSE("GPL");
