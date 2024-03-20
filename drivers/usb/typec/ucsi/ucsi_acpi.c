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
	struct completion complete;
	unsigned long flags;
#define UCSI_ACPI_SUPPRESS_EVENT	0
#define UCSI_ACPI_COMMAND_PENDING	1
#define UCSI_ACPI_ACK_PENDING		2
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

static int ucsi_acpi_read(struct ucsi *ucsi, unsigned int offset,
			  void *val, size_t val_len)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	int ret;

	ret = ucsi_acpi_dsm(ua, UCSI_DSM_FUNC_READ);
	if (ret)
		return ret;

	memcpy(val, ua->base + offset, val_len);

	return 0;
}

static int ucsi_acpi_async_write(struct ucsi *ucsi, unsigned int offset,
				 const void *val, size_t val_len)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);

	memcpy(ua->base + offset, val, val_len);
	ua->cmd = *(u64 *)val;

	return ucsi_acpi_dsm(ua, UCSI_DSM_FUNC_WRITE);
}

static int ucsi_acpi_sync_write(struct ucsi *ucsi, unsigned int offset,
				const void *val, size_t val_len)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	bool ack = UCSI_COMMAND(*(u64 *)val) == UCSI_ACK_CC_CI;
	int ret;

	if (ack)
		set_bit(UCSI_ACPI_ACK_PENDING, &ua->flags);
	else
		set_bit(UCSI_ACPI_COMMAND_PENDING, &ua->flags);

	ret = ucsi_acpi_async_write(ucsi, offset, val, val_len);
	if (ret)
		goto out_clear_bit;

	if (!wait_for_completion_timeout(&ua->complete, 5 * HZ))
		ret = -ETIMEDOUT;

out_clear_bit:
	if (ack)
		clear_bit(UCSI_ACPI_ACK_PENDING, &ua->flags);
	else
		clear_bit(UCSI_ACPI_COMMAND_PENDING, &ua->flags);

	return ret;
}

static const struct ucsi_operations ucsi_acpi_ops = {
	.read = ucsi_acpi_read,
	.sync_write = ucsi_acpi_sync_write,
	.async_write = ucsi_acpi_async_write
};

static int
ucsi_zenbook_read(struct ucsi *ucsi, unsigned int offset, void *val, size_t val_len)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	int ret;

	if (offset == UCSI_VERSION || UCSI_COMMAND(ua->cmd) == UCSI_PPM_RESET) {
		ret = ucsi_acpi_dsm(ua, UCSI_DSM_FUNC_READ);
		if (ret)
			return ret;
	}

	memcpy(val, ua->base + offset, val_len);

	return 0;
}

static const struct ucsi_operations ucsi_zenbook_ops = {
	.read = ucsi_zenbook_read,
	.sync_write = ucsi_acpi_sync_write,
	.async_write = ucsi_acpi_async_write
};

/*
 * Some Dell laptops don't like ACK commands with the
 * UCSI_ACK_CONNECTOR_CHANGE but not the UCSI_ACK_COMMAND_COMPLETE
 * bit set. To work around this send a dummy command and bundle the
 * UCSI_ACK_CONNECTOR_CHANGE with the UCSI_ACK_COMMAND_COMPLETE
 * for the dummy command.
 */
static int
ucsi_dell_sync_write(struct ucsi *ucsi, unsigned int offset,
		     const void *val, size_t val_len)
{
	struct ucsi_acpi *ua = ucsi_get_drvdata(ucsi);
	u64 cmd = *(u64 *)val;
	u64 dummycmd = UCSI_GET_CAPABILITY;
	int ret;

	if (cmd == (UCSI_ACK_CC_CI | UCSI_ACK_CONNECTOR_CHANGE)) {
		cmd |= UCSI_ACK_COMMAND_COMPLETE;

		/*
		 * The UCSI core thinks it is sending a connector change ack
		 * and will accept new connector change events. We don't want
		 * this to happen for the dummy command as its response will
		 * still report the very event that the core is trying to clear.
		 */
		set_bit(UCSI_ACPI_SUPPRESS_EVENT, &ua->flags);
		ret = ucsi_acpi_sync_write(ucsi, UCSI_CONTROL, &dummycmd,
					   sizeof(dummycmd));
		clear_bit(UCSI_ACPI_SUPPRESS_EVENT, &ua->flags);

		if (ret < 0)
			return ret;
	}

	return ucsi_acpi_sync_write(ucsi, UCSI_CONTROL, &cmd, sizeof(cmd));
}

static const struct ucsi_operations ucsi_dell_ops = {
	.read = ucsi_acpi_read,
	.sync_write = ucsi_dell_sync_write,
	.async_write = ucsi_acpi_async_write
};

static const struct dmi_system_id ucsi_acpi_quirks[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZenBook UX325UA_UM325UA"),
		},
		.driver_data = (void *)&ucsi_zenbook_ops,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
		.driver_data = (void *)&ucsi_dell_ops,
	},
	{ }
};

static void ucsi_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct ucsi_acpi *ua = data;
	u32 cci;
	int ret;

	ret = ua->ucsi->ops->read(ua->ucsi, UCSI_CCI, &cci, sizeof(cci));
	if (ret)
		return;

	if (UCSI_CCI_CONNECTOR(cci) &&
	    !test_bit(UCSI_ACPI_SUPPRESS_EVENT, &ua->flags))
		ucsi_connector_change(ua->ucsi, UCSI_CCI_CONNECTOR(cci));

	if (cci & UCSI_CCI_ACK_COMPLETE && test_bit(ACK_PENDING, &ua->flags))
		complete(&ua->complete);
	if (cci & UCSI_CCI_COMMAND_COMPLETE &&
	    test_bit(UCSI_ACPI_COMMAND_PENDING, &ua->flags))
		complete(&ua->complete);
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

	init_completion(&ua->complete);
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
	.remove_new = ucsi_acpi_remove,
};

module_platform_driver(ucsi_acpi_platform_driver);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("UCSI ACPI driver");
