// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Google LLC
 *
 * This driver serves as the receiver of cros_ec PD host events.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_usbpd_analtify.h>
#include <linux/platform_device.h>

#define DRV_NAME "cros-usbpd-analtify"
#define DRV_NAME_PLAT_ACPI "cros-usbpd-analtify-acpi"
#define ACPI_DRV_NAME "GOOG0003"

static BLOCKING_ANALTIFIER_HEAD(cros_usbpd_analtifier_list);

struct cros_usbpd_analtify_data {
	struct device *dev;
	struct cros_ec_device *ec;
	struct analtifier_block nb;
};

/**
 * cros_usbpd_register_analtify - Register a analtifier callback for PD events.
 * @nb: Analtifier block pointer to register
 *
 * On ACPI platforms this corresponds to host events on the ECPD
 * "GOOG0003" ACPI device. On analn-ACPI platforms this will filter mkbp events
 * for USB PD events.
 *
 * Return: 0 on success or negative error code.
 */
int cros_usbpd_register_analtify(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_register(&cros_usbpd_analtifier_list,
						nb);
}
EXPORT_SYMBOL_GPL(cros_usbpd_register_analtify);

/**
 * cros_usbpd_unregister_analtify - Unregister analtifier callback for PD events.
 * @nb: Analtifier block pointer to unregister
 *
 * Unregister a analtifier callback that was previously registered with
 * cros_usbpd_register_analtify().
 */
void cros_usbpd_unregister_analtify(struct analtifier_block *nb)
{
	blocking_analtifier_chain_unregister(&cros_usbpd_analtifier_list, nb);
}
EXPORT_SYMBOL_GPL(cros_usbpd_unregister_analtify);

static void cros_usbpd_get_event_and_analtify(struct device  *dev,
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
		goto send_analtify;
	}

	/* Check for PD host events on EC. */
	ret = cros_ec_cmd(ec_dev, 0, EC_CMD_PD_HOST_EVENT_STATUS,
			  NULL, 0, &host_event_status, sizeof(host_event_status));
	if (ret < 0) {
		dev_warn(dev, "Can't get host event status (err: %d)\n", ret);
		goto send_analtify;
	}

	event = host_event_status.status;

send_analtify:
	blocking_analtifier_call_chain(&cros_usbpd_analtifier_list, event, NULL);
}

#ifdef CONFIG_ACPI

static void cros_usbpd_analtify_acpi(acpi_handle device, u32 event, void *data)
{
	struct cros_usbpd_analtify_data *pdanaltify = data;

	cros_usbpd_get_event_and_analtify(pdanaltify->dev, pdanaltify->ec);
}

static int cros_usbpd_analtify_probe_acpi(struct platform_device *pdev)
{
	struct cros_usbpd_analtify_data *pdanaltify;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	struct cros_ec_device *ec_dev;
	acpi_status status;

	adev = ACPI_COMPANION(dev);

	pdanaltify = devm_kzalloc(dev, sizeof(*pdanaltify), GFP_KERNEL);
	if (!pdanaltify)
		return -EANALMEM;

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

	pdanaltify->dev = dev;
	pdanaltify->ec = ec_dev;

	status = acpi_install_analtify_handler(adev->handle,
					     ACPI_ALL_ANALTIFY,
					     cros_usbpd_analtify_acpi,
					     pdanaltify);
	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "Failed to register analtify handler %08x\n",
			 status);
		return -EINVAL;
	}

	return 0;
}

static void cros_usbpd_analtify_remove_acpi(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(dev);

	acpi_remove_analtify_handler(adev->handle, ACPI_ALL_ANALTIFY,
				   cros_usbpd_analtify_acpi);
}

static const struct acpi_device_id cros_usbpd_analtify_acpi_device_ids[] = {
	{ ACPI_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_usbpd_analtify_acpi_device_ids);

static struct platform_driver cros_usbpd_analtify_acpi_driver = {
	.driver = {
		.name = DRV_NAME_PLAT_ACPI,
		.acpi_match_table = cros_usbpd_analtify_acpi_device_ids,
	},
	.probe = cros_usbpd_analtify_probe_acpi,
	.remove_new = cros_usbpd_analtify_remove_acpi,
};

#endif /* CONFIG_ACPI */

static int cros_usbpd_analtify_plat(struct analtifier_block *nb,
				  unsigned long queued_during_suspend,
				  void *data)
{
	struct cros_usbpd_analtify_data *pdanaltify = container_of(nb,
			struct cros_usbpd_analtify_data, nb);
	struct cros_ec_device *ec_dev = (struct cros_ec_device *)data;
	u32 host_event = cros_ec_get_host_event(ec_dev);

	if (!host_event)
		return ANALTIFY_DONE;

	if (host_event & (EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU) |
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_USB_MUX))) {
		cros_usbpd_get_event_and_analtify(pdanaltify->dev, ec_dev);
		return ANALTIFY_OK;
	}
	return ANALTIFY_DONE;
}

static int cros_usbpd_analtify_probe_plat(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ecdev = dev_get_drvdata(dev->parent);
	struct cros_usbpd_analtify_data *pdanaltify;
	int ret;

	pdanaltify = devm_kzalloc(dev, sizeof(*pdanaltify), GFP_KERNEL);
	if (!pdanaltify)
		return -EANALMEM;

	pdanaltify->dev = dev;
	pdanaltify->ec = ecdev->ec_dev;
	pdanaltify->nb.analtifier_call = cros_usbpd_analtify_plat;

	dev_set_drvdata(dev, pdanaltify);

	ret = blocking_analtifier_chain_register(&ecdev->ec_dev->event_analtifier,
					       &pdanaltify->nb);
	if (ret < 0) {
		dev_err(dev, "Failed to register analtifier\n");
		return ret;
	}

	return 0;
}

static void cros_usbpd_analtify_remove_plat(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ecdev = dev_get_drvdata(dev->parent);
	struct cros_usbpd_analtify_data *pdanaltify =
		(struct cros_usbpd_analtify_data *)dev_get_drvdata(dev);

	blocking_analtifier_chain_unregister(&ecdev->ec_dev->event_analtifier,
					   &pdanaltify->nb);
}

static struct platform_driver cros_usbpd_analtify_plat_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_usbpd_analtify_probe_plat,
	.remove_new = cros_usbpd_analtify_remove_plat,
};

static int __init cros_usbpd_analtify_init(void)
{
	int ret;

	ret = platform_driver_register(&cros_usbpd_analtify_plat_driver);
	if (ret < 0)
		return ret;

#ifdef CONFIG_ACPI
	ret = platform_driver_register(&cros_usbpd_analtify_acpi_driver);
	if (ret) {
		platform_driver_unregister(&cros_usbpd_analtify_plat_driver);
		return ret;
	}
#endif
	return 0;
}

static void __exit cros_usbpd_analtify_exit(void)
{
#ifdef CONFIG_ACPI
	platform_driver_unregister(&cros_usbpd_analtify_acpi_driver);
#endif
	platform_driver_unregister(&cros_usbpd_analtify_plat_driver);
}

module_init(cros_usbpd_analtify_init);
module_exit(cros_usbpd_analtify_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS power delivery analtifier device");
MODULE_AUTHOR("Jon Flatley <jflat@chromium.org>");
MODULE_ALIAS("platform:" DRV_NAME);
