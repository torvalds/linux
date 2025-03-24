// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI ACPI driver
 *
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include "ucsi.h"

#define UCSI_DSM_UUID		"6f8398c2-7ca4-11e4-ad36-631042b5008f"
#define UCSI_DSM_FUNC_WRITE	1
#define UCSI_DSM_FUNC_READ	2

struct ucsi_acpi {
	struct device *dev;
	struct ucsi *ucsi;
	void *base;
	bool check_bogus_event;
	guid_t guid;
	u64 cmd;
};

static int ucsi_acpi_dsm(struct ucsi_acpi *ua, int func)
{
	union acpi_object *obj;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(ua->dev), &ua->guid, 1, func,
				NULL);
	if (!obj) {
		dev_err(ua->dev, "%s: failed to evaluate _DSM %d\n",
			__func__, func);
		return -EIO;
	}

	ACPI_FREE(obj);
	return 0;
}

static int ucsi_acpi_read_version(struct ucsi *ucsi, u16 *version)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	int ret;

	ret = ucsi_acpi_dsm(ua, UCSI_DSM_FUNC_READ);
	if (ret)
		return ret;

	memcpy(version, ua->base + UCSI_VERSION, sizeof(*version));

	return 0;
}

static int ucsi_acpi_read_cci(struct ucsi *ucsi, u32 *cci)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);

	memcpy(cci, ua->base + UCSI_CCI, sizeof(*cci));

	return 0;
}

static int ucsi_acpi_poll_cci(struct ucsi *ucsi, u32 *cci)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	int ret;

	ret = ucsi_acpi_dsm(ua, UCSI_DSM_FUNC_READ);
	if (ret)
		return ret;

	return ucsi_acpi_read_cci(ucsi, cci);
}

static int ucsi_acpi_read_message_in(struct ucsi *ucsi, void *val, size_t val_len)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);

	memcpy(val, ua->base + UCSI_MESSAGE_IN, val_len);

	return 0;
}

static int ucsi_acpi_async_control(struct ucsi *ucsi, u64 command)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);

	memcpy(ua->base + UCSI_CONTROL, &command, sizeof(command));
	ua->cmd = command;

	return ucsi_acpi_dsm(ua, UCSI_DSM_FUNC_WRITE);
}

static const struct ucsi_operations ucsi_acpi_ops = {
	.read_version = ucsi_acpi_read_version,
	.read_cci = ucsi_acpi_read_cci,
	.poll_cci = ucsi_acpi_poll_cci,
	.read_message_in = ucsi_acpi_read_message_in,
	.sync_control = ucsi_sync_control_common,
	.async_control = ucsi_acpi_async_control
};

static int ucsi_gram_read_message_in(struct ucsi *ucsi, void *val, size_t val_len)
{
	u16 bogus_change = UCSI_CONSTAT_POWER_LEVEL_CHANGE |
			   UCSI_CONSTAT_PDOS_CHANGE;
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	int ret;

	ret = ucsi_acpi_read_message_in(ucsi, val, val_len);
	if (ret < 0)
		return ret;

	if (UCSI_COMMAND(ua->cmd) == UCSI_GET_CONNECTOR_STATUS &&
	    ua->check_bogus_event) {
		/* Clear the bogus change */
		if (*(u16 *)val == bogus_change)
			*(u16 *)val = 0;

		ua->check_bogus_event = false;
	}

	return ret;
}

static int ucsi_gram_sync_control(struct ucsi *ucsi, u64 command)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	int ret;

	ret = ucsi_sync_control_common(ucsi, command);
	if (ret < 0)
		return ret;

	if (UCSI_COMMAND(ua->cmd) == UCSI_GET_PDOS &&
	    ua->cmd & UCSI_GET_PDOS_PARTNER_PDO(1) &&
	    ua->cmd & UCSI_GET_PDOS_SRC_PDOS)
		ua->check_bogus_event = true;

	return ret;
}

