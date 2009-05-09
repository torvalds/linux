/*
 * MPC8610 HPCD board specific routines
 *
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 * Recode: Jason Jin <jason.jin@freescale.com>
 *         York Sun <yorksun@freescale.com>
 *
 * Rewrite the interrupt routing. remove the 8259PIC support,
 * All the integrated device in ULI use sideband interrupt.
 *
 * Copyright 2008 Freescale Semiconductor Inc.
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
#include <linux/of.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc86xx.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <asm/mpic.h>

#include <linux/of_platform.h>
#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"

static unsigned char *pixis_bdcfg0, *pixis_arch;

static struct of_device_id __initdata mpc8610_ids[] = {
	{ .compatible = "fsl,mpc8610-immr", },
	{ .compatible = "simple-bus", },
	{ .compatible = "gianfar", },
	{}
};

static int __init mpc8610_declare_of_platform_devices(void)
{
	/* Without this call, the SSI device driver won't get probed. */
	of_platform_bus_probe(NULL, mpc8610_ids, NULL);

	return 0;
}
machine_device_initcall(mpc86xx_hpcd, mpc8610_declare_of_platform_devices);

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)

static u32 get_busfreq(void)
{
	struct device_node *node;

	u32 fs_busfreq = 0;
	node = of_find_node_by_type(NULL, "cpu");
	if (node) {
		unsigned int size;
		const unsigned int *prop =
			of_get_property(node, "bus-frequency", &size);
		if (prop)
			fs_busfreq = *prop;
		of_node_put(node);
	};
	return fs_busfreq;
}

unsigned int mpc8610hpcd_get_pixel_format(unsigned int bits_per_pixel,
						int monitor_port)
{
	static const unsigned long pixelformat[][3] = {
		{0x88882317, 0x88083218, 0x65052119},
		{0x88883316, 0x88082219, 0x65053118},
	};
	unsigned int pix_fmt, arch_monitor;

	arch_monitor = ((*pixis_arch == 0x01) && (monitor_port == 0))? 0 : 1;
		/* DVI port for board version 0x01 */

	if (bits_per_pixel == 32)
		pix_fmt = pixelformat[arch_monitor][0];
	else if (bits_per_pixel == 24)
		pix_fmt = pixelformat[arch_monitor][1];
	else if (bits_per_pixel == 16)
		pix_fmt = pixelformat[arch_monitor][2];
	else
		pix_fmt = pixelformat[1][0];

	return pix_fmt;
}

void mpc8610hpcd_set_gamma_table(int monitor_port, char *gamma_table_base)
{
	int i;
	if (monitor_port == 2) {		/* dual link LVDS */
		for (i = 0; i < 256*3; i++)
			gamma_table_base[i] = (gamma_table_base[i] << 2) |
					 ((gamma_table_base[i] >> 6) & 0x03);
	}
}

#define PX_BRDCFG0_DVISEL	(1 << 3)
#define PX_BRDCFG0_DLINK	(1 << 4)
#define PX_BRDCFG0_DIU_MASK	(PX_BRDCFG0_DVISEL | PX_BRDCFG0_DLINK)

void mpc8610hpcd_set_monitor_port(int monitor_port)
{
	static const u8 bdcfg[] = {
		PX_BRDCFG0_DVISEL | PX_BRDCFG0_DLINK,
		PX_BRDCFG0_DLINK,
		0,
	};

	if (monitor_port < 3)
		clrsetbits_8(pixis_bdcfg0, PX_BRDCFG0_DIU_MASK,
			     bdcfg[monitor_port]);
}

