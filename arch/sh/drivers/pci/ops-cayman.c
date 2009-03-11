#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <cpu/irq.h>
#include "pci-sh5.h"

int __init pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int result = -1;

	/* The complication here is that the PCI IRQ lines from the Cayman's 2
	   5V slots get into the CPU via a different path from the IRQ lines
	   from the 3 3.3V slots.  Thus, we have to detect whether the card's
	   interrupts go via the 5V or 3.3V path, i.e. the 'bridge swizzling'
	   at the point where we cross from 5V to 3.3V is not the normal case.

	   The added complication is that we don't know that the 5V slots are
	   always bus 2, because a card containing a PCI-PCI bridge may be
	   plugged into a 3.3V slot, and this changes the bus numbering.

	   Also, the Cayman has an intermediate PCI bus that goes a custom
	   expansion board header (and to the secondary bridge).  This bus has
	   never been used in practice.

	   The 1ary onboard PCI-PCI bridge is device 3 on bus 0
	   The 2ary onboard PCI-PCI bridge is device 0 on the 2ary bus of
	   the 1ary bridge.
	   */

	struct slot_pin {
		int slot;
		int pin;
	} path[4];
	int i=0;

	while (dev->bus->number > 0) {

		slot = path[i].slot = PCI_SLOT(dev->devfn);
		pin = path[i].pin = pci_swizzle_interrupt_pin(dev, pin);
		dev = dev->bus->self;
		i++;
		if (i > 3) panic("PCI path to root bus too long!\n");
	}

	slot = PCI_SLOT(dev->devfn);
	/* This is the slot on bus 0 through which the device is eventually
	   reachable. */

	/* Now work back up. */
	if ((slot < 3) || (i == 0)) {
		/* Bus 0 (incl. PCI-PCI bridge itself) : perform the final
		   swizzle now. */
		result = IRQ_INTA + pci_swizzle_interrupt_pin(dev, pin) - 1;
	} else {
		i--;
		slot = path[i].slot;
		pin  = path[i].pin;
		if (slot > 0) {
			panic("PCI expansion bus device found - not handled!\n");
		} else {
			if (i > 0) {
				/* 5V slots */
				i--;
				slot = path[i].slot;
				pin  = path[i].pin;
				/* 'pin' was swizzled earlier wrt slot, don't do it again. */
				result = IRQ_P2INTA + (pin - 1);
			} else {
				/* IRQ for 2ary PCI-PCI bridge : unused */
				result = -1;
			}
		}
	}

	return result;
}

struct pci_channel board_pci_channels[] = {
	{ sh5_pci_init, &sh5_pci_ops, NULL, NULL, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};
EXPORT_SYMBOL(board_pci_channels);

int __init pcibios_init_platform(void)
{
	return sh5pci_init(__pa(memory_start),
			   __pa(memory_end) - __pa(memory_start));
}
