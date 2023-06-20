/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_PCI_H
#define _ASM_LKL_PCI_H

#define pcibios_assign_all_busses() 0
#define PCIBIOS_MIN_IO 0x1000
#define PCIBIOS_MIN_MEM 0x10000000

#include <asm-generic/pci.h>

#endif /* _ASM_LKL_PCI_H */
