/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_RALINK_SPACES_H_
#define __ASM_MACH_RALINK_SPACES_H_

#define PCI_IOBASE	_AC(0xa0000000, UL)
#define PCI_IOSIZE	SZ_16M
#define IO_SPACE_LIMIT	(PCI_IOSIZE - 1)

#include <asm/mach-generic/spaces.h>
#endif
