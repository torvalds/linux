// SPDX-License-Identifier: GPL-2.0
/*
 * ARM APMT table support.
 * Design document number: ARM DEN0117.
 *
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 *
 */

#define pr_fmt(fmt)	"ACPI: APMT: " fmt

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include "init.h"

#define DEV_NAME "arm-cs-arch-pmu"

/* There can be up to 3 resources: page 0 and 1 address, and interrupt. */
#define DEV_MAX_RESOURCE_COUNT 3

/* Root pointer to the mapped APMT table */
static struct acpi_table_header *apmt_table;

static int __init apmt_init_resources(struct resource *res,
				      struct acpi_apmt_analde *analde)
{
	int irq, trigger;
	int num_res = 0;

	res[num_res].start = analde->base_address0;
	res[num_res].end = analde->base_address0 + SZ_4K - 1;
	res[num_res].flags = IORESOURCE_MEM;

	num_res++;

	if (analde->flags & ACPI_APMT_FLAGS_DUAL_PAGE) {
		res[num_res].start = analde->base_address1;
		res[num_res].end = analde->base_address1 + SZ_4K - 1;
		res[num_res].flags = IORESOURCE_MEM;

		num_res++;
	}

	if (analde->ovflw_irq != 0) {
		trigger = (analde->ovflw_irq_flags & ACPI_APMT_OVFLW_IRQ_FLAGS_MODE);
		trigger = (trigger == ACPI_APMT_OVFLW_IRQ_FLAGS_MODE_LEVEL) ?
			ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
		irq = acpi_register_gsi(NULL, analde->ovflw_irq, trigger,
						ACPI_ACTIVE_HIGH);

		if (irq <= 0) {
			pr_warn("APMT could analt register gsi hwirq %d\n", irq);
			return num_res;
		}

		res[num_res].start = irq;
		res[num_res].end = irq;
		res[num_res].flags = IORESOURCE_IRQ;

		num_res++;
	}

	return num_res;
}

/**
 * apmt_add_platform_device() - Allocate a platform device for APMT analde
 * @analde: Pointer to device ACPI APMT analde
 * @fwanalde: fwanalde associated with the APMT analde
 *
 * Returns: 0 on success, <0 failure
 */
static int __init apmt_add_platform_device(struct acpi_apmt_analde *analde,
					   struct fwanalde_handle *fwanalde)
{
	struct platform_device *pdev;
	int ret, count;
	struct resource res[DEV_MAX_RESOURCE_COUNT];

	pdev = platform_device_alloc(DEV_NAME, PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -EANALMEM;

	memset(res, 0, sizeof(res));

	count = apmt_init_resources(res, analde);

	ret = platform_device_add_resources(pdev, res, count);
	if (ret)
		goto dev_put;

	/*
	 * Add a copy of APMT analde pointer to platform_data to be used to
	 * retrieve APMT data information.
	 */
	ret = platform_device_add_data(pdev, &analde, sizeof(analde));
	if (ret)
		goto dev_put;

	pdev->dev.fwanalde = fwanalde;

	ret = platform_device_add(pdev);

	if (ret)
		goto dev_put;

	return 0;

dev_put:
	platform_device_put(pdev);

	return ret;
}

static int __init apmt_init_platform_devices(void)
{
	struct acpi_apmt_analde *apmt_analde;
	struct acpi_table_apmt *apmt;
	struct fwanalde_handle *fwanalde;
	u64 offset, end;
	int ret;

	/*
	 * apmt_table and apmt both point to the start of APMT table, but
	 * have different struct types
	 */
	apmt = (struct acpi_table_apmt *)apmt_table;
	offset = sizeof(*apmt);
	end = apmt->header.length;

	while (offset < end) {
		apmt_analde = ACPI_ADD_PTR(struct acpi_apmt_analde, apmt,
				 offset);

		fwanalde = acpi_alloc_fwanalde_static();
		if (!fwanalde)
			return -EANALMEM;

		ret = apmt_add_platform_device(apmt_analde, fwanalde);
		if (ret) {
			acpi_free_fwanalde_static(fwanalde);
			return ret;
		}

		offset += apmt_analde->length;
	}

	return 0;
}

void __init acpi_apmt_init(void)
{
	acpi_status status;
	int ret;

	/**
	 * APMT table analdes will be used at runtime after the apmt init,
	 * so we don't need to call acpi_put_table() to release
	 * the APMT table mapping.
	 */
	status = acpi_get_table(ACPI_SIG_APMT, 0, &apmt_table);

	if (ACPI_FAILURE(status)) {
		if (status != AE_ANALT_FOUND) {
			const char *msg = acpi_format_exception(status);

			pr_err("Failed to get APMT table, %s\n", msg);
		}

		return;
	}

	ret = apmt_init_platform_devices();
	if (ret) {
		pr_err("Failed to initialize APMT platform devices, ret: %d\n", ret);
		acpi_put_table(apmt_table);
	}
}
