/* arch/arm/mach-s5p6442/include/mach/io.h
 *
 * Copyright 2008-2010 Ben Dooks <ben-linux@fluff.org>
 *
 * Default IO routines for S5P6442
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

/* No current ISA/PCI bus support. */
#define __io(a)		__typesafe_io(a)
#define __mem_pci(a)	(a)

#define IO_SPACE_LIMIT (0xFFFFFFFF)

#endif
