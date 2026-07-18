// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  dell-dw5826e-reset.c - Dell DW5826e reset driver
 *
 *  Copyright (C) 2026 Jackbb Wu <jackbb.wu@compal.com>
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uuid.h>

#define PALC_DSM_FN_TRIGGER_PLDR    BIT(1)

static guid_t palc_dsm_guid =
	GUID_INIT(0x5a1a4bba, 0x8006, 0x487e, 0xbe, 0x0a, 0xac, 0xf5, 0xd8, 0xfd, 0xfe, 0x59);

static int trigger_palc_pldr(struct device *dev, acpi_handle handle)
{
	union acpi_object *obj;
	int ret = 0;

	obj = acpi_evaluate_dsm(handle, &palc_dsm_guid, 1, PALC_DSM_FN_TRIGGER_PLDR, NULL);
	if (!obj) {
		dev_err(dev, "Failed to evaluate _DSM\n");
		return -EIO;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(dev, "Unexpected _DSM return type: %d\n", obj->type);
		ret = -EINVAL;
	}

	ACPI_FREE(obj);
	return ret;
}

static ssize_t wwan_reset_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	int ret;

	ret = trigger_palc_pldr(dev, handle);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(wwan_reset);

static struct attribute *palc_attrs[] = {
	&dev_attr_wwan_reset.attr,
	NULL
};
ATTRIBUTE_GROUPS(palc);

static int palc_probe(struct platform_device *pdev)
{
	acpi_handle handle;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	if (!acpi_check_dsm(handle, &palc_dsm_guid, 1, PALC_DSM_FN_TRIGGER_PLDR))
		return -ENODEV;

	return 0;
}

static const struct acpi_device_id palc_acpi_ids[] = {
	{ "PALC0001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, palc_acpi_ids);

static struct platform_driver palc_driver = {
	.driver = {
		.name = "dell-dw5826e-reset",
		.acpi_match_table = palc_acpi_ids,
		.dev_groups = palc_groups,
	},
	.probe  = palc_probe,
};
module_platform_driver(palc_driver);

MODULE_DESCRIPTION("Dell DW5826e reset driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("JackBB Wu");
