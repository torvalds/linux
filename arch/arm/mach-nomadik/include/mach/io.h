/*
 * arch/arm/mach-nomadik/include/mach/io.h   (copied from mach-sa1100)
 *
 * Copyright (C) 1997-1999 Russell King
 *
 * Modifications:
 *  06-12-1997  RMK     Created.
 *  07-04-1999  RMK     Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)         __typesafe_io(a)
#define __mem_pci(a)    (a)

#endif
