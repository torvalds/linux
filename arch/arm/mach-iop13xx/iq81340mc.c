/*
 * iq81340mc board support
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */
#include <linux/pci.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/pci.h>
#include <asm/mach/time.h>
#include <mach/time.h>

extern int init_atu; /* Flag to select which ATU(s) to initialize / disable */

static int __init
iq81340mc_pcix_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
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
	.swizzle	= pci_std_swizzle,
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

static struct sys_timer iq81340mc_timer = {
       .init       = iq81340mc_timer_init,
       .offset     = iop_gettimeoffset,
};

MACHINE_START(IQ81340MC, "Intel IQ81340MC")
	/* Maintainer: Dan Williams <dan.j.williams@intel.com> */
	.phys_io        = IOP13XX_PMMR_PHYS_MEM_BASE,
	.io_pg_offst    = (IOP13XX_PMMR_VIRT_MEM_BASE >> 18) & 0xfffc,
	.boot_params    = 0x00000100,
	.map_io         = iop13xx_map_io,
	.init_irq       = iop13xx_init_irq,
	.timer          = &iq81340mc_timer,
	.init_machine   = iq81340mc_init,
MACHINE_END
