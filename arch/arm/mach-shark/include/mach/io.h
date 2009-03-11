/*
 * arch/arm/mach-shark/include/mach/io.h
 *
 * by Alexander Schulz
 *
 * derived from:
 * arch/arm/mach-ebsa110/include/mach/io.h
 * Copyright (C) 1997,1998 Russell King
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define PCIO_BASE	0xe0000000
#define IO_SPACE_LIMIT	0xffffffff

#define __io(a)		((void __iomem *)(PCIO_BASE + (a)))
#define __mem_pci(addr)	(addr)

#endif
