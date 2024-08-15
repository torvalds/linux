/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASMARM_PCI_H
#define ASMARM_PCI_H

#ifdef __KERNEL__
#include <asm/mach/pci.h> /* for pci_sys_data */

extern unsigned long pcibios_min_io;
#define PCIBIOS_MIN_IO pcibios_min_io
extern unsigned long pcibios_min_mem;
#define PCIBIOS_MIN_MEM pcibios_min_mem

#define pcibios_assign_all_busses()	pci_has_flag(PCI_REASSIGN_ALL_BUS)

#ifdef CONFIG_PCI_DOMAINS
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}
#endif /* CONFIG_PCI_DOMAINS */

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

extern void pcibios_report_status(unsigned int status_mask, int warn);

#endif /* __KERNEL__ */
#endif
