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

#include <linux/fsl/guts.h>
#include <linux/pci.h>
#include <linux/of_platform.h>
#include <asm/div64.h>
#include <asm/mpic.h>
#include <asm/swiotlb.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include <asm/udbg.h>
#include <asm/fsl_lbc.h>
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

struct fsl_law {
	u32	lawbar;
	u32	reserved1;
	u32	lawar;
	u32	reserved[5];
};

#define LAWBAR_MASK	0x00F00000
#define LAWBAR_SHIFT	12

#define LAWAR_EN	0x80000000
#define LAWAR_TGT_MASK	0x01F00000
#define LAW_TRGT_IF_LBC	(0x04 << 20)

#define LAWAR_MASK	(LAWAR_EN | LAWAR_TGT_MASK)
#define LAWAR_MATCH	(LAWAR_EN | LAW_TRGT_IF_LBC)

#define BR_BA		0xFFFF8000

/*
 * Map a BRx value to a physical address
 *
 * The localbus BRx registers only store the lower 32 bits of the address.  To
 * obtain the upper four bits, we need to scan the LAW table.  The entry which
 * maps to the localbus will contain the upper four bits.
 */
static phys_addr_t lbc_br_to_phys(const void *ecm, unsigned int count, u32 br)
{
#ifndef CONFIG_PHYS_64BIT
	/*
	 * If we only have 32-bit addressing, then the BRx address *is* the
	 * physical address.
	 */
	return br & BR_BA;
#else
	const struct fsl_law *law = ecm + 0xc08;
	unsigned int i;

	for (i = 0; i < count; i++) {
		u64 lawbar = in_be32(&law[i].lawbar);
		u32 lawar = in_be32(&law[i].lawar);

		if ((lawar & LAWAR_MASK) == LAWAR_MATCH)
			/* Extract the upper four bits */
			return (br & BR_BA) | ((lawbar & LAWBAR_MASK) << 12);
	}

	return 0;
#endif
}

/**
 * p1022ds_set_monitor_port: switch the output to a different monitor port
 */