static const struct ucsi_operations ucsi_gram_ops = {
	.read_version = ucsi_acpi_read_version,
	.read_cci = ucsi_acpi_read_cci,
	.poll_cci = ucsi_acpi_poll_cci,
	.read_message_in = ucsi_gram_read_message_in,
	.sync_control = ucsi_gram_sync_control,
	.async_control = ucsi_acpi_async_control
};

static const struct dmi_system_id ucsi_acpi_quirks[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LG Electronics"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "LG gram PC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "90Q"),
		},
		.driver_data = (void *)&ucsi_gram_ops,
	},
	{ }
};

static void ucsi_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct ucsi_acpi *ua = data;
	u32 cci;
	int ret;

	ret = ua->ucsi->ops->read_cci(ua->ucsi, &cci);
	if (ret)
		return;

	ucsi_notify_common(ua->ucsi, cci);
}

static int ucsi_acpi_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	const struct ucsi_operations *ops = &ucsi_acpi_ops;
	const struct dmi_system_id *id;
	struct ucsi_acpi *ua;
	struct resource *res;
	acpi_status status;
	int ret;

	if (adev->dep_unmet)
		return -EPROBE_DEFER;

	ua = devm_kzalloc(&pdev->dev, sizeof(*ua), GFP_KERNEL);
	if (!ua)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing memory resource\n");
		return -ENODEV;
	}

	ua->base = devm_memremap(&pdev->dev, res->start, resource_size(res), MEMREMAP_WB);
	if (IS_ERR(ua->base))
		return PTR_ERR(ua->base);

	ret = guid_parse(UCSI_DSM_UUID, &ua->guid);
	if (ret)
		return ret;

	ua->dev = &pdev->dev;

	id = dmi_first_match(ucsi_acpi_quirks);
	if (id)
		ops = id->driver_data;

	ua->ucsi = ucsi_create(&pdev->dev, ops);
	if (IS_ERR(ua->ucsi))
		return PTR_ERR(ua->ucsi);

	ucsi_set_drvdata(ua->ucsi, ua);

	status = acpi_install_notify_handler(ACPI_HANDLE(&pdev->dev),
					     ACPI_DEVICE_NOTIFY,
					     ucsi_acpi_notify, ua);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev, "failed to install notify handler\n");
		ucsi_destroy(ua->ucsi);
		return -ENODEV;
	}

	ret = ucsi_register(ua->ucsi);
	if (ret) {
		acpi_remove_notify_handler(ACPI_HANDLE(&pdev->dev),
					   ACPI_DEVICE_NOTIFY,
					   ucsi_acpi_notify);
		ucsi_destroy(ua->ucsi);
		return ret;
	}

	platform_set_drvdata(pdev, ua);

	return 0;
}

static void ucsi_acpi_remove(struct platform_device *pdev)
{
	struct ucsi_acpi *ua = platform_get_drvdata(pdev);

	ucsi_unregister(ua->ucsi);
	ucsi_destroy(ua->ucsi);

	acpi_remove_notify_handler(ACPI_HANDLE(&pdev->dev), ACPI_DEVICE_NOTIFY,
				   ucsi_acpi_notify);
}

static int ucsi_acpi_resume(struct device *dev)
{
	struct ucsi_acpi *ua = dev_get_drvdata(dev);

	return ucsi_resume(ua->ucsi);
}

static DEFINE_SIMPLE_DEV_PM_OPS(ucsi_acpi_pm_ops, NULL, ucsi_acpi_resume);

static const struct acpi_device_id ucsi_acpi_match[] = {
	{ "PNP0CA0", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ucsi_acpi_match);

static struct platform_driver ucsi_acpi_platform_driver = {
	.driver = {
		.name = "ucsi_acpi",
		.pm = pm_ptr(&ucsi_acpi_pm_ops),
		.acpi_match_table = ACPI_PTR(ucsi_acpi_match),
	},
	.probe = ucsi_acpi_probe,
	.remove = ucsi_acpi_remove,
};

module_platform_driver(ucsi_acpi_platform_driver);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("UCSI ACPI driver");
