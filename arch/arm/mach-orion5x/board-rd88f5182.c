// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-orion5x/rd88f5182-setup.c
 *
 * Marvell Orion-NAS Reference Design Setup
 *
 * Maintainer: Ronen Shitrit <rshitrit@marvell.com>
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include "common.h"
#include "orion5x.h"

/*****************************************************************************
 * RD-88F5182 Info
 ****************************************************************************/

/*
 * PCI
 */

#define RD88F5182_PCI_SLOT0_OFFS	7
#define RD88F5182_PCI_SLOT0_IRQ_A_PIN	7
#define RD88F5182_PCI_SLOT0_IRQ_B_PIN	6

/*****************************************************************************
 * PCI
 ****************************************************************************/

static void __init rd88f5182_pci_preinit(void)
{
	int pin;

	/*
	 * Configure PCI GPIO IRQ pins
	 */
	pin = RD88F5182_PCI_SLOT0_IRQ_A_PIN;
	if (gpio_request(pin, "PCI IntA") == 0) {
		if (gpio_direction_input(pin) == 0) {
			irq_set_irq_type(gpio_to_irq(pin), IRQ_TYPE_LEVEL_LOW);
		} else {
			printk(KERN_ERR "rd88f5182_pci_preinit failed to "
					"set_irq_type pin %d\n", pin);
			gpio_free(pin);
		}
	} else {
		printk(KERN_ERR "rd88f5182_pci_preinit failed to request gpio %d\n", pin);
	}

	pin = RD88F5182_PCI_SLOT0_IRQ_B_PIN;
	if (gpio_request(pin, "PCI IntB") == 0) {
		if (gpio_direction_input(pin) == 0) {
			irq_set_irq_type(gpio_to_irq(pin), IRQ_TYPE_LEVEL_LOW);
		} else {
			printk(KERN_ERR "rd88f5182_pci_preinit failed to "
					"set_irq_type pin %d\n", pin);
			gpio_free(pin);
		}
	} else {
		printk(KERN_ERR "rd88f5182_pci_preinit failed to gpio_request %d\n", pin);
	}
}

static int __init rd88f5182_pci_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * PCI IRQs are connected via GPIOs
	 */
	switch (slot - RD88F5182_PCI_SLOT0_OFFS) {
	case 0:
		if (pin == 1)
			return gpio_to_irq(RD88F5182_PCI_SLOT0_IRQ_A_PIN);
		else
			return gpio_to_irq(RD88F5182_PCI_SLOT0_IRQ_B_PIN);
	default:
		return -1;
	}
}

static struct hw_pci rd88f5182_pci __initdata = {
	.nr_controllers	= 2,
	.preinit	= rd88f5182_pci_preinit,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= rd88f5182_pci_map_irq,
};

static int __init rd88f5182_pci_init(void)
{
	if (of_machine_is_compatible("marvell,rd-88f5182-nas"))
		pci_common_init(&rd88f5182_pci);

	return 0;
}

subsys_initcall(rd88f5182_pci_init);
