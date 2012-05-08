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
#include <asm/udbg.h>
#include <asm/fsl_guts.h>
#include "smp.h"

#include "mpc85xx.h"

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)

#define PMUXCR_ELBCDIU_MASK	0xc0000000
#define PMUXCR_ELBCDIU_NOR16	0x80000000
#define PMUXCR_ELBCDIU_DIU	0x40000000

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
#define PX_CTL		3
#define PX_BRDCFG0	8
#define PX_BRDCFG1	9

#define PX_BRDCFG0_ELBC_SPI_MASK	0xc0
#define PX_BRDCFG0_ELBC_SPI_ELBC	0x00
#define PX_BRDCFG0_ELBC_SPI_NULL	0xc0
#define PX_BRDCFG0_ELBC_DIU		0x02

#define PX_BRDCFG1_DVIEN	0x80
#define PX_BRDCFG1_DFPEN	0x40
#define PX_BRDCFG1_BACKLIGHT	0x20
#define PX_BRDCFG1_DDCEN	0x10

#define PX_CTL_ALTACC		0x80

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
static u32 p1022ds_get_pixel_format(enum fsl_diu_monitor_port port,
				    unsigned int bits_per_pixel)
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
static void p1022ds_set_gamma_table(enum fsl_diu_monitor_port port,
				    char *gamma_table_base)
{
}

/**
 * p1022ds_set_monitor_port: switch the output to a different monitor port
 *
 */
static void p1022ds_set_monitor_port(enum fsl_diu_monitor_port port)
{
	struct device_node *guts_node;
	struct device_node *indirect_node = NULL;
	struct ccsr_guts __iomem *guts;
	u8 __iomem *lbc_lcs0_ba = NULL;
	u8 __iomem *lbc_lcs1_ba = NULL;
	u8 b;

	/* Map the global utilities registers. */
	guts_node = of_find_compatible_node(NULL, NULL, "fsl,p1022-guts");
	if (!guts_node) {
		pr_err("p1022ds: missing global utilties device node\n");
		return;
	}

	guts = of_iomap(guts_node, 0);
	if (!guts) {
		pr_err("p1022ds: could not map global utilties device\n");
		goto exit;
	}

	indirect_node = of_find_compatible_node(NULL, NULL,
					     "fsl,p1022ds-indirect-pixis");
	if (!indirect_node) {
		pr_err("p1022ds: missing pixis indirect mode node\n");
		goto exit;
	}

	lbc_lcs0_ba = of_iomap(indirect_node, 0);
	if (!lbc_lcs0_ba) {
		pr_err("p1022ds: could not map localbus chip select 0\n");
		goto exit;
	}

	lbc_lcs1_ba = of_iomap(indirect_node, 1);
	if (!lbc_lcs1_ba) {
		pr_err("p1022ds: could not map localbus chip select 1\n");
		goto exit;
	}

	/* Make sure we're in indirect mode first. */
	if ((in_be32(&guts->pmuxcr) & PMUXCR_ELBCDIU_MASK) !=
	    PMUXCR_ELBCDIU_DIU) {
		struct device_node *pixis_node;
		void __iomem *pixis;

		pixis_node =
			of_find_compatible_node(NULL, NULL, "fsl,p1022ds-fpga");
		if (!pixis_node) {
			pr_err("p1022ds: missing pixis node\n");
			goto exit;
		}

		pixis = of_iomap(pixis_node, 0);
		of_node_put(pixis_node);
		if (!pixis) {
			pr_err("p1022ds: could not map pixis registers\n");
			goto exit;
		}

		/* Enable indirect PIXIS mode.  */
		setbits8(pixis + PX_CTL, PX_CTL_ALTACC);
		iounmap(pixis);

		/* Switch the board mux to the DIU */
		out_8(lbc_lcs0_ba, PX_BRDCFG0);	/* BRDCFG0 */
		b = in_8(lbc_lcs1_ba);
		b |= PX_BRDCFG0_ELBC_DIU;
		out_8(lbc_lcs1_ba, b);

		/* Set the chip mux to DIU mode. */
		clrsetbits_be32(&guts->pmuxcr, PMUXCR_ELBCDIU_MASK,
				PMUXCR_ELBCDIU_DIU);
		in_be32(&guts->pmuxcr);
	}


	switch (port) {
	case FSL_DIU_PORT_DVI:
		/* Enable the DVI port, disable the DFP and the backlight */
		out_8(lbc_lcs0_ba, PX_BRDCFG1);
		b = in_8(lbc_lcs1_ba);
		b &= ~(PX_BRDCFG1_DFPEN | PX_BRDCFG1_BACKLIGHT);
		b |= PX_BRDCFG1_DVIEN;
		out_8(lbc_lcs1_ba, b);
		break;
	case FSL_DIU_PORT_LVDS:
		/*
		 * LVDS also needs backlight enabled, otherwise the display
		 * will be blank.
		 */
		/* Enable the DFP port, disable the DVI and the backlight */
		out_8(lbc_lcs0_ba, PX_BRDCFG1);
		b = in_8(lbc_lcs1_ba);
		b &= ~PX_BRDCFG1_DVIEN;
		b |= PX_BRDCFG1_DFPEN | PX_BRDCFG1_BACKLIGHT;
		out_8(lbc_lcs1_ba, b);
		break;
	default:
		pr_err("p1022ds: unsupported monitor port %i\n", port);
	}

exit:
	if (lbc_lcs1_ba)
		iounmap(lbc_lcs1_ba);
	if (lbc_lcs0_ba)
		iounmap(lbc_lcs0_ba);
	if (guts)
		iounmap(guts);

	of_node_put(indirect_node);
	of_node_put(guts_node);
}