static void p1022ds_set_monitor_port(enum fsl_diu_monitor_port port)
{
	struct device_node *guts_node;
	struct device_node *lbc_node = NULL;
	struct device_node *law_node = NULL;
	struct ccsr_guts __iomem *guts;
	struct fsl_lbc_regs *lbc = NULL;
	void *ecm = NULL;
	u8 __iomem *lbc_lcs0_ba = NULL;
	u8 __iomem *lbc_lcs1_ba = NULL;
	phys_addr_t cs0_addr, cs1_addr;
	u32 br0, or0, br1, or1;
	const __be32 *iprop;
	unsigned int num_laws;
	u8 b;

	/* Map the global utilities registers. */
	guts_node = of_find_compatible_node(NULL, NULL, "fsl,p1022-guts");
	if (!guts_node) {
		pr_err("p1022ds: missing global utilities device node\n");
		return;
	}

	guts = of_iomap(guts_node, 0);
	if (!guts) {
		pr_err("p1022ds: could not map global utilities device\n");
		goto exit;
	}

	lbc_node = of_find_compatible_node(NULL, NULL, "fsl,p1022-elbc");
	if (!lbc_node) {
		pr_err("p1022ds: missing localbus node\n");
		goto exit;
	}

	lbc = of_iomap(lbc_node, 0);
	if (!lbc) {
		pr_err("p1022ds: could not map localbus node\n");
		goto exit;
	}

	law_node = of_find_compatible_node(NULL, NULL, "fsl,ecm-law");
	if (!law_node) {
		pr_err("p1022ds: missing local access window node\n");
		goto exit;
	}

	ecm = of_iomap(law_node, 0);
	if (!ecm) {
		pr_err("p1022ds: could not map local access window node\n");
		goto exit;
	}

	iprop = of_get_property(law_node, "fsl,num-laws", NULL);
	if (!iprop) {
		pr_err("p1022ds: LAW node is missing fsl,num-laws property\n");
		goto exit;
	}
	num_laws = be32_to_cpup(iprop);

	/*
	 * Indirect mode requires both BR0 and BR1 to be set to "GPCM",
	 * otherwise writes to these addresses won't actually appear on the
	 * local bus, and so the PIXIS won't see them.
	 *
	 * In FCM mode, writes go to the NAND controller, which does not pass
	 * them to the localbus directly.  So we force BR0 and BR1 into GPCM
	 * mode, since we don't care about what's behind the localbus any
	 * more.
	 */
	br0 = in_be32(&lbc->bank[0].br);
	br1 = in_be32(&lbc->bank[1].br);
	or0 = in_be32(&lbc->bank[0].or);
	or1 = in_be32(&lbc->bank[1].or);

	/* Make sure CS0 and CS1 are programmed */
	if (!(br0 & BR_V) || !(br1 & BR_V)) {
		pr_err("p1022ds: CS0 and/or CS1 is not programmed\n");
		goto exit;
	}

	/*
	 * Use the existing BRx/ORx values if it's already GPCM. Otherwise,
	 * force the values to simple 32KB GPCM windows with the most
	 * conservative timing.
	 */
	if ((br0 & BR_MSEL) != BR_MS_GPCM) {
		br0 = (br0 & BR_BA) | BR_V;
		or0 = 0xFFFF8000 | 0xFF7;
		out_be32(&lbc->bank[0].br, br0);
		out_be32(&lbc->bank[0].or, or0);
	}
	if ((br1 & BR_MSEL) != BR_MS_GPCM) {
		br1 = (br1 & BR_BA) | BR_V;
		or1 = 0xFFFF8000 | 0xFF7;
		out_be32(&lbc->bank[1].br, br1);
		out_be32(&lbc->bank[1].or, or1);
	}

	cs0_addr = lbc_br_to_phys(ecm, num_laws, br0);
	if (!cs0_addr) {
		pr_err("p1022ds: could not determine physical address for CS0"
		       " (BR0=%08x)\n", br0);
		goto exit;
	}
	cs1_addr = lbc_br_to_phys(ecm, num_laws, br1);
	if (!cs1_addr) {
		pr_err("p1022ds: could not determine physical address for CS1"
		       " (BR1=%08x)\n", br1);
		goto exit;
	}

	lbc_lcs0_ba = ioremap(cs0_addr, 1);
	if (!lbc_lcs0_ba) {
		pr_err("p1022ds: could not ioremap CS0 address %llx\n",
		       (unsigned long long)cs0_addr);
		goto exit;
	}
	lbc_lcs1_ba = ioremap(cs1_addr, 1);
	if (!lbc_lcs1_ba) {
		pr_err("p1022ds: could not ioremap CS1 address %llx\n",
		       (unsigned long long)cs1_addr);
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
	if (lbc)
		iounmap(lbc);
	if (ecm)
		iounmap(ecm);
	if (guts)
		iounmap(guts);

	of_node_put(law_node);
	of_node_put(lbc_node);
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
		pr_err("p1022ds: missing global utilities device node\n");
		return;
	}

	guts = of_iomap(guts_np, 0);
	of_node_put(guts_np);
	if (!guts) {
		pr_err("p1022ds: could not map global utilities device\n");
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
	if (ppc_md.progress)
		ppc_md.progress("p1022_ds_setup_arch()", 0);

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)
	diu_ops.set_monitor_port	= p1022ds_set_monitor_port;
	diu_ops.set_pixel_clock		= p1022ds_set_pixel_clock;
	diu_ops.valid_monitor_port	= p1022ds_valid_monitor_port;

	/*
	 * Disable the NOR and NAND flash nodes if there is video=fslfb...
	 * command-line parameter.  When the DIU is active, the localbus is
	 * unavailable, so we have to disable these nodes before the MTD
	 * driver loads.
	 */
	if (fslfb) {
		struct device_node *np =
			of_find_compatible_node(NULL, NULL, "fsl,p1022-elbc");

		if (np) {
			struct device_node *np2;

			of_node_get(np);
			np2 = of_find_compatible_node(np, NULL, "cfi-flash");
			if (np2) {
				static struct property nor_status = {
					.name = "status",
					.value = "disabled",
					.length = sizeof("disabled"),
				};

				/*
				 * of_update_property() is called before
				 * kmalloc() is available, so the 'new' object
				 * should be allocated in the global area.
				 * The easiest way is to do that is to
				 * allocate one static local variable for each
				 * call to this function.
				 */
				pr_info("p1022ds: disabling %s node",
					np2->full_name);
				of_update_property(np2, &nor_status);
				of_node_put(np2);
			}

			of_node_get(np);
			np2 = of_find_compatible_node(np, NULL,
						      "fsl,elbc-fcm-nand");
			if (np2) {
				static struct property nand_status = {
					.name = "status",
					.value = "disabled",
					.length = sizeof("disabled"),
				};

				pr_info("p1022ds: disabling %s node",
					np2->full_name);
				of_update_property(np2, &nand_status);
				of_node_put(np2);
			}

			of_node_put(np);
		}

	}

#endif

	mpc85xx_smp_init();

	fsl_pci_assign_primary();

	swiotlb_detect_4g();

	pr_info("Freescale P1022 DS reference board\n");
}

machine_arch_initcall(p1022_ds, mpc85xx_common_publish_devices);

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
	.pcibios_fixup_phb	= fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
