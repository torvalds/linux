/*
 * Pistachio platform setup
 *
 * Copyright (C) 2014 Google, Inc.
 * Copyright (C) 2016 Imagination Technologies
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>

#include <asm/cacheflush.h>
#include <asm/dma-coherence.h>
#include <asm/fw/fw.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-cps.h>
#include <asm/prom.h>
#include <asm/smp-ops.h>
#include <asm/traps.h>

/*
 * Core revision register decoding
 * Bits 23 to 20: Major rev
 * Bits 15 to 8: Minor rev
 * Bits 7 to 0: Maintenance rev
 */
#define PISTACHIO_CORE_REV_REG	0xB81483D0
#define PISTACHIO_CORE_REV_A1	0x00100006
#define PISTACHIO_CORE_REV_B0	0x00100106

const char *get_system_type(void)
{
	u32 core_rev;
	const char *sys_type;

	core_rev = __raw_readl((const void *)PISTACHIO_CORE_REV_REG);

	switch (core_rev) {
	case PISTACHIO_CORE_REV_B0:
		sys_type = "IMG Pistachio SoC (B0)";
		break;

	case PISTACHIO_CORE_REV_A1:
		sys_type = "IMG Pistachio SoC (A1)";
		break;

	default:
		sys_type = "IMG Pistachio SoC";
		break;
	}

	return sys_type;
}

void __init *plat_get_fdt(void)
{
	if (fw_arg0 != -2)
		panic("Device-tree not present");
	return (void *)fw_arg1;
}

void __init plat_mem_setup(void)
{
	__dt_setup_arch(plat_get_fdt());
}

#define DEFAULT_CPC_BASE_ADDR	0x1bde0000
#define DEFAULT_CDMM_BASE_ADDR	0x1bdd0000

phys_addr_t mips_cpc_default_phys_base(void)
{
	return DEFAULT_CPC_BASE_ADDR;
}

phys_addr_t mips_cdmm_phys_base(void)
{
	return DEFAULT_CDMM_BASE_ADDR;
}

static void __init mips_nmi_setup(void)
{
	void *base;
	extern char except_vec_nmi;

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa80) :
		(void *)(CAC_BASE + 0x380);
	memcpy(base, &except_vec_nmi, 0x80);
	flush_icache_range((unsigned long)base,
			   (unsigned long)base + 0x80);
}

static void __init mips_ejtag_setup(void)
{
	void *base;
	extern char except_vec_ejtag_debug;

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa00) :
		(void *)(CAC_BASE + 0x300);
	memcpy(base, &except_vec_ejtag_debug, 0x80);
	flush_icache_range((unsigned long)base,
			   (unsigned long)base + 0x80);
}

void __init prom_init(void)
{
	board_nmi_handler_setup = mips_nmi_setup;
	board_ejtag_handler_setup = mips_ejtag_setup;

	mips_cm_probe();
	mips_cpc_probe();
	register_cps_smp_ops();

	pr_info("SoC Type: %s\n", get_system_type());
}

void __init prom_free_prom_memory(void)
{
}

void __init device_tree_init(void)
{
	if (!initial_boot_params)
		return;

	unflatten_and_copy_device_tree();
}
