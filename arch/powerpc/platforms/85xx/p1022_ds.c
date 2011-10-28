/*
 * P1022DS board specific routines
 *
 * Authors: Travis Wheatley <travis.wheatley@freescale.com>
 *          Dave Liu <daveliu@freescale.com>
 *          Timur Tabi <timur@freescale.com>
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This file is taken from the Freescale P1022DS BSP, with modifications:
 * 2) No AMP support
 * 3) No PCI endpoint support
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/pci.h>
#include <linux/of_platform.h>
#include <linux/memblock.h>
#include <asm/div64.h>
#include <asm/mpic.h>
#include <asm/swiotlb.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include <asm/fsl_guts.h>

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)

/*
 * Board-specific initialization of the DIU.  This code should probably be
 * executed when the DIU is opened, rather than in arch code, but the DIU
 * driver does not have a mechanism for this (yet).
 *
 * This is especially problematic on the P1022DS because the local bus (eLBC)
 * and the DIU video signals share the same pins, which means that enabling the
 * DIU will disable access to NOR flash.
 */

/* DIU Pixel Clock bits of the CLKDVDR Global Utilities register */
#define CLKDVDR_PXCKEN		0x80000000
#define CLKDVDR_PXCKINV		0x10000000
#define CLKDVDR_PXCKDLY		0x06000000
#define CLKDVDR_PXCLK_MASK	0x00FF0000

/* Some ngPIXIS register definitions */
#define PX_BRDCFG1_DVIEN	0x80
#define PX_BRDCFG1_DFPEN	0x40
#define PX_BRDCFG1_BACKLIGHT	0x20
#define PX_BRDCFG1_DDCEN	0x10

/*
 * DIU Area Descriptor
 *
 * Note that we need to byte-swap the value before it's written to the AD
 * register.  So even though the registers don't look like they're in the same
 * bit positions as they are on the MPC8610, the same value is written to the
 * AD register on the MPC8610 and on the P1022.
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

/**
 * p1022ds_get_pixel_format: return the Area Descriptor for a given pixel depth
 *
 * The Area Descriptor is a 32-bit value that determine which bits in each
 * pixel are to be used for each color.
 */
static unsigned int p1022ds_get_pixel_format(unsigned int bits_per_pixel,
	int monitor_port)
{
	switch (bits_per_pixel) {
	case 32:
		/* 0x88883316 */
		return MAKE_AD(3, 2, 0, 1, 3, 8, 8, 8, 8);
	case 24:
		/* 0x88082219 */
		return MAKE_AD(4, 0, 1, 2, 2, 0, 8, 8, 8);
	case 16:
		/* 0x65053118 */
		return MAKE_AD(4, 2, 1, 0, 1, 5, 6, 5, 0);
	default:
		pr_err("fsl-diu: unsupported pixel depth %u\n", bits_per_pixel);
		return 0;
	}
}

/**
 * p1022ds_set_gamma_table: update the gamma table, if necessary
 *
 * On some boards, the gamma table for some ports may need to be modified.
 * This is not the case on the P1022DS, so we do nothing.
*/
static void p1022ds_set_gamma_table(int monitor_port, char *gamma_table_base)
{
}

/**
 * p1022ds_set_monitor_port: switch the output to a different monitor port
 *
 */
static void p1022ds_set_monitor_port(int monitor_port)
{
	struct device_node *pixis_node;
	u8 __iomem *brdcfg1;

	pixis_node = of_find_compatible_node(NULL, NULL, "fsl,p1022ds-pixis");
	if (!pixis_node) {
		pr_err("p1022ds: missing ngPIXIS node\n");
		return;
	}

	brdcfg1 = of_iomap(pixis_node, 0);
	if (!brdcfg1) {
		pr_err("p1022ds: could not map ngPIXIS registers\n");
		return;
	}
	brdcfg1 += 9;	/* BRDCFG1 is at offset 9 in the ngPIXIS */

	switch (monitor_port) {
	case 0: /* DVI */
		/* Enable the DVI port, disable the DFP and the backlight */
		clrsetbits_8(brdcfg1, PX_BRDCFG1_DFPEN | PX_BRDCFG1_BACKLIGHT,
			     PX_BRDCFG1_DVIEN);
		break;
	case 1: /* Single link LVDS */
		/* Enable the DFP port, disable the DVI and the backlight */
		clrsetbits_8(brdcfg1, PX_BRDCFG1_DVIEN | PX_BRDCFG1_BACKLIGHT,
			     PX_BRDCFG1_DFPEN);
		break;
	default:
		pr_err("p1022ds: unsupported monitor port %i\n", monitor_port);
	}
}

/**
 * p1022ds_set_pixel_clock: program the DIU's clock
 *
 * @pixclock: the wavelength, in picoseconds, of the clock
 */
