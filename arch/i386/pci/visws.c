/*
 *	Low-Level PCI Support for SGI Visual Workstation
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include "cobalt.h"
#include "lithium.h"

#include "pci.h"


extern struct pci_raw_ops pci_direct_conf1;

static int pci_visws_enable_irq(struct pci_dev *dev) { return 0; }

int (*pcibios_enable_irq)(struct pci_dev *dev) = &pci_visws_enable_irq;

void __init pcibios_penalize_isa_irq(int irq) {}


unsigned int pci_bus0, pci_bus1;

static inline u8 bridge_swizzle(u8 pin, u8 slot) 
{
	return (((pin - 1) + slot) % 4) + 1;
}

static u8 __init visws_swizzle(struct pci_dev *dev, u8 *pinp)
{
	u8 pin = *pinp;

	while (dev->bus->self) {	/* Move up the chain of bridges. */
		pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
		dev = dev->bus->self;
	}
	*pinp = pin;

	return PCI_SLOT(dev->devfn);
}

static int __init visws_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq, bus = dev->bus->number;

	pin--;

	/* Nothing useful at PIIX4 pin 1 */
	if (bus == pci_bus0 && slot == 4 && pin == 0)
		return -1;

	/* PIIX4 USB is on Bus 0, Slot 4, Line 3 */
	if (bus == pci_bus0 && slot == 4 && pin == 3) {
		irq = CO_IRQ(CO_APIC_PIIX4_USB);
		goto out;
	}

	/* First pin spread down 1 APIC entry per slot */
	if (pin == 0) {
		irq = CO_IRQ((bus == pci_bus0 ? CO_APIC_PCIB_BASE0 :
						CO_APIC_PCIA_BASE0) + slot);
		goto out;
	}

	/* lines 1,2,3 from any slot is shared in this twirly pattern */
	if (bus == pci_bus1) {
		/* lines 1-3 from devices 0 1 rotate over 2 apic entries */
		irq = CO_IRQ(CO_APIC_PCIA_BASE123 + ((slot + (pin - 1)) % 2));
	} else { /* bus == pci_bus0 */
		/* lines 1-3 from devices 0-3 rotate over 3 apic entries */
		if (slot == 0)
			slot = 3; /* same pattern */
		irq = CO_IRQ(CO_APIC_PCIA_BASE123 + ((3 - slot) + (pin - 1) % 3));
	}
out:
	printk(KERN_DEBUG "PCI: Bus %d Slot %d Line %d -> IRQ %d\n", bus, slot, pin, irq);
	return irq;
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

static int __init pcibios_init(void)
{
	/* The VISWS supports configuration access type 1 only */
	pci_probe = (pci_probe | PCI_PROBE_CONF1) &
		    ~(PCI_PROBE_BIOS | PCI_PROBE_CONF2);

	pci_bus0 = li_pcib_read16(LI_PCI_BUSNUM) & 0xff;
	pci_bus1 = li_pcia_read16(LI_PCI_BUSNUM) & 0xff;

	printk(KERN_INFO "PCI: Lithium bridge A bus: %u, "
		"bridge B (PIIX4) bus: %u\n", pci_bus1, pci_bus0);

	raw_pci_ops = &pci_direct_conf1;
	pci_scan_bus(pci_bus0, &pci_root_ops, NULL);
	pci_scan_bus(pci_bus1, &pci_root_ops, NULL);
	pci_fixup_irqs(visws_swizzle, visws_map_irq);
	pcibios_resource_survey();
	return 0;
}

subsys_initcall(pcibios_init);
