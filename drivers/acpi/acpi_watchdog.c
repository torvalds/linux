/*
 * ACPI watchdog table parsing support.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "ACPI: watchdog: " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include "internal.h"

static const struct dmi_system_id acpi_watchdog_skip[] = {
	{
		/*
		 * On Lenovo Z50-70 there are two issues with the WDAT
		 * table. First some of the instructions use RTC SRAM
		 * to store persistent information. This does not work well
		 * with Linux RTC driver. Second, more important thing is
		 * that the instructions do not actually reset the system.
		 *
		 * On this particular system iTCO_wdt seems to work just
		 * fine so we prefer that over WDAT for now.
		 *
		 * See also https://bugzilla.kernel.org/show_bug.cgi?id=199033.
		 */
		.ident = "Lenovo Z50-70",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20354"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo Z50-70"),
		},
	},
	{}
};

static const struct acpi_table_wdat *acpi_watchdog_get_wdat(void)
{
	const struct acpi_table_wdat *wdat = NULL;
	acpi_status status;

	if (acpi_disabled)
		return NULL;

	if (dmi_check_system(acpi_watchdog_skip))
		return NULL;

	status = acpi_get_table(ACPI_SIG_WDAT, 0,
				(struct acpi_table_header **)&wdat);
	if (ACPI_FAILURE(status)) {
		/* It is fine if there is no WDAT */
		return NULL;
	}

	return wdat;
}

/**
 * Returns true if this system should prefer ACPI based watchdog instead of
 * the native one (which are typically the same hardware).
 */
bool acpi_has_watchdog(void)
{
	return !!acpi_watchdog_get_wdat();
}
EXPORT_SYMBOL_GPL(acpi_has_watchdog);

void __init acpi_watchdog_init(void)
{
	const struct acpi_wdat_entry *entries;
	const struct acpi_table_wdat *wdat;
	struct list_head resource_list;
	struct resource_entry *rentry;
	struct platform_device *pdev;
	struct resource *resources;
	size_t nresources = 0;
	int i;

	wdat = acpi_watchdog_get_wdat();
	if (!wdat) {
		/* It is fine if there is no WDAT */
		return;
	}

	/* Watchdog disabled by BIOS */
	if (!(wdat->flags & ACPI_WDAT_ENABLED))
		return;

	/* Skip legacy PCI WDT devices */
	if (wdat->pci_segment != 0xff || wdat->pci_bus != 0xff ||
	    wdat->pci_device != 0xff || wdat->pci_function != 0xff)
		return;

	INIT_LIST_HEAD(&resource_list);

	entries = (struct acpi_wdat_entry *)(wdat + 1);
	for (i = 0; i < wdat->entries; i++) {
		const struct acpi_generic_address *gas;
		struct resource_entry *rentry;
		struct resource res = {};
		bool found;

		gas = &entries[i].register_region;

		res.start = gas->address;
		if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			res.flags = IORESOURCE_MEM;
			res.end = res.start + ALIGN(gas->access_width, 4) - 1;
		} else if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
			res.flags = IORESOURCE_IO;
			res.end = res.start + gas->access_width - 1;
		} else {
			pr_warn("Unsupported address space: %u\n",
				gas->space_id);
			goto fail_free_resource_list;
		}

		found = false;
		resource_list_for_each_entry(rentry, &resource_list) {
			if (rentry->res->flags == res.flags &&
			    resource_overlaps(rentry->res, &res)) {
				if (res.start < rentry->res->start)
					rentry->res->start = res.start;
				if (res.end > rentry->res->end)
					rentry->res->end = res.end;
				found = true;
				break;
			}
		}

		if (!found) {
			rentry = resource_list_create_entry(NULL, 0);
			if (!rentry)
				goto fail_free_resource_list;

			*rentry->res = res;
			resource_list_add_tail(rentry, &resource_list);
			nresources++;
		}
	}

	resources = kcalloc(nresources, sizeof(*resources), GFP_KERNEL);
	if (!resources)
		goto fail_free_resource_list;

	i = 0;
	resource_list_for_each_entry(rentry, &resource_list)
		resources[i++] = *rentry->res;

	pdev = platform_device_register_simple("wdat_wdt", PLATFORM_DEVID_NONE,
					       resources, nresources);
	if (IS_ERR(pdev))
		pr_err("Device creation failed: %ld\n", PTR_ERR(pdev));

	kfree(resources);

fail_free_resource_list:
	resource_list_free(&resource_list);
}
