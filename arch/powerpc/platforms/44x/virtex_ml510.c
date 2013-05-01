#include <asm/i8259.h>
#include <linux/pci.h>
#include "44x.h"

/**
 * ml510_ail_quirk
 */
static void ml510_ali_quirk(struct pci_dev *dev)
{
	/* Enable the IDE controller */
	pci_write_config_byte(dev, 0x58, 0x4c);
	/* Assign irq 14 to the primary ide channel */
	pci_write_config_byte(dev, 0x44, 0x0d);
	/* Assign irq 15 to the secondary ide channel */
	pci_write_config_byte(dev, 0x75, 0x0f);
	/* Set the ide controller in native mode */
	pci_write_config_byte(dev, 0x09, 0xff);

	/* INTB = disabled, INTA = disabled */
	pci_write_config_byte(dev, 0x48, 0x00);
	/* INTD = disabled, INTC = disabled */
	pci_write_config_byte(dev, 0x4a, 0x00);
	/* Audio = INT7, Modem = disabled. */
	pci_write_config_byte(dev, 0x4b, 0x60);
	/* USB = INT7 */
	pci_write_config_byte(dev, 0x74, 0x06);
}
DECLARE_PCI_FIXUP_EARLY(0x10b9, 0x1533, ml510_ali_quirk);

