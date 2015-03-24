/*
 *  ARM64 Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2013-2014, Linaro Ltd.
 *	Author: Al Stone <al.stone@linaro.org>
 *	Author: Graeme Gregory <graeme.gregory@linaro.org>
 *	Author: Hanjun Guo <hanjun.guo@linaro.org>
 *	Author: Tomasz Nowicki <tomasz.nowicki@linaro.org>
 *	Author: Naresh Bhat <naresh.bhat@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/smp.h>

int acpi_noirq = 1;		/* skip ACPI IRQ initialization */
int acpi_disabled = 1;
EXPORT_SYMBOL(acpi_disabled);

int acpi_pci_disabled = 1;	/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);

static bool param_acpi_off __initdata;
static bool param_acpi_force __initdata;

static int __init parse_acpi(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* "acpi=off" disables both ACPI table parsing and interpreter */
	if (strcmp(arg, "off") == 0)
		param_acpi_off = true;
	else if (strcmp(arg, "force") == 0) /* force ACPI to be enabled */
		param_acpi_force = true;
	else
		return -EINVAL;	/* Core will print when we return error */

	return 0;
}
early_param("acpi", parse_acpi);

static int __init dt_scan_depth1_nodes(unsigned long node,
				       const char *uname, int depth,
				       void *data)
{
	/*
	 * Return 1 as soon as we encounter a node at depth 1 that is
	 * not the /chosen node.
	 */
	if (depth == 1 && (strcmp(uname, "chosen") != 0))
		return 1;
	return 0;
}

/*
 * __acpi_map_table() will be called before page_init(), so early_ioremap()
 * or early_memremap() should be called here to for ACPI table mapping.
 */
char *__init __acpi_map_table(unsigned long phys, unsigned long size)
{
	if (!size)
		return NULL;

	return early_memremap(phys, size);
}

void __init __acpi_unmap_table(char *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_memunmap(map, size);
}

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{
	struct acpi_table_fadt *fadt = (struct acpi_table_fadt *)table;

	/*
	 * Revision in table header is the FADT Major revision, and there
	 * is a minor revision of FADT which was introduced by ACPI 5.1,
	 * we only deal with ACPI 5.1 or newer revision to get GIC and SMP
	 * boot protocol configuration data, or we will disable ACPI.
	 */
	if (table->revision > 5 ||
	    (table->revision == 5 && fadt->minor_revision >= 1))
		return 0;

	pr_warn("Unsupported FADT revision %d.%d, should be 5.1+, will disable ACPI\n",
		table->revision, fadt->minor_revision);
	disable_acpi();

	return -EINVAL;
}

/*
 * acpi_boot_table_init() called from setup_arch(), always.
 *	1. find RSDP and get its address, and then find XSDT
 *	2. extract all tables and checksums them all
 *	3. check ACPI FADT revision
 *
 * We can parse ACPI boot-time tables such as MADT after
 * this function is called.
 */
void __init acpi_boot_table_init(void)
{
	/*
	 * Enable ACPI instead of device tree unless
	 * - ACPI has been disabled explicitly (acpi=off), or
	 * - the device tree is not empty (it has more than just a /chosen node)
	 *   and ACPI has not been force enabled (acpi=force)
	 */
	if (param_acpi_off ||
	    (!param_acpi_force && of_scan_flat_dt(dt_scan_depth1_nodes, NULL)))
		return;

	enable_acpi();

	/* Initialize the ACPI boot-time table parser. */
	if (acpi_table_init()) {
		disable_acpi();
		return;
	}

	if (acpi_table_parse(ACPI_SIG_FADT, acpi_parse_fadt)) {
		/* disable ACPI if no FADT is found */
		disable_acpi();
		pr_err("Can't find FADT\n");
	}
}
