// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Copyright (C) 2011, Maarten ter Huurne <maarten@treewalker.org>
 *  JZ4740 setup code
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>

#include <asm/bootinfo.h>
#include <asm/prom.h>

#include "reset.h"

#define JZ4740_EMC_BASE_ADDR 0x13010000

#define JZ4740_EMC_SDRAM_CTRL 0x80

static void __init jz4740_detect_mem(void)
{
	void __iomem *jz_emc_base;
	u32 ctrl, bus, bank, rows, cols;
	phys_addr_t size;

	jz_emc_base = ioremap(JZ4740_EMC_BASE_ADDR, 0x100);
	ctrl = readl(jz_emc_base + JZ4740_EMC_SDRAM_CTRL);
	bus = 2 - ((ctrl >> 31) & 1);
	bank = 1 + ((ctrl >> 19) & 1);
	cols = 8 + ((ctrl >> 26) & 7);
	rows = 11 + ((ctrl >> 20) & 3);
	printk(KERN_DEBUG
		"SDRAM preconfigured: bus:%u bank:%u rows:%u cols:%u\n",
		bus, bank, rows, cols);
	iounmap(jz_emc_base);

	size = 1 << (bus + bank + cols + rows);
	add_memory_region(0, size, BOOT_MEM_RAM);
}

static unsigned long __init get_board_mach_type(const void *fdt)
{
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,x1830"))
		return MACH_INGENIC_X1830;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,x1000"))
		return MACH_INGENIC_X1000;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4780"))
		return MACH_INGENIC_JZ4780;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4770"))
		return MACH_INGENIC_JZ4770;

	return MACH_INGENIC_JZ4740;
}

void __init plat_mem_setup(void)
{
	int offset;
	void *dtb;

	jz4740_reset_init();

	if (__dtb_start != __dtb_end)
		dtb = __dtb_start;
	else
		dtb = (void *)fw_passed_dtb;

	__dt_setup_arch(dtb);

	offset = fdt_path_offset(dtb, "/memory");
	if (offset < 0)
		jz4740_detect_mem();

	mips_machtype = get_board_mach_type(dtb);
}

void __init device_tree_init(void)
{
	if (!initial_boot_params)
		return;

	unflatten_and_copy_device_tree();
}

const char *get_system_type(void)
{
	switch (mips_machtype) {
	case MACH_INGENIC_X1830:
		return "X1830";
	case MACH_INGENIC_X1000:
		return "X1000";
	case MACH_INGENIC_JZ4780:
		return "JZ4780";
	case MACH_INGENIC_JZ4770:
		return "JZ4770";
	default:
		return "JZ4740";
	}
}

void __init arch_init_irq(void)
{
	irqchip_init();
}
