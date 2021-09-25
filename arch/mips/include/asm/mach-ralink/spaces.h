/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_RALINK_SPACES_H_
#define __ASM_MACH_RALINK_SPACES_H_

#define PCI_IOBASE	mips_io_port_base
#define PCI_IOSIZE	SZ_64K
#define IO_SPACE_LIMIT	(PCI_IOSIZE - 1)

#include <asm/mach-generic/spaces.h>
#endif
