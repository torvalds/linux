// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025, Intel Corporation.
 *
 * Memory Range and Region Mapping (MRRM) structure
 */

#define pr_fmt(fmt) "acpi/mrrm: " fmt

#include <linux/acpi.h>
#include <linux/init.h>

static int max_mem_region = -ENOENT;

/* Access for use by resctrl file system */
int acpi_mrrm_max_mem_region(void)
{
	return max_mem_region;
}

static __init int acpi_parse_mrrm(struct acpi_table_header *table)
{
	struct acpi_table_mrrm *mrrm;

	mrrm = (struct acpi_table_mrrm *)table;
	if (!mrrm)
		return -ENODEV;

	max_mem_region = mrrm->max_mem_region;

	return 0;
}

static __init int mrrm_init(void)
{
	int ret;

	ret = acpi_table_parse(ACPI_SIG_MRRM, acpi_parse_mrrm);

	return ret;
}
device_initcall(mrrm_init);
