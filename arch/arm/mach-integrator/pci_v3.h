/* SPDX-License-Identifier: GPL-2.0 */
/* Simple oneliner include to the PCIv3 early init */
#ifdef CONFIG_PCI
extern int pci_v3_early_init(void);
#else
static inline int pci_v3_early_init(void)
{
	return 0;
}
#endif
