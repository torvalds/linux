/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_IO_H
#define __ASM_OPENRISC_IO_H

/*
 * PCI: can we really do 0 here if we have no port IO?
 */
#define IO_SPACE_LIMIT		0

/* OpenRISC has no port IO */
#define HAVE_ARCH_PIO_SIZE	1
#define PIO_RESERVED		0X0UL
#define PIO_OFFSET		0
#define PIO_MASK		0

#include <asm-generic/io.h>

extern void __iomem *__ioremap(phys_addr_t offset, unsigned long size,
				pgprot_t prot);

static inline void __iomem *ioremap(phys_addr_t offset, unsigned long size)
{
	return __ioremap(offset, size, PAGE_KERNEL);
}

/* #define _PAGE_CI       0x002 */
static inline void __iomem *ioremap_nocache(phys_addr_t offset,
					     unsigned long size)
{
	return __ioremap(offset, size,
			 __pgprot(pgprot_val(PAGE_KERNEL) | _PAGE_CI));
}

extern void iounmap(void *addr);
#endif
