#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/machdep.h>
#include <platforms/gemini.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pci-bridge.h>

void __init gemini_pcibios_fixup(void)
{
	int i;
	struct pci_dev *dev = NULL;
	
	for_each_pci_dev(dev) {
		for(i = 0; i < 6; i++) {
			if (dev->resource[i].flags & IORESOURCE_IO) {
				dev->resource[i].start |= (0xfe << 24);
				dev->resource[i].end |= (0xfe << 24);
			}
		}
	}
}


/* The "bootloader" for Synergy boards does none of this for us, so we need to
   lay it all out ourselves... --Dan */
void __init gemini_find_bridges(void)
{
	struct pci_controller* hose;
	
	ppc_md.pcibios_fixup = gemini_pcibios_fixup;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;
	setup_indirect_pci(hose, 0xfec00000, 0xfee00000);
}
