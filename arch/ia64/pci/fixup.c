/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 * Derived from fixup.c of i386 tree.
 */

#include <linux/pci.h>
#include <linux/init.h>

#include <asm/machvec.h>

/*
 * Fixup to mark boot BIOS video selected by BIOS before it changes
 *
 * From information provided by "Jon Smirl" <jonsmirl@gmail.com>
 *
 * The standard boot ROM sequence for an x86 machine uses the BIOS
 * to select an initial video card for boot display. This boot video
 * card will have it's BIOS copied to C0000 in system RAM.
 * IORESOURCE_ROM_SHADOW is used to associate the boot video
 * card with this copy. On laptops this copy has to be used since
 * the main ROM may be compressed or combined with another image.
 * See pci_map_rom() for use of this flag. IORESOURCE_ROM_SHADOW
 * is marked here since the boot video device will be the only enabled
 * video device at this point.
 */

static void __devinit pci_fixup_video(struct pci_dev *pdev)
{
	struct pci_dev *bridge;
	struct pci_bus *bus;
	u16 config;

	if ((strcmp(platform_name, "dig") != 0)
	    && (strcmp(platform_name, "hpzx1")  != 0))
		return;
	/* Maybe, this machine supports legacy memory map. */

	if ((pdev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
		return;

	/* Is VGA routed to us? */
	bus = pdev->bus;
	while (bus) {
		bridge = bus->self;

		/*
		 * From information provided by
		 * "David Miller" <davem@davemloft.net>
		 * The bridge control register is valid for PCI header
		 * type BRIDGE, or CARDBUS. Host to PCI controllers use
		 * PCI header type NORMAL.
		 */
		if (bridge
		    &&((bridge->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		       ||(bridge->hdr_type == PCI_HEADER_TYPE_CARDBUS))) {
			pci_read_config_word(bridge, PCI_BRIDGE_CONTROL,
						&config);
			if (!(config & PCI_BRIDGE_CTL_VGA))
				return;
		}
		bus = bus->parent;
	}
	pci_read_config_word(pdev, PCI_COMMAND, &config);
	if (config & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) {
		pdev->resource[PCI_ROM_RESOURCE].flags |= IORESOURCE_ROM_SHADOW;
		printk(KERN_DEBUG "Boot video device is %s\n", pci_name(pdev));
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pci_fixup_video);
