// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale P2020 board Setup
 *
 * Copyright 2007,2009,2012-2013 Freescale Semiconductor Inc.
 * Copyright 2022-2023 Pali Rohár <pali@kernel.org>
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/swiotlb.h>
#include <asm/ppc-pci.h>

#include <sysdev/fsl_pci.h>

#include "smp.h"
#include "mpc85xx.h"

#ifdef CONFIG_MPC85xx_DS
machine_arch_initcall(p2020_ds, mpc85xx_common_publish_devices);
#endif /* CONFIG_MPC85xx_DS */

#ifdef CONFIG_MPC85xx_DS
define_machine(p2020_ds) {
	.name			= "P2020 DS",
	.compatible		= "fsl,P2020DS",
	.setup_arch		= mpc85xx_ds_setup_arch,
	.init_IRQ		= mpc85xx_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb	= fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
#endif /* CONFIG_MPC85xx_DS */