void mpc8610hpcd_set_pixel_clock(unsigned int pixclock)
{
	u32 __iomem *clkdvdr;
	u32 temp;
	/* variables for pixel clock calcs */
	ulong  bestval, bestfreq, speed_ccb, minpixclock, maxpixclock;
	ulong pixval;
	long err;
	int i;

	clkdvdr = ioremap(get_immrbase() + 0xe0800, sizeof(u32));
	if (!clkdvdr) {
		printk(KERN_ERR "Err: can't map clock divider register!\n");
		return;
	}

	/* Pixel Clock configuration */
	pr_debug("DIU: Bus Frequency = %d\n", get_busfreq());
	speed_ccb = get_busfreq();

	/* Calculate the pixel clock with the smallest error */
	/* calculate the following in steps to avoid overflow */
	pr_debug("DIU pixclock in ps - %d\n", pixclock);
	temp = 1000000000/pixclock;
	temp *= 1000;
	pixclock = temp;
	pr_debug("DIU pixclock freq - %u\n", pixclock);

	temp = pixclock * 5 / 100;
	pr_debug("deviation = %d\n", temp);
	minpixclock = pixclock - temp;
	maxpixclock = pixclock + temp;
	pr_debug("DIU minpixclock - %lu\n", minpixclock);
	pr_debug("DIU maxpixclock - %lu\n", maxpixclock);
	pixval = speed_ccb/pixclock;
	pr_debug("DIU pixval = %lu\n", pixval);

	err = 100000000;
	bestval = pixval;
	pr_debug("DIU bestval = %lu\n", bestval);

	bestfreq = 0;
	for (i = -1; i <= 1; i++) {
		temp = speed_ccb / ((pixval+i) + 1);
		pr_debug("DIU test pixval i= %d, pixval=%lu, temp freq. = %u\n",
							i, pixval, temp);
		if ((temp < minpixclock) || (temp > maxpixclock))
			pr_debug("DIU exceeds monitor range (%lu to %lu)\n",
				minpixclock, maxpixclock);
		else if (abs(temp - pixclock) < err) {
		  pr_debug("Entered the else if block %d\n", i);
			err = abs(temp - pixclock);
			bestval = pixval+i;
			bestfreq = temp;
		}
	}

	pr_debug("DIU chose = %lx\n", bestval);
	pr_debug("DIU error = %ld\n NomPixClk ", err);
	pr_debug("DIU: Best Freq = %lx\n", bestfreq);
	/* Modify PXCLK in GUTS CLKDVDR */
	pr_debug("DIU: Current value of CLKDVDR = 0x%08x\n", (*clkdvdr));
	temp = (*clkdvdr) & 0x2000FFFF;
	*clkdvdr = temp;		/* turn off clock */
	*clkdvdr = temp | 0x80000000 | (((bestval) & 0x1F) << 16);
	pr_debug("DIU: Modified value of CLKDVDR = 0x%08x\n", (*clkdvdr));
	iounmap(clkdvdr);
}

ssize_t mpc8610hpcd_show_monitor_port(int monitor_port, char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			"%c0 - DVI\n"
			"%c1 - Single link LVDS\n"
			"%c2 - Dual link LVDS\n",
			monitor_port == 0 ? '*' : ' ',
			monitor_port == 1 ? '*' : ' ',
			monitor_port == 2 ? '*' : ' ');
}

int mpc8610hpcd_set_sysfs_monitor_port(int val)
{
	return val < 3 ? val : 0;
}

#endif

static void __init mpc86xx_hpcd_setup_arch(void)
{
	struct resource r;
	struct device_node *np;
	unsigned char *pixis;

	if (ppc_md.progress)
		ppc_md.progress("mpc86xx_hpcd_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8610-pci")
		    || of_device_is_compatible(np, "fsl,mpc8641-pcie")) {
			struct resource rsrc;
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == 0xa000)
				fsl_add_bridge(np, 1);
			else
				fsl_add_bridge(np, 0);
		}
        }
#endif
#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)
	diu_ops.get_pixel_format	= mpc8610hpcd_get_pixel_format;
	diu_ops.set_gamma_table		= mpc8610hpcd_set_gamma_table;
	diu_ops.set_monitor_port	= mpc8610hpcd_set_monitor_port;
	diu_ops.set_pixel_clock		= mpc8610hpcd_set_pixel_clock;
	diu_ops.show_monitor_port	= mpc8610hpcd_show_monitor_port;
	diu_ops.set_sysfs_monitor_port	= mpc8610hpcd_set_sysfs_monitor_port;
#endif

	np = of_find_compatible_node(NULL, NULL, "fsl,fpga-pixis");
	if (np) {
		of_address_to_resource(np, 0, &r);
		of_node_put(np);
		pixis = ioremap(r.start, 32);
		if (!pixis) {
			printk(KERN_ERR "Err: can't map FPGA cfg register!\n");
			return;
		}
		pixis_bdcfg0 = pixis + 8;
		pixis_arch = pixis + 1;
	} else
		printk(KERN_ERR "Err: "
				"can't find device node 'fsl,fpga-pixis'\n");

	printk("MPC86xx HPCD board from Freescale Semiconductor\n");
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc86xx_hpcd_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "fsl,MPC8610HPCD"))
		return 1;	/* Looks good */

	return 0;
}

static long __init mpc86xx_time_init(void)
{
	unsigned int temp;

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	temp = mfspr(SPRN_HID0);
	temp |= HID0_TBEN;
	mtspr(SPRN_HID0, temp);
	asm volatile("isync");

	return 0;
}

define_machine(mpc86xx_hpcd) {
	.name			= "MPC86xx HPCD",
	.probe			= mpc86xx_hpcd_probe,
	.setup_arch		= mpc86xx_hpcd_setup_arch,
	.init_IRQ		= mpc86xx_init_irq,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
};
