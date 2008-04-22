/*
 * arch/arm/mach-ixp4xx/gtwx5715-pci.c
 *
 * Gemtek GTWX5715 (Linksys WRV54G) board setup
 *
 * Copyright (C) 2004 George T. Joseph
 * Derived from Coyote
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/arch/gtwx5715.h>
#include <asm/mach/pci.h>

/*
 * The exact GPIO pins and IRQs are defined in arch-ixp4xx/gtwx5715.h
 * Slot 0 isn't actually populated with a card connector but
 * we initialize it anyway in case a future version has the
 * slot populated or someone with good soldering skills has
 * some free time.
 */
void __init gtwx5715_pci_preinit(void)
{
	set_irq_type(GTWX5715_PCI_SLOT0_INTA_IRQ, IRQT_LOW);
	set_irq_type(GTWX5715_PCI_SLOT0_INTB_IRQ, IRQT_LOW);
	set_irq_type(GTWX5715_PCI_SLOT1_INTA_IRQ, IRQT_LOW);
	set_irq_type(GTWX5715_PCI_SLOT1_INTB_IRQ, IRQT_LOW);

	ixp4xx_pci_preinit();
}


static int __init gtwx5715_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int rc;
	static int gtwx5715_irqmap
			[GTWX5715_PCI_SLOT_COUNT]
			[GTWX5715_PCI_INT_PIN_COUNT] = {
	{GTWX5715_PCI_SLOT0_INTA_IRQ, GTWX5715_PCI_SLOT0_INTB_IRQ},
	{GTWX5715_PCI_SLOT1_INTA_IRQ, GTWX5715_PCI_SLOT1_INTB_IRQ},
};

	if (slot >= GTWX5715_PCI_SLOT_COUNT ||
			pin >= GTWX5715_PCI_INT_PIN_COUNT) rc = -1;
	else
		rc = gtwx5715_irqmap[slot][pin-1];

	printk("%s: Mapped slot %d pin %d to IRQ %d\n", __func__, slot, pin, rc);
	return(rc);
}

struct hw_pci gtwx5715_pci __initdata = {
	.nr_controllers = 1,
	.preinit =        gtwx5715_pci_preinit,
	.swizzle =        pci_std_swizzle,
	.setup =          ixp4xx_setup,
	.scan =           ixp4xx_scan_bus,
	.map_irq =        gtwx5715_map_irq,
};

int __init gtwx5715_pci_init(void)
{
	if (machine_is_gtwx5715())
	{
		pci_common_init(&gtwx5715_pci);
	}

	return 0;
}

subsys_initcall(gtwx5715_pci_init);
