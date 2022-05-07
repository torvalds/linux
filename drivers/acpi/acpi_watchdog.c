// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI watchdog table parsing support.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#define pr_fmt(fmt) "ACPI: watchdog: " fmt

#include <linux/acpi.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include "internal.h"

#ifdef CONFIG_RTC_MC146818_LIB
#include <linux/mc146818rtc.h>

/*
 * There are several systems where the WDAT table is accessing RTC SRAM to
 * store persistent information. This does not work well with the Linux RTC
 * driver so on those systems we skip WDAT driver and prefer iTCO_wdt
 * instead.
 *
 * See also https://bugzilla.kernel.org/show_bug.cgi?id=199033.
 */
static bool acpi_watchdog_uses_rtc(const struct acpi_table_wdat *wdat)
{
	const struct acpi_wdat_entry *entries;
	int i;

	entries = (struct acpi_wdat_entry *)(wdat + 1);
	for (i = 0; i < wdat->entries; i++) {
		const struct acpi_generic_address *gas;

		gas = &entries[i].register_region;
		if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
			switch (gas->address) {
			case RTC_PORT(0):
			case RTC_PORT(1):
			case RTC_PORT(2):
			case RTC_PORT(3):
				return true;
			}
		}
	}

	return false;
}
#else
static bool acpi_watchdog_uses_rtc(const struct acpi_table_wdat *wdat)
{
	return false;
}
#endif

static bool acpi_no_watchdog;

static const struct acpi_table_wdat *acpi_watchdog_get_wdat(void)
{
	const struct acpi_table_wdat *wdat = NULL;
	acpi_status status;

	if (acpi_disabled || acpi_no_watchdog)
		return NULL;

	status = acpi_get_table(ACPI_SIG_WDAT, 0,
				(struct acpi_table_header **)&wdat);
	if (ACPI_FAILURE(status)) {
		/* It is fine if there is no WDAT */
		return NULL;
	}

	if (acpi_watchdog_uses_rtc(wdat)) {
		acpi_put_table((struct acpi_table_header *)wdat);
		pr_info("Skipping WDAT on this system because it uses RTC SRAM\n");
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

/* ACPI watchdog can be disabled on boot command line */
static int __init disable_acpi_watchdog(char *str)
{
	acpi_no_watchdog = true;
	return 1;
}
__setup("acpi_no_watchdog", disable_acpi_watchdog);

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
		goto fail_put_wdat;

	/* Skip legacy PCI WDT devices */
	if (wdat->pci_segment != 0xff || wdat->pci_bus != 0xff ||
	    wdat->pci_device != 0xff || wdat->pci_function != 0xff)
		goto fail_put_wdat;

	INIT_LIST_HEAD(&resource_list);

	entries = (struct acpi_wdat_entry *)(wdat + 1);
	for (i = 0; i < wdat->entries; i++) {
		const struct acpi_generic_address *gas;
		struct resource_entry *rentry;
		struct resource res = {};
		bool found;

		gas = &entries[i].register_region;

		res.start = gas->address;
		res.end = res.start + ACPI_ACCESS_BYTE_WIDTH(gas->access_width) - 1;
		if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			res.flags = IORESOURCE_MEM;
		} else if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
			res.flags = IORESOURCE_IO;
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
fail_put_wdat:
	acpi_put_table((struct acpi_table_header *)wdat);
}
