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
	{ "PNP0B00" },
	{ "PNP0B01" },
	{ "PNP0B02" },
	{}
};

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

int acpi_install_cmos_rtc_space_handler(acpi_handle handle)
{
	acpi_status status;

	status = acpi_install_address_space_handler(handle,
						    ACPI_ADR_SPACE_CMOS,
						    acpi_cmos_rtc_space_handler,
						    NULL, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("Failed to install CMOS-RTC address space handler\n");
		return -ENODEV;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(acpi_install_cmos_rtc_space_handler);

void acpi_remove_cmos_rtc_space_handler(acpi_handle handle)
{
	acpi_status status;

	status = acpi_remove_address_space_handler(handle,
						   ACPI_ADR_SPACE_CMOS,
						   acpi_cmos_rtc_space_handler);
	if (ACPI_FAILURE(status))
		pr_err("Failed to remove CMOS-RTC address space handler\n");
}
EXPORT_SYMBOL_GPL(acpi_remove_cmos_rtc_space_handler);

static int acpi_cmos_rtc_attach(struct acpi_device *adev,
				const struct acpi_device_id *id)
{
	return acpi_install_cmos_rtc_space_handler(adev->handle);
}

static struct acpi_scan_handler cmos_rtc_handler = {
	.ids = acpi_cmos_rtc_ids,
	.attach = acpi_cmos_rtc_attach,
};

void __init acpi_cmos_rtc_init(void)
{
	acpi_scan_add_handler(&cmos_rtc_handler);
}
