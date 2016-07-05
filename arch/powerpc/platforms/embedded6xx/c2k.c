/*
 * Board setup routines for the GEFanuc C2K board
 *
 * Author: Remi Machet <rmachet@slac.stanford.edu>
 *
 * Originated from prpmc2800.c
 *
 * 2008 (c) Stanford University
 * 2007 (c) MontaVista, Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/time.h>

#include <mm/mmu_decl.h>

#include <sysdev/mv64x60.h>

#define MV64x60_MPP_CNTL_0	0x0000
#define MV64x60_MPP_CNTL_2	0x0008

#define MV64x60_GPP_IO_CNTL	0x0000
#define MV64x60_GPP_LEVEL_CNTL	0x0010
#define MV64x60_GPP_VALUE_SET	0x0018

static void __iomem *mv64x60_mpp_reg_base;
static void __iomem *mv64x60_gpp_reg_base;

static void __init c2k_setup_arch(void)
{
	struct device_node *np;
	phys_addr_t paddr;
	const unsigned int *reg;

	/*
	 * ioremap mpp and gpp registers in case they are later
	 * needed by c2k_reset_board().
	 */
	np = of_find_compatible_node(NULL, NULL, "marvell,mv64360-mpp");
	reg = of_get_property(np, "reg", NULL);
	paddr = of_translate_address(np, reg);
	of_node_put(np);
	mv64x60_mpp_reg_base = ioremap(paddr, reg[1]);

	np = of_find_compatible_node(NULL, NULL, "marvell,mv64360-gpp");
	reg = of_get_property(np, "reg", NULL);
	paddr = of_translate_address(np, reg);
	of_node_put(np);
	mv64x60_gpp_reg_base = ioremap(paddr, reg[1]);

#ifdef CONFIG_PCI
	mv64x60_pci_init();
#endif
}

static void c2k_reset_board(void)
{
	u32 temp;

	local_irq_disable();

	temp = in_le32(mv64x60_mpp_reg_base + MV64x60_MPP_CNTL_0);
	temp &= 0xFFFF0FFF;
	out_le32(mv64x60_mpp_reg_base + MV64x60_MPP_CNTL_0, temp);

	temp = in_le32(mv64x60_gpp_reg_base + MV64x60_GPP_LEVEL_CNTL);
	temp |= 0x00000004;
	out_le32(mv64x60_gpp_reg_base + MV64x60_GPP_LEVEL_CNTL, temp);

	temp = in_le32(mv64x60_gpp_reg_base + MV64x60_GPP_IO_CNTL);
	temp |= 0x00000004;
	out_le32(mv64x60_gpp_reg_base + MV64x60_GPP_IO_CNTL, temp);

	temp = in_le32(mv64x60_mpp_reg_base + MV64x60_MPP_CNTL_2);
	temp &= 0xFFFF0FFF;
	out_le32(mv64x60_mpp_reg_base + MV64x60_MPP_CNTL_2, temp);

	temp = in_le32(mv64x60_gpp_reg_base + MV64x60_GPP_LEVEL_CNTL);
	temp |= 0x00080000;
	out_le32(mv64x60_gpp_reg_base + MV64x60_GPP_LEVEL_CNTL, temp);

	temp = in_le32(mv64x60_gpp_reg_base + MV64x60_GPP_IO_CNTL);
	temp |= 0x00080000;
	out_le32(mv64x60_gpp_reg_base + MV64x60_GPP_IO_CNTL, temp);

	out_le32(mv64x60_gpp_reg_base + MV64x60_GPP_VALUE_SET, 0x00080004);
}

static void __noreturn c2k_restart(char *cmd)
{
	c2k_reset_board();
	msleep(100);
	panic("restart failed\n");
}

#ifdef CONFIG_NOT_COHERENT_CACHE
#define COHERENCY_SETTING "off"
#else
#define COHERENCY_SETTING "on"
#endif

void c2k_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "Vendor\t\t: GEFanuc\n");
	seq_printf(m, "coherency\t: %s\n", COHERENCY_SETTING);
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init c2k_probe(void)
{
	if (!of_machine_is_compatible("GEFanuc,C2K"))
		return 0;

	printk(KERN_INFO "Detected a GEFanuc C2K board\n");

	_set_L2CR(0);
	_set_L2CR(L2CR_L2E | L2CR_L2PE | L2CR_L2I);

	mv64x60_init_early();

	return 1;
}

define_machine(c2k) {
	.name			= "C2K",
	.probe			= c2k_probe,
	.setup_arch		= c2k_setup_arch,
	.show_cpuinfo		= c2k_show_cpuinfo,
	.init_IRQ		= mv64x60_init_irq,
	.get_irq		= mv64x60_get_irq,
	.restart		= c2k_restart,
	.calibrate_decr		= generic_calibrate_decr,
};
