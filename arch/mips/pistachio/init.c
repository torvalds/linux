/*
 * Pistachio platform setup
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/cacheflush.h>
#include <asm/dma-coherence.h>
#include <asm/fw/fw.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-cm.h>
#include <asm/mips-cpc.h>
#include <asm/prom.h>
#include <asm/smp-ops.h>
#include <asm/traps.h>

const char *get_system_type(void)
{
	return "IMG Pistachio SoC";
}

static void __init plat_setup_iocoherency(void)
{
	/*
	 * Kernel has been configured with software coherency
	 * but we might choose to turn it off and use hardware
	 * coherency instead.
	 */
	if (mips_cm_numiocu() != 0) {
		/* Nothing special needs to be done to enable coherency */
		pr_info("CMP IOCU detected\n");
		hw_coherentio = 1;
		if (coherentio == 0)
			pr_info("Hardware DMA cache coherency disabled\n");
		else
			pr_info("Hardware DMA cache coherency enabled\n");
	} else {
		if (coherentio == 1)
			pr_info("Hardware DMA cache coherency unsupported, but enabled from command line!\n");
		else
			pr_info("Software DMA cache coherency enabled\n");
	}
}

void __init plat_mem_setup(void)
{
	if (fw_arg0 != -2)
		panic("Device-tree not present");

	__dt_setup_arch((void *)fw_arg1);
	strlcpy(arcs_cmdline, boot_command_line, COMMAND_LINE_SIZE);

	plat_setup_iocoherency();
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

static int __init plat_of_setup(void)
{
	if (!of_have_populated_dt())
		panic("Device tree not present");

	if (of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL))
		panic("Failed to populate DT");

	return 0;
}
arch_initcall(plat_of_setup);
