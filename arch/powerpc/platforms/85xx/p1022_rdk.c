/*
 * P1022 RDK board specific routines
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Based on p1022_ds.c
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

/* DIU Pixel Clock bits of the CLKDVDR Global Utilities register */
#define CLKDVDR_PXCKEN		0x80000000
#define CLKDVDR_PXCKINV		0x10000000
#define CLKDVDR_PXCKDLY		0x06000000
#define CLKDVDR_PXCLK_MASK	0x00FF0000

/**
 * p1022rdk_set_monitor_port: switch the output to a different monitor port
 */
static void p1022rdk_set_monitor_port(enum fsl_diu_monitor_port port)
{
	if (port != FSL_DIU_PORT_DVI) {
		pr_err("p1022rdk: unsupported monitor port %i\n", port);
		return;
	}
}

/**
 * p1022rdk_set_pixel_clock: program the DIU's clock
 *
 * @pixclock: the wavelength, in picoseconds, of the clock
 */
void p1022rdk_set_pixel_clock(unsigned int pixclock)
{
	struct device_node *guts_np = NULL;
	struct ccsr_guts __iomem *guts;
	unsigned long freq;
	u64 temp;
	u32 pxclk;

	/* Map the global utilities registers. */
	guts_np = of_find_compatible_node(NULL, NULL, "fsl,p1022-guts");
	if (!guts_np) {
		pr_err("p1022rdk: missing global utilties device node\n");
		return;
	}

	guts = of_iomap(guts_np, 0);
	of_node_put(guts_np);
	if (!guts) {
		pr_err("p1022rdk: could not map global utilties device\n");
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
 * p1022rdk_valid_monitor_port: set the monitor port for sysfs
 */
enum fsl_diu_monitor_port
p1022rdk_valid_monitor_port(enum fsl_diu_monitor_port port)
{
	return FSL_DIU_PORT_DVI;
}

#endif

void __init p1022_rdk_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
		MPIC_SINGLE_DEST_CPU,
		0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);
}

/*
 * Setup the architecture
 */
static void __init p1022_rdk_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif
	dma_addr_t max = 0xffffffff;

	if (ppc_md.progress)
		ppc_md.progress("p1022_rdk_setup_arch()", 0);

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
	diu_ops.set_monitor_port	= p1022rdk_set_monitor_port;
	diu_ops.set_pixel_clock		= p1022rdk_set_pixel_clock;
	diu_ops.valid_monitor_port	= p1022rdk_valid_monitor_port;
#endif

	mpc85xx_smp_init();

#ifdef CONFIG_SWIOTLB
	if ((memblock_end_of_DRAM() - 1) > max) {
		ppc_swiotlb_enable = 1;
		set_pci_dma_ops(&swiotlb_dma_ops);
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_swiotlb;
	}
#endif

	pr_info("Freescale / iVeia P1022 RDK reference board\n");
}

machine_device_initcall(p1022_rdk, mpc85xx_common_publish_devices);

machine_arch_initcall(p1022_rdk, swiotlb_setup_bus_notifier);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init p1022_rdk_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,p1022rdk");
}

define_machine(p1022_rdk) {
	.name			= "P1022 RDK",
	.probe			= p1022_rdk_probe,
	.setup_arch		= p1022_rdk_setup_arch,
	.init_IRQ		= p1022_rdk_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
