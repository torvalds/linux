/*
 * MIPS support for CONFIG_OF device tree support
 *
 * Copyright (C) 2010 Cisco Systems Inc. <dediao@cisco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/prom.h>

static char mips_machine_name[64] = "Unknown";

__init void mips_set_machine_name(const char *name)
{
	if (name == NULL)
		return;

	strlcpy(mips_machine_name, name, sizeof(mips_machine_name));
	pr_info("MIPS: machine is %s\n", mips_get_machine_name());
}

char *mips_get_machine_name(void)
{
	return mips_machine_name;
}

#ifdef CONFIG_USE_OF
void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	if (base >= PHYS_ADDR_MAX) {
		pr_warn("Trying to add an invalid memory region, skipped\n");
		return;
	}

	/* Truncate the passed memory region instead of type casting */
	if (base + size - 1 >= PHYS_ADDR_MAX || base + size < base) {
		pr_warn("Truncate memory region %llx @ %llx to size %llx\n",
			size, base, PHYS_ADDR_MAX - base);
		size = PHYS_ADDR_MAX - base;
	}

	add_memory_region(base, size, BOOT_MEM_RAM);
}

int __init early_init_dt_reserve_memory_arch(phys_addr_t base,
					phys_addr_t size, bool nomap)
{
	add_memory_region(base, size, BOOT_MEM_RESERVED);
	return 0;
}

void __init __dt_setup_arch(void *bph)
{
	if (!early_init_dt_scan(bph))
		return;

	mips_set_machine_name(of_flat_dt_get_machine_name());
}

int __init __dt_register_buses(const char *bus0, const char *bus1)
{
	static struct of_device_id of_ids[3];

	if (!of_have_populated_dt())
		panic("device tree not present");

	strlcpy(of_ids[0].compatible, bus0, sizeof(of_ids[0].compatible));
	if (bus1) {
		strlcpy(of_ids[1].compatible, bus1,
			sizeof(of_ids[1].compatible));
	}

	if (of_platform_populate(NULL, of_ids, NULL, NULL))
		panic("failed to populate DT");

	return 0;
}

#endif
