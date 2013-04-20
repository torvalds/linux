/*
 * Support for Core System Resources Table (CSRT)
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "ACPI: CSRT: " fmt

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

ACPI_MODULE_NAME("CSRT");

static int __init acpi_csrt_parse_shared_info(struct platform_device *pdev,
					      const struct acpi_csrt_group *grp)
{
	const struct acpi_csrt_shared_info *si;
	struct resource res[3];
	size_t nres;
	int ret;

	memset(res, 0, sizeof(res));
	nres = 0;

	si = (const struct acpi_csrt_shared_info *)&grp[1];
	/*
	 * The peripherals that are listed on CSRT typically support only
	 * 32-bit addresses so we only use the low part of MMIO base for
	 * now.
	 */
	if (!si->mmio_base_high && si->mmio_base_low) {
		/*
		 * There is no size of the memory resource in shared_info
		 * so we assume that it is 4k here.
		 */
		res[nres].start = si->mmio_base_low;
		res[nres].end = res[0].start + SZ_4K - 1;
		res[nres++].flags = IORESOURCE_MEM;
	}

	if (si->gsi_interrupt) {
		int irq = acpi_register_gsi(NULL, si->gsi_interrupt,
					    si->interrupt_mode,
					    si->interrupt_polarity);
		res[nres].start = irq;
		res[nres].end = irq;
		res[nres++].flags = IORESOURCE_IRQ;
	}

	if (si->base_request_line || si->num_handshake_signals) {
		/*
		 * We pass the driver a DMA resource describing the range
		 * of request lines the device supports.
		 */
		res[nres].start = si->base_request_line;
		res[nres].end = res[nres].start + si->num_handshake_signals - 1;
		res[nres++].flags = IORESOURCE_DMA;
	}

	ret = platform_device_add_resources(pdev, res, nres);
	if (ret) {
		if (si->gsi_interrupt)
			acpi_unregister_gsi(si->gsi_interrupt);
		return ret;
	}

	return 0;
}

static int __init
acpi_csrt_parse_resource_group(const struct acpi_csrt_group *grp)
{
	struct platform_device *pdev;
	char vendor[5], name[16];
	int ret, i;

	vendor[0] = grp->vendor_id;
	vendor[1] = grp->vendor_id >> 8;
	vendor[2] = grp->vendor_id >> 16;
	vendor[3] = grp->vendor_id >> 24;
	vendor[4] = '\0';

	if (grp->shared_info_length != sizeof(struct acpi_csrt_shared_info))
		return -ENODEV;

	snprintf(name, sizeof(name), "%s%04X", vendor, grp->device_id);
	pdev = platform_device_alloc(name, PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	/* Add resources based on the shared info */
	ret = acpi_csrt_parse_shared_info(pdev, grp);
	if (ret)
		goto fail;

	ret = platform_device_add(pdev);
	if (ret)
		goto fail;

	for (i = 0; i < pdev->num_resources; i++)
		dev_dbg(&pdev->dev, "%pR\n", &pdev->resource[i]);

	return 0;

fail:
	platform_device_put(pdev);
	return ret;
}

/*
 * CSRT or Core System Resources Table is a proprietary ACPI table
 * introduced by Microsoft. This table can contain devices that are not in
 * the system DSDT table. In particular DMA controllers might be described
 * here.
 *
 * We present these devices as normal platform devices that don't have ACPI
 * IDs or handle. The platform device name will be something like
 * <VENDOR><DEVID>.<n>.auto for example: INTL9C06.0.auto.
 */
void __init acpi_csrt_init(void)
{
	struct acpi_csrt_group *grp, *end;
	struct acpi_table_csrt *csrt;
	acpi_status status;
	int ret;

	status = acpi_get_table(ACPI_SIG_CSRT, 0,
				(struct acpi_table_header **)&csrt);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			pr_warn("failed to get the CSRT table\n");
		return;
	}

	pr_debug("parsing CSRT table for devices\n");

	grp = (struct acpi_csrt_group *)(csrt + 1);
	end = (struct acpi_csrt_group *)((void *)csrt + csrt->header.length);

	while (grp < end) {
		ret = acpi_csrt_parse_resource_group(grp);
		if (ret) {
			pr_warn("error in parsing resource group: %d\n", ret);
			return;
		}

		grp = (struct acpi_csrt_group *)((void *)grp + grp->length);
	}
}
