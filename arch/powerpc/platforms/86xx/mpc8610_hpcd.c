// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/fsl/guts.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <asm/mpic.h>

#include <linux/of_platform.h>
#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"

static struct device_node *pixis_node;
static unsigned char *pixis_bdcfg0, *pixis_arch;

/* DIU Pixel Clock bits of the CLKDVDR Global Utilities register */
#define CLKDVDR_PXCKEN		0x80000000
#define CLKDVDR_PXCKINV		0x10000000
#define CLKDVDR_PXCKDLY		0x06000000
#define CLKDVDR_PXCLK_MASK	0x001F0000

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

static const struct of_device_id mpc8610_ids[] __initconst = {
	{ .compatible = "fsl,mpc8610-immr", },
	{ .compatible = "fsl,mpc8610-guts", },
	/* So that the DMA channel nodes can be probed individually: */
	{ .compatible = "fsl,eloplus-dma", },
	/* PCI controllers */
	{ .compatible = "fsl,mpc8610-pci", },
	{}
};

static int __init mpc8610_declare_of_platform_devices(void)
{
	/* Enable wakeup on PIXIS' event IRQ. */
	mpc8610_suspend_init();

	mpc86xx_common_publish_devices();

	/* Without this call, the SSI device driver won't get probed. */
	of_platform_bus_probe(NULL, mpc8610_ids, NULL);

	return 0;
}
machine_arch_initcall(mpc86xx_hpcd, mpc8610_declare_of_platform_devices);

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

u32 mpc8610hpcd_get_pixel_format(enum fsl_diu_monitor_port port,
				 unsigned int bits_per_pixel)
{
	static const u32 pixelformat[][3] = {
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
	arch_monitor =
		((*pixis_arch == 0x01) && (port == FSL_DIU_PORT_DVI)) ? 0 : 1;

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

void mpc8610hpcd_set_gamma_table(enum fsl_diu_monitor_port port,
				 char *gamma_table_base)
{
	int i;
	if (port == FSL_DIU_PORT_DLVDS) {
		for (i = 0; i < 256*3; i++)
			gamma_table_base[i] = (gamma_table_base[i] << 2) |
					 ((gamma_table_base[i] >> 6) & 0x03);
	}
}

#define PX_BRDCFG0_DVISEL	(1 << 3)
#define PX_BRDCFG0_DLINK	(1 << 4)
#define PX_BRDCFG0_DIU_MASK	(PX_BRDCFG0_DVISEL | PX_BRDCFG0_DLINK)

void mpc8610hpcd_set_monitor_port(enum fsl_diu_monitor_port port)
{
	switch (port) {
	case FSL_DIU_PORT_DVI:
		clrsetbits_8(pixis_bdcfg0, PX_BRDCFG0_DIU_MASK,
			     PX_BRDCFG0_DVISEL | PX_BRDCFG0_DLINK);
		break;
	case FSL_DIU_PORT_LVDS:
		clrsetbits_8(pixis_bdcfg0, PX_BRDCFG0_DIU_MASK,
			     PX_BRDCFG0_DLINK);
		break;
	case FSL_DIU_PORT_DLVDS:
		clrbits8(pixis_bdcfg0, PX_BRDCFG0_DIU_MASK);
		break;
	}
}

/**
 * mpc8610hpcd_set_pixel_clock: program the DIU's clock
 *
 * @pixclock: the wavelength, in picoseconds, of the clock
 */
void mpc8610hpcd_set_pixel_clock(unsigned int pixclock)
{
	struct device_node *guts_np = NULL;
	struct ccsr_guts __iomem *guts;
	unsigned long freq;
	u64 temp;
	u32 pxclk;

	/* Map the global utilities registers. */
	guts_np = of_find_compatible_node(NULL, NULL, "fsl,mpc8610-guts");
	if (!guts_np) {
		pr_err("mpc8610hpcd: missing global utilities device node\n");
		return;
	}

	guts = of_iomap(guts_np, 0);
	of_node_put(guts_np);
	if (!guts) {
		pr_err("mpc8610hpcd: could not map global utilities device\n");
		return;
	}

	/* Convert pixclock from a wavelength to a frequency */
	temp = 1000000000000ULL;
	do_div(temp, pixclock);
	freq = temp;

	/*
	 * 'pxclk' is the ratio of the platform clock to the pixel clock.
	 * On the MPC8610, the value programmed into CLKDVDR is the ratio
	 * minus one.  The valid range of values is 2-31.
	 */
	pxclk = DIV_ROUND_CLOSEST(fsl_get_sys_freq(), freq) - 1;
	pxclk = clamp_t(u32, pxclk, 2, 31);

	/* Disable the pixel clock, and set it to non-inverted and no delay */
	clrbits32(&guts->clkdvdr,
		  CLKDVDR_PXCKEN | CLKDVDR_PXCKDLY | CLKDVDR_PXCLK_MASK);

	/* Enable the clock and set the pxclk */
	setbits32(&guts->clkdvdr, CLKDVDR_PXCKEN | (pxclk << 16));

	iounmap(guts);
}

enum fsl_diu_monitor_port
mpc8610hpcd_valid_monitor_port(enum fsl_diu_monitor_port port)
{
	return port;
}

#endif

static void __init mpc86xx_hpcd_setup_arch(void)
{
	struct resource r;
	unsigned char *pixis;

	if (ppc_md.progress)
		ppc_md.progress("mpc86xx_hpcd_setup_arch()", 0);

	fsl_pci_assign_primary();

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)
	diu_ops.get_pixel_format	= mpc8610hpcd_get_pixel_format;
	diu_ops.set_gamma_table		= mpc8610hpcd_set_gamma_table;
	diu_ops.set_monitor_port	= mpc8610hpcd_set_monitor_port;
	diu_ops.set_pixel_clock		= mpc8610hpcd_set_pixel_clock;
	diu_ops.valid_monitor_port	= mpc8610hpcd_valid_monitor_port;
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
	if (of_machine_is_compatible("fsl,MPC8610HPCD"))
		return 1;	/* Looks good */

	return 0;
}

define_machine(mpc86xx_hpcd) {
	.name			= "MPC86xx HPCD",
	.probe			= mpc86xx_hpcd_probe,
	.setup_arch		= mpc86xx_hpcd_setup_arch,
	.init_IRQ		= mpc86xx_init_irq,
	.get_irq		= mpic_get_irq,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
};
