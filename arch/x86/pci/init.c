#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"

/* arch_initcall has too random ordering, so call the initializers
   in the right sequence from here. */
static __init int pci_access_init(void)
{
	int type __maybe_unused = 0;

#ifdef CONFIG_PCI_DIRECT
	type = pci_direct_probe();
#endif
#ifdef CONFIG_PCI_MMCONFIG
	pci_mmcfg_init(type);
#endif
#ifdef CONFIG_PCI_OLPC
	pci_olpc_init();
#endif
	if (raw_pci_ops)
		return 0;
#ifdef CONFIG_PCI_BIOS
	pci_pcbios_init();
#endif
	/*
	 * don't check for raw_pci_ops here because we want pcbios as last
	 * fallback, yet it's needed to run first to set pcibios_last_bus
	 * in case legacy PCI probing is used. otherwise detecting peer busses
	 * fails.
	 */
#ifdef CONFIG_PCI_DIRECT
	pci_direct_init(type);
#endif
	if (!raw_pci_ops)
		printk(KERN_ERR
		"PCI: Fatal: No config space access function found\n");

	return 0;
}
arch_initcall(pci_access_init);
