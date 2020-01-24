// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Google LLC
 *
 * This driver serves as the receiver of cros_ec PD host events.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_usbpd_notify.h>
#include <linux/platform_device.h>

#define DRV_NAME "cros-usbpd-notify"
#define ACPI_DRV_NAME "GOOG0003"

static BLOCKING_NOTIFIER_HEAD(cros_usbpd_notifier_list);

/**
 * cros_usbpd_register_notify - Register a notifier callback for PD events.
 * @nb: Notifier block pointer to register
 *
 * On ACPI platforms this corresponds to host events on the ECPD
 * "GOOG0003" ACPI device. On non-ACPI platforms this will filter mkbp events
 * for USB PD events.
 *
 * Return: 0 on success or negative error code.
 */
int cros_usbpd_register_notify(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&cros_usbpd_notifier_list,
						nb);
}
EXPORT_SYMBOL_GPL(cros_usbpd_register_notify);

/**
 * cros_usbpd_unregister_notify - Unregister notifier callback for PD events.
 * @nb: Notifier block pointer to unregister
 *
 * Unregister a notifier callback that was previously registered with
 * cros_usbpd_register_notify().
 */
void cros_usbpd_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&cros_usbpd_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(cros_usbpd_unregister_notify);

#ifdef CONFIG_ACPI

static int cros_usbpd_notify_add_acpi(struct acpi_device *adev)
{
	return 0;
}

static void cros_usbpd_notify_acpi(struct acpi_device *adev, u32 event)
{
	blocking_notifier_call_chain(&cros_usbpd_notifier_list, event, NULL);
}

static const struct acpi_device_id cros_usbpd_notify_acpi_device_ids[] = {
	{ ACPI_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_usbpd_notify_acpi_device_ids);

static struct acpi_driver cros_usbpd_notify_acpi_driver = {
	.name = DRV_NAME,
	.class = DRV_NAME,
	.ids = cros_usbpd_notify_acpi_device_ids,
	.ops = {
		.add = cros_usbpd_notify_add_acpi,
		.notify = cros_usbpd_notify_acpi,
	},
};

#endif /* CONFIG_ACPI */

static int cros_usbpd_notify_plat(struct notifier_block *nb,
				  unsigned long queued_during_suspend,
				  void *data)
{
	struct cros_ec_device *ec_dev = (struct cros_ec_device *)data;
	u32 host_event = cros_ec_get_host_event(ec_dev);

	if (!host_event)
		return NOTIFY_BAD;

	if (host_event & EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU)) {
		blocking_notifier_call_chain(&cros_usbpd_notifier_list,
					     host_event, NULL);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static int cros_usbpd_notify_probe_plat(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ecdev = dev_get_drvdata(dev->parent);
	struct notifier_block *nb;
	int ret;

	nb = devm_kzalloc(dev, sizeof(*nb), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	nb->notifier_call = cros_usbpd_notify_plat;
	dev_set_drvdata(dev, nb);

	ret = blocking_notifier_chain_register(&ecdev->ec_dev->event_notifier,
					       nb);
	if (ret < 0) {
		dev_err(dev, "Failed to register notifier\n");
		return ret;
	}

	return 0;
}

static int cros_usbpd_notify_remove_plat(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ecdev = dev_get_drvdata(dev->parent);
	struct notifier_block *nb =
		(struct notifier_block *)dev_get_drvdata(dev);

	blocking_notifier_chain_unregister(&ecdev->ec_dev->event_notifier, nb);

	return 0;
}

static struct platform_driver cros_usbpd_notify_plat_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_usbpd_notify_probe_plat,
	.remove = cros_usbpd_notify_remove_plat,
};

static int __init cros_usbpd_notify_init(void)
{
	int ret;

	ret = platform_driver_register(&cros_usbpd_notify_plat_driver);
	if (ret < 0)
		return ret;

#ifdef CONFIG_ACPI
	acpi_bus_register_driver(&cros_usbpd_notify_acpi_driver);
#endif
	return 0;
}

static void __exit cros_usbpd_notify_exit(void)
{
#ifdef CONFIG_ACPI
	acpi_bus_unregister_driver(&cros_usbpd_notify_acpi_driver);
#endif
	platform_driver_unregister(&cros_usbpd_notify_plat_driver);
}

module_init(cros_usbpd_notify_init);
module_exit(cros_usbpd_notify_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS power delivery notifier device");
MODULE_AUTHOR("Jon Flatley <jflat@chromium.org>");
MODULE_ALIAS("platform:" DRV_NAME);
