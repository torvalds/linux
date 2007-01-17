/*
 * MPC85xx setup and early boot code plus other random bits.
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/prom.h>
#include <asm/mpic.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <sysdev/fsl_soc.h>
#include "mpc85xx.h"

#ifdef CONFIG_CPM2
#include <linux/fs_enet_pd.h>
#include <asm/cpm2.h>
#include <sysdev/cpm2_pic.h>
#include <asm/fs_pd.h>
#endif

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

#ifdef CONFIG_PCI
int
mpc85xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_CPM2

static void cpm2_cascade(unsigned int irq, struct irq_desc *desc)
{
	int cascade_irq;

	while ((cascade_irq = cpm2_get_irq()) >= 0) {
		generic_handle_irq(cascade_irq);
	}
	desc->chip->eoi(irq);
}

#endif /* CONFIG_CPM2 */

void __init mpc85xx_ads_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np = NULL;
#ifdef CONFIG_CPM2
	int irq;
#endif

	np = of_find_node_by_type(np, "open-pic");

	if (np == NULL) {
		printk(KERN_ERR "Could not find open-pic node\n");
		return;
	}

	if(of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Could not map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			4, 0, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	of_node_put(np);

	mpic_assign_isu(mpic, 0, r.start + 0x10200);
	mpic_assign_isu(mpic, 1, r.start + 0x10280);
	mpic_assign_isu(mpic, 2, r.start + 0x10300);
	mpic_assign_isu(mpic, 3, r.start + 0x10380);
	mpic_assign_isu(mpic, 4, r.start + 0x10400);
	mpic_assign_isu(mpic, 5, r.start + 0x10480);
	mpic_assign_isu(mpic, 6, r.start + 0x10500);
	mpic_assign_isu(mpic, 7, r.start + 0x10580);

	/* Unused on this platform (leave room for 8548) */
	mpic_assign_isu(mpic, 8, r.start + 0x10600);
	mpic_assign_isu(mpic, 9, r.start + 0x10680);
	mpic_assign_isu(mpic, 10, r.start + 0x10700);
	mpic_assign_isu(mpic, 11, r.start + 0x10780);

	/* External Interrupts */
	mpic_assign_isu(mpic, 12, r.start + 0x10000);
	mpic_assign_isu(mpic, 13, r.start + 0x10080);
	mpic_assign_isu(mpic, 14, r.start + 0x10100);

	mpic_init(mpic);

#ifdef CONFIG_CPM2
	/* Setup CPM2 PIC */
	np = of_find_node_by_type(NULL, "cpm-pic");
	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find cpm-pic node\n");
                return;
	}
	irq = irq_of_parse_and_map(np, 0);

	cpm2_pic_init(np);
	set_irq_chained_handler(irq, cpm2_cascade);
#endif
}

/*
 * Setup the architecture
 */
#ifdef CONFIG_CPM2
void init_fcc_ioports(struct fs_platform_info *fpi)
{
	struct io_port *io = cpm2_map(im_ioport);
	int fcc_no = fs_get_fcc_index(fpi->fs_no);
	int target;
	u32 tempval;

	switch(fcc_no) {
	case 1:
		tempval = in_be32(&io->iop_pdirb);
		tempval &= ~PB2_DIRB0;
		tempval |= PB2_DIRB1;
		out_be32(&io->iop_pdirb, tempval);

		tempval = in_be32(&io->iop_psorb);
		tempval &= ~PB2_PSORB0;
		tempval |= PB2_PSORB1;
		out_be32(&io->iop_psorb, tempval);

		tempval = in_be32(&io->iop_pparb);
		tempval |= (PB2_DIRB0 | PB2_DIRB1);
		out_be32(&io->iop_pparb, tempval);

		target = CPM_CLK_FCC2;
		break;
	case 2:
		tempval = in_be32(&io->iop_pdirb);
		tempval &= ~PB3_DIRB0;
		tempval |= PB3_DIRB1;
		out_be32(&io->iop_pdirb, tempval);

		tempval = in_be32(&io->iop_psorb);
		tempval &= ~PB3_PSORB0;
		tempval |= PB3_PSORB1;
		out_be32(&io->iop_psorb, tempval);

		tempval = in_be32(&io->iop_pparb);
		tempval |= (PB3_DIRB0 | PB3_DIRB1);
		out_be32(&io->iop_pparb, tempval);

		tempval = in_be32(&io->iop_pdirc);
		tempval |= PC3_DIRC1;
		out_be32(&io->iop_pdirc, tempval);

		tempval = in_be32(&io->iop_pparc);
		tempval |= PC3_DIRC1;
		out_be32(&io->iop_pparc, tempval);

		target = CPM_CLK_FCC3;
		break;
	default:
		printk(KERN_ERR "init_fcc_ioports: invalid FCC number\n");
		return;
	}

	/* Port C has clocks......  */
	tempval = in_be32(&io->iop_psorc);
	tempval &= ~(PC_CLK(fpi->clk_rx - 8) | PC_CLK(fpi->clk_tx - 8));
	out_be32(&io->iop_psorc, tempval);

	tempval = in_be32(&io->iop_pdirc);
	tempval &= ~(PC_CLK(fpi->clk_rx - 8) | PC_CLK(fpi->clk_tx - 8));
	out_be32(&io->iop_pdirc, tempval);
	tempval = in_be32(&io->iop_pparc);
	tempval |= (PC_CLK(fpi->clk_rx - 8) | PC_CLK(fpi->clk_tx - 8));
	out_be32(&io->iop_pparc, tempval);

	cpm2_unmap(io);

	/* Configure Serial Interface clock routing.
	 * First,  clear FCC bits to zero,
	 * then set the ones we want.
	 */
	cpm2_clk_setup(target, fpi->clk_rx, CPM_CLK_RX);
	cpm2_clk_setup(target, fpi->clk_tx, CPM_CLK_TX);
}
#endif

static void __init mpc85xx_ads_setup_arch(void)
{
	struct device_node *cpu;
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_ads_setup_arch()", 0);

	cpu = of_find_node_by_type(NULL, "cpu");
	if (cpu != 0) {
		const unsigned int *fp;

		fp = get_property(cpu, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(cpu);
	}

#ifdef CONFIG_CPM2
	cpm2_reset();
#endif

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);
	ppc_md.pci_exclude_device = mpc85xx_exclude_device;
#endif

#ifdef  CONFIG_ROOT_NFS
	ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif
}

void mpc85xx_ads_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "Machine\t\t: mpc85xx\n");
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc85xx_ads_probe(void)
{
	/* We always match for now, eventually we should look at the flat
	   dev tree to ensure this is the board we are suppose to run on
	*/
	return 1;
}

define_machine(mpc85xx_ads) {
	.name			= "MPC85xx ADS",
	.probe			= mpc85xx_ads_probe,
	.setup_arch		= mpc85xx_ads_setup_arch,
	.init_IRQ		= mpc85xx_ads_pic_init,
	.show_cpuinfo		= mpc85xx_ads_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= mpc85xx_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
