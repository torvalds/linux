#ifndef __ASM_POWERPC_LIBATA_PORTMAP_H
#define __ASM_POWERPC_LIBATA_PORTMAP_H

#define ATA_PRIMARY_IRQ(dev)	pci_get_legacy_ide_irq(dev, 0)

#define ATA_SECONDARY_IRQ(dev)	pci_get_legacy_ide_irq(dev, 1)

#endif
