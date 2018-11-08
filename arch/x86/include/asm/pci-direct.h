/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PCI_DIRECT_H
#define _ASM_X86_PCI_DIRECT_H

#include <linux/types.h>

/* Direct PCI access. This is used for PCI accesses in early boot before
   the PCI subsystem works. */

extern u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset);
extern u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset);
extern u16 read_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset);
extern u32 pci_early_find_cap(int bus, int slot, int func, int cap);
extern void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val);
extern void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val);
extern void write_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset, u16 val);

extern unsigned int pci_early_clear_msi;
extern int early_pci_allowed(void);
#endif /* _ASM_X86_PCI_DIRECT_H */
