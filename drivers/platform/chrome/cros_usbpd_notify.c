// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Google LLC
 *
 * This driver serves as the receiver of cros_ec PD host events.
 */

#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_usbpd_notify.h>
#include <linux/platform_device.h>

#define DRV_NAME "cros-usbpd-notify"
#define DRV_NAME_PLAT_ACPI "cros-usbpd-notify-acpi"
#define ACPI_DRV_NAME "GOOG0003"

static BLOCKING_NOTIFIER_HEAD(cros_usbpd_notifier_list);

struct cros_usbpd_notify_data {
	struct device *dev;
	struct cros_ec_device *ec;
	struct notifier_block nb;
};

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

static void cros_usbpd_get_event_and_notify(struct device  *dev,
					    struct cros_ec_device *ec_dev)
{
	struct ec_response_host_event_status host_event_status;
	u32 event = 0;
	int ret;

	/*
	 * We still send a 0 event out to older devices which don't
	 * have the updated device heirarchy.
	 */
	if (!ec_dev) {
		dev_dbg(dev,
			"EC device inaccessible; sending 0 event status.\n");
		goto send_notify;
	}

	/* Check for PD host events on EC. */
	ret = cros_ec_cmd(ec_dev, 0, EC_CMD_PD_HOST_EVENT_STATUS,
			  NULL, 0, &host_event_status, sizeof(host_event_status));
	if (ret < 0) {
		dev_warn(dev, "Can't get host event status (err: %d)\n", ret);
		goto send_notify;
	}

	event = host_event_status.status;

send_notify:
	blocking_notifier_call_chain(&cros_usbpd_notifier_list, event, NULL);
}

#ifdef CONFIG_ACPI

static void cros_usbpd_notify_acpi(acpi_handle device, u32 event, void *data)
{
	struct cros_usbpd_notify_data *pdnotify = data;

	cros_usbpd_get_event_and_notify(pdnotify->dev, pdnotify->ec);
}

static int cros_usbpd_notify_probe_acpi(struct platform_device *pdev)
{
	struct cros_usbpd_notify_data *pdnotify;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	struct cros_ec_device *ec_dev;
	acpi_status status;

	adev = ACPI_COMPANION(dev);

	pdnotify = devm_kzalloc(dev, sizeof(*pdnotify), GFP_KERNEL);
	if (!pdnotify)
		return -ENOMEM;

	/* Get the EC device pointer needed to talk to the EC. */
	ec_dev = dev_get_drvdata(dev->parent);
	if (!ec_dev) {
		/*
		 * We continue even for older devices which don't have the
		 * correct device heirarchy, namely, GOOG0003 is a child
		 * of GOOG0004.
		 */
		dev_warn(dev, "Couldn't get Chrome EC device pointer.\n");
	}

	pdnotify->dev = dev;
	pdnotify->ec = ec_dev;

	status = acpi_install_notify_handler(adev->handle,
					     ACPI_ALL_NOTIFY,
					     cros_usbpd_notify_acpi,
					     pdnotify);
	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "Failed to register notify handler %08x\n",
			 status);
		return -EINVAL;
	}

	return 0;
}

static void cros_usbpd_notify_remove_acpi(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(dev);

	acpi_remove_notify_handler(adev->handle, ACPI_ALL_NOTIFY,
				   cros_usbpd_notify_acpi);
}

static const struct acpi_device_id cros_usbpd_notify_acpi_device_ids[] = {
	{ ACPI_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_usbpd_notify_acpi_device_ids);

static struct platform_driver cros_usbpd_notify_acpi_driver = {
	.driver = {
		.name = DRV_NAME_PLAT_ACPI,
		.acpi_match_table = cros_usbpd_notify_acpi_device_ids,
	},
	.probe = cros_usbpd_notify_probe_acpi,
	.remove_new = cros_usbpd_notify_remove_acpi,
};

#endif /* CONFIG_ACPI */

static int cros_usbpd_notify_plat(struct notifier_block *nb,
				  unsigned long queued_during_suspend,
				  void *data)
{
	struct cros_usbpd_notify_data *pdnotify = container_of(nb,
			struct cros_usbpd_notify_data, nb);
	struct cros_ec_device *ec_dev = (struct cros_ec_device *)data;
	u32 host_event = cros_ec_get_host_event(ec_dev);

	if (!host_event)
		return NOTIFY_DONE;

	if (host_event & (EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_USB_MUX))) {
		cros_usbpd_get_event_and_notify(pdnotify->dev, ec_dev);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static int cros_usbpd_notify_probe_plat(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ecdev = dev_get_drvdata(dev->parent);
	struct cros_usbpd_notify_data *pdnotify;
	int ret;

	pdnotify = devm_kzalloc(dev, sizeof(*pdnotify), GFP_KERNEL);
	if (!pdnotify)
		return -ENOMEM;

	pdnotify->dev = dev;
	pdnotify->ec = ecdev->ec_dev;
	pdnotify->nb.notifier_call = cros_usbpd_notify_plat;

	dev_set_drvdata(dev, pdnotify);

	ret = blocking_notifier_chain_register(&ecdev->ec_dev->event_notifier,
					       &pdnotify->nb);
	if (ret < 0) {
		dev_err(dev, "Failed to register notifier\n");
		return ret;
	}

	return 0;
}

static void cros_usbpd_notify_remove_plat(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ecdev = dev_get_drvdata(dev->parent);
	struct cros_usbpd_notify_data *pdnotify =
		(struct cros_usbpd_notify_data *)dev_get_drvdata(dev);

	blocking_notifier_chain_unregister(&ecdev->ec_dev->event_notifier,
					   &pdnotify->nb);
}

static const struct platform_device_id cros_usbpd_notify_id[] = {
	{ DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_usbpd_notify_id);

static struct platform_driver cros_usbpd_notify_plat_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_usbpd_notify_probe_plat,
	.remove_new = cros_usbpd_notify_remove_plat,
	.id_table = cros_usbpd_notify_id,
};

static int __init cros_usbpd_notify_init(void)
{
	int ret;

	ret = platform_driver_register(&cros_usbpd_notify_plat_driver);
	if (ret < 0)
		return ret;

#ifdef CONFIG_ACPI
	ret = platform_driver_register(&cros_usbpd_notify_acpi_driver);
	if (ret) {
		platform_driver_unregister(&cros_usbpd_notify_plat_driver);
		return ret;
	}
#endif
	return 0;
}

static void __exit cros_usbpd_notify_exit(void)
{
#ifdef CONFIG_ACPI
	platform_driver_unregister(&cros_usbpd_notify_acpi_driver);
#endif
	platform_driver_unregister(&cros_usbpd_notify_plat_driver);
}

module_init(cros_usbpd_notify_init);
module_exit(cros_usbpd_notify_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS power delivery notifier device");
MODULE_AUTHOR("Jon Flatley <jflat@chromium.org>");
