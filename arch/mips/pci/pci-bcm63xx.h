#ifndef PCI_BCM63XX_H_
#define PCI_BCM63XX_H_

#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_dev_pci.h>

/*
 * Cardbus shares  the PCI bus, but has	 no IDSEL, so a	 special id is
 * reserved for it.  If you have a standard PCI device at this id, you
 * need to change the following definition.
 */
#define CARDBUS_PCI_IDSEL	0x8


#define PCIE_BUS_BRIDGE		0
#define PCIE_BUS_DEVICE		1

/*
 * defined in ops-bcm63xx.c
 */
extern struct pci_ops bcm63xx_pci_ops;
extern struct pci_ops bcm63xx_cb_ops;
extern struct pci_ops bcm63xx_pcie_ops;

/*
 * defined in pci-bcm63xx.c
 */
extern void __iomem *pci_iospace_start;

#endif /* ! PCI_BCM63XX_H_ */