void p1022ds_set_pixel_clock(unsigned int pixclock)
{
	struct device_node *guts_np = NULL;
	struct ccsr_guts_85xx __iomem *guts;
	unsigned long freq;
	u64 temp;
	u32 pxclk;

	/* Map the global utilities registers. */
	guts_np = of_find_compatible_node(NULL, NULL, "fsl,p1022-guts");
	if (!guts_np) {
		pr_err("p1022ds: missing global utilties device node\n");
		return;
	}

	guts = of_iomap(guts_np, 0);
	of_node_put(guts_np);
	if (!guts) {
		pr_err("p1022ds: could not map global utilties device\n");
		return;
	}

	/* Convert pixclock from a wavelength to a frequency */
	temp = 1000000000000ULL;
	do_div(temp, pixclock);
	freq = temp;

	/* pixclk is the ratio of the platform clock to the pixel clock */
	pxclk = DIV_ROUND_CLOSEST(fsl_get_sys_freq(), freq);

	/* Disable the pixel clock, and set it to non-inverted and no delay */
	clrbits32(&guts->clkdvdr,
		  CLKDVDR_PXCKEN | CLKDVDR_PXCKDLY | CLKDVDR_PXCLK_MASK);

	/* Enable the clock and set the pxclk */
	setbits32(&guts->clkdvdr, CLKDVDR_PXCKEN | (pxclk << 16));
}

/**
 * p1022ds_show_monitor_port: show the current monitor
 *
 * This function returns a string indicating whether the current monitor is
 * set to DVI or LVDS.
 */
ssize_t p1022ds_show_monitor_port(int monitor_port, char *buf)
{
	return sprintf(buf, "%c0 - DVI\n%c1 - Single link LVDS\n",
		monitor_port == 0 ? '*' : ' ', monitor_port == 1 ? '*' : ' ');
}

/**
 * p1022ds_set_sysfs_monitor_port: set the monitor port for sysfs
 */
int p1022ds_set_sysfs_monitor_port(int val)
{
	return val < 2 ? val : 0;
}

#endif

void __init p1022_ds_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np;

	np = of_find_node_by_type(NULL, "open-pic");
	if (!np) {
		pr_err("Could not find open-pic node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &r)) {
		pr_err("Failed to map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
		MPIC_PRIMARY | MPIC_WANTS_RESET |
		MPIC_BIG_ENDIAN | MPIC_BROKEN_FRR_NIRQS |
		MPIC_SINGLE_DEST_CPU,
		0, 256, " OpenPIC  ");

	BUG_ON(mpic == NULL);
	of_node_put(np);

	mpic_init(mpic);
}

#ifdef CONFIG_SMP
void __init mpc85xx_smp_init(void);
#endif

/*
 * Setup the architecture
 */
static void __init p1022_ds_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif
	dma_addr_t max = 0xffffffff;

	if (ppc_md.progress)
		ppc_md.progress("p1022_ds_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,p1022-pcie") {
		struct resource rsrc;
		struct pci_controller *hose;

		of_address_to_resource(np, 0, &rsrc);

		if ((rsrc.start & 0xfffff) == 0x8000)
			fsl_add_bridge(np, 1);
		else
			fsl_add_bridge(np, 0);

		hose = pci_find_hose_for_OF_device(np);
		max = min(max, hose->dma_window_base_cur +
			  hose->dma_window_size);
	}
#endif

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)
	diu_ops.get_pixel_format	= p1022ds_get_pixel_format;
	diu_ops.set_gamma_table		= p1022ds_set_gamma_table;
	diu_ops.set_monitor_port	= p1022ds_set_monitor_port;
	diu_ops.set_pixel_clock		= p1022ds_set_pixel_clock;
	diu_ops.show_monitor_port	= p1022ds_show_monitor_port;
	diu_ops.set_sysfs_monitor_port	= p1022ds_set_sysfs_monitor_port;
#endif

#ifdef CONFIG_SMP
	mpc85xx_smp_init();
#endif

#ifdef CONFIG_SWIOTLB
	if (memblock_end_of_DRAM() > max) {
		ppc_swiotlb_enable = 1;
		set_pci_dma_ops(&swiotlb_dma_ops);
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_swiotlb;
	}
#endif

	pr_info("Freescale P1022 DS reference board\n");
}

static struct of_device_id __initdata p1022_ds_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{ .compatible = "gianfar", },
	/* So that the DMA channel nodes can be probed individually: */
	{ .compatible = "fsl,eloplus-dma", },
	{},
};

static int __init p1022_ds_publish_devices(void)
{
	return of_platform_bus_probe(NULL, p1022_ds_ids, NULL);
}
machine_device_initcall(p1022_ds, p1022_ds_publish_devices);

machine_arch_initcall(p1022_ds, swiotlb_setup_bus_notifier);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init p1022_ds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,p1022ds");
}

define_machine(p1022_ds) {
	.name			= "P1022 DS",
	.probe			= p1022_ds_probe,
	.setup_arch		= p1022_ds_setup_arch,
	.init_IRQ		= p1022_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
