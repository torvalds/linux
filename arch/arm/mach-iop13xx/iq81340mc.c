// SPDX-License-Identifier: GPL-2.0-only
/*
 * iq81340mc board support
 * Copyright (c) 2005-2006, Intel Corporation.
 */
#include <linux/pci.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "pci.h"
#include <asm/mach/time.h>
#include <mach/time.h>

extern int init_atu; /* Flag to select which ATU(s) to initialize / disable */

static int __init
iq81340mc_pcix_map_irq(const struct pci_dev *dev, u8 idsel, u8 pin)
{
	switch (idsel) {
	case 1:
		switch (pin) {
		case 1: return ATUX_INTB;
		case 2: return ATUX_INTC;
		case 3: return ATUX_INTD;
		case 4: return ATUX_INTA;
		default: return -1;
		}
	case 2:
		switch (pin) {
		case 1: return ATUX_INTC;
		case 2: return ATUX_INTD;
		case 3: return ATUX_INTC;
		case 4: return ATUX_INTD;
		default: return -1;
		}
	default: return -1;
	}
}

static struct hw_pci iq81340mc_pci __initdata = {
	.nr_controllers = 0,
	.setup		= iop13xx_pci_setup,
	.map_irq	= iq81340mc_pcix_map_irq,
	.scan		= iop13xx_scan_bus,
	.preinit	= iop13xx_pci_init,
};

static int __init iq81340mc_pci_init(void)
{
	iop13xx_atu_select(&iq81340mc_pci);
	pci_common_init(&iq81340mc_pci);
	iop13xx_map_pci_memory();

	return 0;
}

static void __init iq81340mc_init(void)
{
	iop13xx_platform_init();
	iq81340mc_pci_init();
	iop13xx_add_tpmi_devices();
}

static void __init iq81340mc_timer_init(void)
{
	unsigned long bus_freq = iop13xx_core_freq() / iop13xx_xsi_bus_ratio();
	printk(KERN_DEBUG "%s: bus frequency: %lu\n", __func__, bus_freq);
	iop_init_time(bus_freq);
}

MACHINE_START(IQ81340MC, "Intel IQ81340MC")
	/* Maintainer: Dan Williams <dan.j.williams@intel.com> */
	.atag_offset    = 0x100,
	.init_early     = iop13xx_init_early,
	.map_io         = iop13xx_map_io,
	.init_irq       = iop13xx_init_irq,
	.init_time	= iq81340mc_timer_init,
	.init_machine   = iq81340mc_init,
	.restart	= iop13xx_restart,
	.nr_irqs	= NR_IOP13XX_IRQS,
MACHINE_END
