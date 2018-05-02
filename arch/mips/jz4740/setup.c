/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Copyright (C) 2011, Maarten ter Huurne <maarten@treewalker.org>
 *  JZ4740 setup code
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>

#include <asm/bootinfo.h>
#include <asm/prom.h>

#include <asm/mach-jz4740/base.h>

#include "reset.h"


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
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4780"))
		return MACH_INGENIC_JZ4780;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4770"))
		return MACH_INGENIC_JZ4770;

	return MACH_INGENIC_JZ4740;
}

void __init plat_mem_setup(void)
{
	int offset;

	jz4740_reset_init();
	__dt_setup_arch(__dtb_start);

	offset = fdt_path_offset(__dtb_start, "/memory");
	if (offset < 0)
		jz4740_detect_mem();

	mips_machtype = get_board_mach_type(__dtb_start);
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
