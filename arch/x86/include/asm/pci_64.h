/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PCI_64_H
#define _ASM_X86_PCI_64_H

#ifdef __KERNEL__

extern int (*pci_config_read)(int seg, int bus, int dev, int fn,
			      int reg, int len, u32 *value);
extern int (*pci_config_write)(int seg, int bus, int dev, int fn,
			       int reg, int len, u32 value);

#endif /* __KERNEL__ */

#endif /* _ASM_X86_PCI_64_H */
