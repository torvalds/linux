/*
 * arch/arm/mach-sa1100/include/mach/io.h
 *
 * Copyright (C) 1997-1999 Russell King
 *
 * Modifications:
 *  06-12-1997	RMK	Created.
 *  07-04-1999	RMK	Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

/*
 * __io() is required to be an equivalent mapping to __mem_pci() for
 * SOC_COMMON to work.
 */
#define __io(a)		__typesafe_io(a)
#define __mem_pci(a)	(a)

#endif
