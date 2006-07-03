/*
 * arch/arm/mach-ixp23xx/roadrunner.c
 *
 * RoadRunner board-specific routines
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Based on 2.4 code Copyright 2005 (c) ADI Engineering Corporation
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/mtd/physmap.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/pci.h>

/*
 * Interrupt mapping
 */
#define INTA		IRQ_ROADRUNNER_PCI_INTA
#define INTB		IRQ_ROADRUNNER_PCI_INTB
#define INTC		IRQ_ROADRUNNER_PCI_INTC
#define INTD		IRQ_ROADRUNNER_PCI_INTD

#define INTC_PIN	IXP23XX_GPIO_PIN_11
#define INTD_PIN	IXP23XX_GPIO_PIN_12

static int __init roadrunner_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	static int pci_card_slot_irq[] = {INTB, INTC, INTD, INTA};
	static int pmc_card_slot_irq[] = {INTA, INTB, INTC, INTD};
	static int usb_irq[] = {INTB, INTC, INTD, -1};
	static int mini_pci_1_irq[] = {INTB, INTC, -1, -1};
	static int mini_pci_2_irq[] = {INTC, INTD, -1, -1};

	switch(dev->bus->number) {
		case 0:
			switch(dev->devfn) {
			case 0x0: // PCI-PCI bridge
				break;
			case 0x8: // PCI Card Slot
				return pci_card_slot_irq[pin - 1];
			case 0x10: // PMC Slot
				return pmc_card_slot_irq[pin - 1];
			case 0x18: // PMC Slot Secondary Agent
				break;
			case 0x20: // IXP Processor
				break;
			default:
				return NO_IRQ;
			}
			break;

		case 1:
			switch(dev->devfn) {
			case 0x0: // IDE Controller
				return (pin == 1) ? INTC : -1;
			case 0x8: // USB fun 0
			case 0x9: // USB fun 1
			case 0xa: // USB fun 2
				return usb_irq[pin - 1];
			case 0x10: // Mini PCI 1
				return mini_pci_1_irq[pin-1];
			case 0x18: // Mini PCI 2
				return mini_pci_2_irq[pin-1];
			case 0x20: // MEM slot
				return (pin == 1) ? INTA : -1;
			default:
				return NO_IRQ;
			}
			break;

		default:
			return NO_IRQ;
	}

	return NO_IRQ;
}

static void roadrunner_pci_preinit(void)
{
	set_irq_type(IRQ_ROADRUNNER_PCI_INTC, IRQT_LOW);
	set_irq_type(IRQ_ROADRUNNER_PCI_INTD, IRQT_LOW);

	ixp23xx_pci_preinit();
}

static struct hw_pci roadrunner_pci __initdata = {
	.nr_controllers	= 1,
	.preinit	= roadrunner_pci_preinit,
	.setup		= ixp23xx_pci_setup,
	.scan		= ixp23xx_pci_scan_bus,
	.map_irq	= roadrunner_map_irq,
};

static int __init roadrunner_pci_init(void)
{
	if (machine_is_roadrunner())
		pci_common_init(&roadrunner_pci);

	return 0;
};

subsys_initcall(roadrunner_pci_init);

static struct physmap_flash_data roadrunner_flash_data = {
	.width		= 2,
};

static struct resource roadrunner_flash_resource = {
	.start		= 0x90000000,
	.end		= 0x93ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device roadrunner_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &roadrunner_flash_data,
	},
	.num_resources	= 1,
	.resource	= &roadrunner_flash_resource,
};

static void __init roadrunner_init(void)
{
	platform_device_register(&roadrunner_flash);

	/*
	 * Mark flash as writeable
	 */
	IXP23XX_EXP_CS0[0] |= IXP23XX_FLASH_WRITABLE;
	IXP23XX_EXP_CS0[1] |= IXP23XX_FLASH_WRITABLE;
	IXP23XX_EXP_CS0[2] |= IXP23XX_FLASH_WRITABLE;
	IXP23XX_EXP_CS0[3] |= IXP23XX_FLASH_WRITABLE;

	ixp23xx_sys_init();
}

MACHINE_START(ROADRUNNER, "ADI Engineering RoadRunner Development Platform")
	/* Maintainer: Deepak Saxena */
	.phys_io	= IXP23XX_PERIPHERAL_PHYS,
	.io_pg_offst	= ((IXP23XX_PERIPHERAL_VIRT >> 18)) & 0xfffc,
	.map_io		= ixp23xx_map_io,
	.init_irq	= ixp23xx_init_irq,
	.timer		= &ixp23xx_timer,
	.boot_params	= 0x00000100,
	.init_machine	= roadrunner_init,
MACHINE_END
