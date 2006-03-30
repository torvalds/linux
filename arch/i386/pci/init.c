#include <linux/config.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"

/* arch_initcall has too random ordering, so call the initializers
   in the right sequence from here. */
static __init int pci_access_init(void)
{
#ifdef CONFIG_PCI_MMCONFIG
	pci_mmcfg_init();
#endif
	if (raw_pci_ops)
		return 0;
#ifdef CONFIG_PCI_BIOS
	pci_pcbios_init();
#endif
	if (raw_pci_ops)
		return 0;
#ifdef CONFIG_PCI_DIRECT
	pci_direct_init();
#endif
	return 0;
}
arch_initcall(pci_access_init);
