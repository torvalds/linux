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
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <asm/mpic.h>

#include <linux/of_platform.h>
#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/simple_gpio.h>

#include "mpc86xx.h"

static struct device_node *pixis_node;
static unsigned char *pixis_bdcfg0, *pixis_arch;

#ifdef CONFIG_SUSPEND
static irqreturn_t mpc8610_sw9_irq(int irq, void *data)
{
	pr_debug("%s: PIXIS' event (sw9/wakeup) IRQ handled\n", __func__);
	return IRQ_HANDLED;
}

static void __init mpc8610_suspend_init(void)
{
	int irq;
	int ret;

	if (!pixis_node)
		return;

	irq = irq_of_parse_and_map(pixis_node, 0);
	if (!irq) {
		pr_err("%s: can't map pixis event IRQ.\n", __func__);
		return;
	}

	ret = request_irq(irq, mpc8610_sw9_irq, 0, "sw9:wakeup", NULL);
	if (ret) {
		pr_err("%s: can't request pixis event IRQ: %d\n",
		       __func__, ret);
		irq_dispose_mapping(irq);
	}

	enable_irq_wake(irq);
}
#else
static inline void mpc8610_suspend_init(void) { }
#endif /* CONFIG_SUSPEND */

static struct of_device_id __initdata mpc8610_ids[] = {
	{ .compatible = "fsl,mpc8610-immr", },
	{ .compatible = "fsl,mpc8610-guts", },
	{ .compatible = "simple-bus", },
	/* So that the DMA channel nodes can be probed individually: */
	{ .compatible = "fsl,eloplus-dma", },
	{}
};

static int __init mpc8610_declare_of_platform_devices(void)
{
	/* Firstly, register PIXIS GPIOs. */
	simple_gpiochip_init("fsl,fpga-pixis-gpio-bank");

	/* Enable wakeup on PIXIS' event IRQ. */
	mpc8610_suspend_init();

	/* Without this call, the SSI device driver won't get probed. */
	of_platform_bus_probe(NULL, mpc8610_ids, NULL);

	return 0;
}
machine_device_initcall(mpc86xx_hpcd, mpc8610_declare_of_platform_devices);

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)

/*
 * DIU Area Descriptor
 *
 * The MPC8610 reference manual shows the bits of the AD register in
 * little-endian order, which causes the BLUE_C field to be split into two
 * parts. To simplify the definition of the MAKE_AD() macro, we define the
 * fields in big-endian order and byte-swap the result.
 *
 * So even though the registers don't look like they're in the
 * same bit positions as they are on the P1022, the same value is written to
 * the AD register on the MPC8610 and on the P1022.
 */
#define AD_BYTE_F		0x10000000
#define AD_ALPHA_C_MASK		0x0E000000
#define AD_ALPHA_C_SHIFT	25
#define AD_BLUE_C_MASK		0x01800000
#define AD_BLUE_C_SHIFT		23
#define AD_GREEN_C_MASK		0x00600000
#define AD_GREEN_C_SHIFT	21
#define AD_RED_C_MASK		0x00180000
#define AD_RED_C_SHIFT		19
#define AD_PALETTE		0x00040000
#define AD_PIXEL_S_MASK		0x00030000
#define AD_PIXEL_S_SHIFT	16
#define AD_COMP_3_MASK		0x0000F000
#define AD_COMP_3_SHIFT		12
#define AD_COMP_2_MASK		0x00000F00
#define AD_COMP_2_SHIFT		8
#define AD_COMP_1_MASK		0x000000F0
#define AD_COMP_1_SHIFT		4
#define AD_COMP_0_MASK		0x0000000F
#define AD_COMP_0_SHIFT		0

#define MAKE_AD(alpha, red, blue, green, size, c0, c1, c2, c3) \
	cpu_to_le32(AD_BYTE_F | (alpha << AD_ALPHA_C_SHIFT) | \
	(blue << AD_BLUE_C_SHIFT) | (green << AD_GREEN_C_SHIFT) | \
	(red << AD_RED_C_SHIFT) | (c3 << AD_COMP_3_SHIFT) | \
	(c2 << AD_COMP_2_SHIFT) | (c1 << AD_COMP_1_SHIFT) | \
	(c0 << AD_COMP_0_SHIFT) | (size << AD_PIXEL_S_SHIFT))

unsigned int mpc8610hpcd_get_pixel_format(unsigned int bits_per_pixel,
						int monitor_port)
{
	static const unsigned long pixelformat[][3] = {
		{
			MAKE_AD(3, 0, 2, 1, 3, 8, 8, 8, 8),
			MAKE_AD(4, 2, 0, 1, 2, 8, 8, 8, 0),
			MAKE_AD(4, 0, 2, 1, 1, 5, 6, 5, 0)
		},
		{
			MAKE_AD(3, 2, 0, 1, 3, 8, 8, 8, 8),
			MAKE_AD(4, 0, 2, 1, 2, 8, 8, 8, 0),
			MAKE_AD(4, 2, 0, 1, 1, 5, 6, 5, 0)
		},
	};
	unsigned int arch_monitor;

	/* The DVI port is mis-wired on revision 1 of this board. */
	arch_monitor = ((*pixis_arch == 0x01) && (monitor_port == 0))? 0 : 1;

	switch (bits_per_pixel) {
	case 32:
		return pixelformat[arch_monitor][0];
	case 24:
		return pixelformat[arch_monitor][1];
	case 16:
		return pixelformat[arch_monitor][2];
	default:
		pr_err("fsl-diu: unsupported pixel depth %u\n", bits_per_pixel);
		return 0;
	}
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
	speed_ccb = fsl_get_sys_freq();

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

	pixis_node = of_find_compatible_node(NULL, NULL, "fsl,fpga-pixis");
	if (pixis_node) {
		of_address_to_resource(pixis_node, 0, &r);
		of_node_put(pixis_node);
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
