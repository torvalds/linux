/*
 * MPC8560ADS board specific routines
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>	/* for linux/serial_core.h */
#include <linux/serial_core.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/fsl_devices.h>
#include <linux/fs_enet_pd.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <asm/immap_85xx.h>
#include <asm/kgdb.h>
#include <asm/ppc_sys.h>
#include <asm/cpm2.h>
#include <mm/mmu_decl.h>

#include <syslib/cpm2_pic.h>
#include <syslib/ppc85xx_common.h>
#include <syslib/ppc85xx_setup.h>


/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void init_fcc_ioports(void)
{
	struct immap *immap;
	struct io_port *io;
	u32 tempval;

	immap = cpm2_immr;

	io = &immap->im_ioport;
	/* FCC2/3 are on the ports B/C. */
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

	/* Port C has clocks......  */
	tempval = in_be32(&io->iop_psorc);
	tempval &= ~(CLK_TRX);
	out_be32(&io->iop_psorc, tempval);

	tempval = in_be32(&io->iop_pdirc);
	tempval &= ~(CLK_TRX);
	out_be32(&io->iop_pdirc, tempval);
	tempval = in_be32(&io->iop_pparc);
	tempval |= (CLK_TRX);
	out_be32(&io->iop_pparc, tempval);

	/* Configure Serial Interface clock routing.
	 * First,  clear all FCC bits to zero,
	 * then set the ones we want.
	 */
	immap->im_cpmux.cmx_fcr &= ~(CPMUX_CLK_MASK);
	immap->im_cpmux.cmx_fcr |= CPMUX_CLK_ROUTE;
}

static void __init
mpc8560ads_setup_arch(void)
{
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;
	struct gianfar_platform_data *pdata;
	struct gianfar_mdio_data *mdata;
	struct fs_platform_info *fpi;

	cpm2_reset();

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	if (ppc_md.progress)
		ppc_md.progress("mpc8560ads_setup_arch()", 0);

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	loops_per_jiffy = freq / HZ;

#ifdef CONFIG_PCI
	/* setup PCI host bridges */
	mpc85xx_setup_hose();
#endif

	/* setup the board related info for the MDIO bus */
	mdata = (struct gianfar_mdio_data *) ppc_sys_get_pdata(MPC85xx_MDIO);

	mdata->irq[0] = MPC85xx_IRQ_EXT5;
	mdata->irq[1] = MPC85xx_IRQ_EXT5;
	mdata->irq[2] = PHY_POLL;
	mdata->irq[3] = MPC85xx_IRQ_EXT5;
	mdata->irq[31] = PHY_POLL;

	/* setup the board related information for the enet controllers */
	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC1);
	if (pdata) {
		pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
		pdata->bus_id = 0;
		pdata->phy_id = 0;
		memcpy(pdata->mac_addr, binfo->bi_enetaddr, 6);
	}

	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC2);
	if (pdata) {
		pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
		pdata->bus_id = 0;
		pdata->phy_id = 1;
		memcpy(pdata->mac_addr, binfo->bi_enet1addr, 6);
	}

	init_fcc_ioports();
	ppc_sys_device_remove(MPC85xx_CPM_FCC1);

	fpi = (struct fs_platform_info *) ppc_sys_get_pdata(MPC85xx_CPM_FCC2);
	if (fpi) {
		memcpy(fpi->macaddr, binfo->bi_enet2addr, 6);
		fpi->bus_id = "0:02";
		fpi->phy_addr = 2;
		fpi->dpram_offset = (u32)cpm2_immr->im_dprambase;
		fpi->fcc_regs_c = (u32)&cpm2_immr->im_fcc_c[1];
	}

	fpi = (struct fs_platform_info *) ppc_sys_get_pdata(MPC85xx_CPM_FCC3);
	if (fpi) {
		memcpy(fpi->macaddr, binfo->bi_enet2addr, 6);
		fpi->macaddr[5] += 1;
		fpi->bus_id = "0:03";
		fpi->phy_addr = 3;
		fpi->dpram_offset = (u32)cpm2_immr->im_dprambase;
		fpi->fcc_regs_c = (u32)&cpm2_immr->im_fcc_c[2];
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef  CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif
}

static irqreturn_t cpm2_cascade(int irq, void *dev_id)
{
	while ((irq = cpm2_get_irq()) >= 0)
		__do_IRQ(irq);
	return IRQ_HANDLED;
}

static struct irqaction cpm2_irqaction = {
	.handler = cpm2_cascade,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "cpm2_cascade",
};

static void __init
mpc8560_ads_init_IRQ(void)
{
	/* Setup OpenPIC */
	mpc85xx_ads_init_IRQ();

	/* Setup CPM2 PIC */
        cpm2_init_IRQ();

	setup_irq(MPC85xx_IRQ_CPM, &cpm2_irqaction);

	return;
}



/* ************************************************************************ */
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* parse_bootinfo must always be called first */
	parse_bootinfo(find_bootinfo());

	/*
	 * If we were passed in a board information, copy it into the
	 * residual data area.
	 */
	if (r3) {
		memcpy((void *) __res, (void *) (r3 + KERNELBASE),
		       sizeof (bd_t));

	}
#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif				/* CONFIG_BLK_DEV_INITRD */

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
		*(char *) (r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *) (r6 + KERNELBASE));
	}

	identify_ppc_sys_by_id(mfspr(SPRN_SVR));

	/* setup the PowerPC module struct */
	ppc_md.setup_arch = mpc8560ads_setup_arch;
	ppc_md.show_cpuinfo = mpc85xx_ads_show_cpuinfo;

	ppc_md.init_IRQ = mpc8560_ads_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = mpc85xx_restart;
	ppc_md.power_off = mpc85xx_power_off;
	ppc_md.halt = mpc85xx_halt;

	ppc_md.find_end_of_memory = mpc85xx_find_end_of_memory;

	ppc_md.time_init = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = mpc85xx_calibrate_decr;

	if (ppc_md.progress)
		ppc_md.progress("mpc8560ads_init(): exit", 0);

	return;
}
