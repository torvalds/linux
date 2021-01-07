// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Corenet based SoC DS Setup
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2009-2011 Freescale Semiconductor Inc.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pgtable.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/ehv_pic.h>
#include <asm/swiotlb.h>

#include <linux/of_platform.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "smp.h"
#include "mpc85xx.h"

void __init corenet_gen_pic_init(void)
{
	struct mpic *mpic;
	unsigned int flags = MPIC_BIG_ENDIAN | MPIC_SINGLE_DEST_CPU |
		MPIC_NO_RESET;

	if (ppc_md.get_irq == mpic_get_coreint_irq)
		flags |= MPIC_ENABLE_COREINT;

	mpic = mpic_alloc(NULL, 0, flags, 0, 512, " OpenPIC  ");
	BUG_ON(mpic == NULL);

	mpic_init(mpic);
}

/*
 * Setup the architecture
 */
void __init corenet_gen_setup_arch(void)
{
	mpc85xx_smp_init();

	swiotlb_detect_4g();

	pr_info("%s board\n", ppc_md.name);
}

static const struct of_device_id of_device_ids[] = {
	{
		.compatible	= "simple-bus"
	},
	{
		.compatible	= "mdio-mux-gpio"
	},
	{
		.compatible	= "fsl,fpga-ngpixis"
	},
	{
		.compatible	= "fsl,fpga-qixis"
	},
	{
		.compatible	= "fsl,srio",
	},
	{
		.compatible	= "fsl,p4080-pcie",
	},
	{
		.compatible	= "fsl,qoriq-pcie-v2.2",
	},
	{
		.compatible	= "fsl,qoriq-pcie-v2.3",
	},
	{
		.compatible	= "fsl,qoriq-pcie-v2.4",
	},
	{
		.compatible	= "fsl,qoriq-pcie-v3.0",
	},
	{
		.compatible	= "fsl,qe",
	},
	/* The following two are for the Freescale hypervisor */
	{
		.name		= "hypervisor",
	},
	{
		.name		= "handles",
	},
	{}
};

int __init corenet_gen_publish_devices(void)
{
	return of_platform_bus_probe(NULL, of_device_ids, NULL);
}
machine_arch_initcall(corenet_generic, corenet_gen_publish_devices);

static const char * const boards[] __initconst = {
	"fsl,P2041RDB",
	"fsl,P3041DS",
	"fsl,OCA4080",
	"fsl,P4080DS",
	"fsl,P5020DS",
	"fsl,P5040DS",
	"fsl,T2080QDS",
	"fsl,T2080RDB",
	"fsl,T2081QDS",
	"fsl,T4240QDS",
	"fsl,T4240RDB",
	"fsl,B4860QDS",
	"fsl,B4420QDS",
	"fsl,B4220QDS",
	"fsl,T1023RDB",
	"fsl,T1024QDS",
	"fsl,T1024RDB",
	"fsl,T1040D4RDB",
	"fsl,T1042D4RDB",
	"fsl,T1040QDS",
	"fsl,T1042QDS",
	"fsl,T1040RDB",
	"fsl,T1042RDB",
	"fsl,T1042RDB_PI",
	"keymile,kmcent2",
	"keymile,kmcoge4",
	"varisys,CYRUS",
	NULL
};

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init corenet_generic_probe(void)
{
	char hv_compat[24];
	int i;
#ifdef CONFIG_SMP
	extern struct smp_ops_t smp_85xx_ops;
#endif

	if (of_device_compatible_match(of_root, boards))
		return 1;

	/* Check if we're running under the Freescale hypervisor */
	for (i = 0; boards[i]; i++) {
		snprintf(hv_compat, sizeof(hv_compat), "%s-hv", boards[i]);
		if (of_machine_is_compatible(hv_compat)) {
			ppc_md.init_IRQ = ehv_pic_init;

			ppc_md.get_irq = ehv_pic_get_irq;
			ppc_md.restart = fsl_hv_restart;
			pm_power_off = fsl_hv_halt;
			ppc_md.halt = fsl_hv_halt;
#ifdef CONFIG_SMP
			/*
			 * Disable the timebase sync operations because we
			 * can't write to the timebase registers under the
			 * hypervisor.
			 */
			smp_85xx_ops.give_timebase = NULL;
			smp_85xx_ops.take_timebase = NULL;
#endif
			return 1;
		}
	}

	return 0;
}

define_machine(corenet_generic) {
	.name			= "CoreNet Generic",
	.probe			= corenet_generic_probe,
	.setup_arch		= corenet_gen_setup_arch,
	.init_IRQ		= corenet_gen_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
/*
 * Core reset may cause issues if using the proxy mode of MPIC.
 * So, use the mixed mode of MPIC if enabling CPU hotplug.
 *
 * Likewise, problems have been seen with kexec when coreint is enabled.
 */
#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_KEXEC_CORE)
	.get_irq		= mpic_get_irq,
#else
	.get_irq		= mpic_get_coreint_irq,
#endif
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
#ifdef CONFIG_PPC64
	.power_save		= book3e_idle,
#else
	.power_save		= e500_idle,
#endif
};
