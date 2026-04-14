// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI support for CMOS RTC Address Space access
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Lan Tianyu <tianyu.lan@intel.com>
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mc146818rtc.h>

#include "../internal.h"

static const struct acpi_device_id acpi_cmos_rtc_ids[] = {
	{ "ACPI000E", 1 }, /* ACPI Time and Alarm Device (TAD) */
	ACPI_CMOS_RTC_IDS
};

bool cmos_rtc_platform_device_present;

static acpi_status acpi_cmos_rtc_space_handler(u32 function,
					       acpi_physical_address address,
					       u32 bits, u64 *value64,
					       void *handler_context,
					       void *region_context)
{
	unsigned int i, bytes = DIV_ROUND_UP(bits, 8);
	u8 *value = (u8 *)value64;

	if (address > 0xff || !value64)
		return AE_BAD_PARAMETER;

	guard(spinlock_irq)(&rtc_lock);

	if (function == ACPI_WRITE) {
		for (i = 0; i < bytes; i++, address++, value++)
			CMOS_WRITE(*value, address);

		return AE_OK;
	}

	if (function == ACPI_READ) {
		for (i = 0; i < bytes; i++, address++, value++)
			*value = CMOS_READ(address);

		return AE_OK;
	}

	return AE_BAD_PARAMETER;
}

static int acpi_install_cmos_rtc_space_handler(acpi_handle handle)
{
	static bool cmos_rtc_space_handler_present __read_mostly;
	acpi_status status;

	if (cmos_rtc_space_handler_present)
		return 0;

	status = acpi_install_address_space_handler(handle,
						    ACPI_ADR_SPACE_CMOS,
						    acpi_cmos_rtc_space_handler,
						    NULL, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("Failed to install CMOS-RTC address space handler\n");
		return -ENODEV;
	}

	cmos_rtc_space_handler_present = true;

	return 1;
}

static int acpi_cmos_rtc_attach(struct acpi_device *adev,
				const struct acpi_device_id *id)
{
	int ret;

	ret = acpi_install_cmos_rtc_space_handler(adev->handle);
	if (ret < 0)
		return ret;

	if (IS_ERR_OR_NULL(acpi_create_platform_device(adev, NULL))) {
		pr_err("Failed to create a platform device for %s\n", (char *)id->id);
		return 0;
	} else if (!id->driver_data) {
		cmos_rtc_platform_device_present = true;
	}
	return 1;
}

static struct acpi_scan_handler cmos_rtc_handler = {
	.ids = acpi_cmos_rtc_ids,
	.attach = acpi_cmos_rtc_attach,
};

void __init acpi_cmos_rtc_init(void)
{
	acpi_scan_add_handler(&cmos_rtc_handler);
}
