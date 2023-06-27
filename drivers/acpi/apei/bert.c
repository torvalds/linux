// SPDX-License-Identifier: GPL-2.0-only
/*
 * APEI Boot Error Record Table (BERT) support
 *
 * Copyright 2011 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * Under normal circumstances, when a hardware error occurs, the error
 * handler receives control and processes the error. This gives OSPM a
 * chance to process the error condition, report it, and optionally attempt
 * recovery. In some cases, the system is unable to process an error.
 * For example, system firmware or a management controller may choose to
 * reset the system or the system might experience an uncontrolled crash
 * or reset.The boot error source is used to report unhandled errors that
 * occurred in a previous boot. This mechanism is described in the BERT
 * table.
 *
 * For more information about BERT, please refer to ACPI Specification
 * version 4.0, section 17.3.1
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/cper.h>
#include <linux/io.h>

#include "apei-internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "BERT: " fmt

#define ACPI_BERT_PRINT_MAX_RECORDS 5
#define ACPI_BERT_PRINT_MAX_LEN 1024

static int bert_disable;

/*
 * Print "all" the error records in the BERT table, but avoid huge spam to
 * the console if the BIOS included oversize records, or too many records.
 * Skipping some records here does not lose anything because the full
 * data is available to user tools in:
 *	/sys/firmware/acpi/tables/data/BERT
 */
static void __init bert_print_all(struct acpi_bert_region *region,
				  unsigned int region_len)
{
	struct acpi_hest_generic_status *estatus =
		(struct acpi_hest_generic_status *)region;
	int remain = region_len;
	int printed = 0, skipped = 0;
	u32 estatus_len;

	while (remain >= sizeof(struct acpi_bert_region)) {
		estatus_len = cper_estatus_len(estatus);
		if (remain < estatus_len) {
			pr_err(FW_BUG "Truncated status block (length: %u).\n",
			       estatus_len);
			break;
		}

		/* No more error records. */
		if (!estatus->block_status)
			break;

		if (cper_estatus_check(estatus)) {
			pr_err(FW_BUG "Invalid error record.\n");
			break;
		}

		if (estatus_len < ACPI_BERT_PRINT_MAX_LEN &&
		    printed < ACPI_BERT_PRINT_MAX_RECORDS) {
			pr_info_once("Error records from previous boot:\n");
			cper_estatus_print(KERN_INFO HW_ERR, estatus);
			printed++;
		} else {
			skipped++;
		}

		/*
		 * Because the boot error source is "one-time polled" type,
		 * clear Block Status of current Generic Error Status Block,
		 * once it's printed.
		 */
		estatus->block_status = 0;

		estatus = (void *)estatus + estatus_len;
		remain -= estatus_len;
	}

	if (skipped)
		pr_info(HW_ERR "Skipped %d error records\n", skipped);

	if (printed + skipped)
		pr_info("Total records found: %d\n", printed + skipped);
}

static int __init setup_bert_disable(char *str)
{
	bert_disable = 1;

	return 1;
}
__setup("bert_disable", setup_bert_disable);

static int __init bert_check_table(struct acpi_table_bert *bert_tab)
{
	if (bert_tab->header.length < sizeof(struct acpi_table_bert) ||
	    bert_tab->region_length < sizeof(struct acpi_bert_region))
		return -EINVAL;

	return 0;
}

static int __init bert_init(void)
{
	struct apei_resources bert_resources;
	struct acpi_bert_region *boot_error_region;
	struct acpi_table_bert *bert_tab;
	unsigned int region_len;
	acpi_status status;
	int rc = 0;

	if (acpi_disabled)
		return 0;

	if (bert_disable) {
		pr_info("Boot Error Record Table support is disabled.\n");
		return 0;
	}

	status = acpi_get_table(ACPI_SIG_BERT, 0, (struct acpi_table_header **)&bert_tab);
	if (status == AE_NOT_FOUND)
		return 0;

	if (ACPI_FAILURE(status)) {
		pr_err("get table failed, %s.\n", acpi_format_exception(status));
		return -EINVAL;
	}

	rc = bert_check_table(bert_tab);
	if (rc) {
		pr_err(FW_BUG "table invalid.\n");
		goto out_put_bert_tab;
	}

	region_len = bert_tab->region_length;
	apei_resources_init(&bert_resources);
	rc = apei_resources_add(&bert_resources, bert_tab->address,
				region_len, true);
	if (rc)
		goto out_put_bert_tab;
	rc = apei_resources_request(&bert_resources, "APEI BERT");
	if (rc)
		goto out_fini;
	boot_error_region = ioremap_cache(bert_tab->address, region_len);
	if (boot_error_region) {
		bert_print_all(boot_error_region, region_len);
		iounmap(boot_error_region);
	} else {
		rc = -ENOMEM;
	}

	apei_resources_release(&bert_resources);
out_fini:
	apei_resources_fini(&bert_resources);
out_put_bert_tab:
	acpi_put_table((struct acpi_table_header *)bert_tab);

	return rc;
}

late_initcall(bert_init);
