/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_LOONGSON64_SPACES_H_
#define __ASM_MACH_LOONGSON64_SPACES_H_

#if defined(CONFIG_64BIT)
#define CAC_BASE        _AC(0x9800000000000000, UL)
#endif /* CONFIG_64BIT */

/* Skip 128k to trap NULL pointer dereferences */
#define PCI_IOBASE	_AC(0xc000000000000000 + SZ_128K, UL)
#define PCI_IOSIZE	SZ_16M
#define MAP_BASE	(PCI_IOBASE + PCI_IOSIZE)

/* Reserved at the start of PCI_IOBASE for legacy drivers */
#define MMIO_LOWER_RESERVED	0x10000

#include <asm/mach-generic/spaces.h>
#endif