/**
 * p1022ds_set_pixel_clock: program the DIU's clock
 *
 * @pixclock: the wavelength, in picoseconds, of the clock
 */
void p1022ds_set_pixel_clock(unsigned int pixclock)
{
	struct device_node *guts_np = NULL;
	struct ccsr_guts __iomem *guts;
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

	/*
	 * 'pxclk' is the ratio of the platform clock to the pixel clock.
	 * This number is programmed into the CLKDVDR register, and the valid
	 * range of values is 2-255.
	 */
	pxclk = DIV_ROUND_CLOSEST(fsl_get_sys_freq(), freq);
	pxclk = clamp_t(u32, pxclk, 2, 255);

	/* Disable the pixel clock, and set it to non-inverted and no delay */
	clrbits32(&guts->clkdvdr,
		  CLKDVDR_PXCKEN | CLKDVDR_PXCKDLY | CLKDVDR_PXCLK_MASK);

	/* Enable the clock and set the pxclk */
	setbits32(&guts->clkdvdr, CLKDVDR_PXCKEN | (pxclk << 16));

	iounmap(guts);
}

/**
 * p1022ds_valid_monitor_port: set the monitor port for sysfs
 */
enum fsl_diu_monitor_port
p1022ds_valid_monitor_port(enum fsl_diu_monitor_port port)
{
	switch (port) {
	case FSL_DIU_PORT_DVI:
	case FSL_DIU_PORT_LVDS:
		return port;
	default:
		return FSL_DIU_PORT_DVI; /* Dual-link LVDS is not supported */
	}
}

#endif

void __init p1022_ds_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
		MPIC_SINGLE_DEST_CPU,
		0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);
}

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)

/*
 * Disables a node in the device tree.
 *
 * This function is called before kmalloc() is available, so the 'new' object
 * should be allocated in the global area.  The easiest way is to do that is
 * to allocate one static local variable for each call to this function.
 */
static void __init disable_one_node(struct device_node *np, struct property *new)
{
	struct property *old;

	old = of_find_property(np, new->name, NULL);
	if (old)
		prom_update_property(np, new, old);
	else
		prom_add_property(np, new);
}

/* TRUE if there is a "video=fslfb" command-line parameter. */
static bool fslfb;

/*
 * Search for a "video=fslfb" command-line parameter, and set 'fslfb' to
 * true if we find it.
 *
 * We need to use early_param() instead of __setup() because the normal
 * __setup() gets called to late.  However, early_param() gets called very
 * early, before the device tree is unflattened, so all we can do now is set a
 * global variable.  Later on, p1022_ds_setup_arch() will use that variable
 * to determine if we need to update the device tree.
 */
static int __init early_video_setup(char *options)
{
	fslfb = (strncmp(options, "fslfb:", 6) == 0);

	return 0;
}
early_param("video", early_video_setup);

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
	diu_ops.valid_monitor_port	= p1022ds_valid_monitor_port;

	/*
	 * Disable the NOR flash node if there is video=fslfb... command-line
	 * parameter.  When the DIU is active, NOR flash is unavailable, so we
	 * have to disable the node before the MTD driver loads.
	 */
	if (fslfb) {
		struct device_node *np =
			of_find_compatible_node(NULL, NULL, "fsl,p1022-elbc");

		if (np) {
			np = of_find_compatible_node(np, NULL, "cfi-flash");
			if (np) {
				static struct property nor_status = {
					.name = "status",
					.value = "disabled",
					.length = sizeof("disabled"),
				};

				pr_info("p1022ds: disabling %s node",
					np->full_name);
				disable_one_node(np, &nor_status);
				of_node_put(np);
			}
		}

	}

#endif

	mpc85xx_smp_init();

#ifdef CONFIG_SWIOTLB
	if (memblock_end_of_DRAM() > max) {
		ppc_swiotlb_enable = 1;
		set_pci_dma_ops(&swiotlb_dma_ops);
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_swiotlb;
	}
#endif

	pr_info("Freescale P1022 DS reference board\n");
}

machine_device_initcall(p1022_ds, mpc85xx_common_publish_devices);

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
