/*
 * IO definitions for TCC8000 processors and boards
 *
 * Copyright (C) 1997-1999 Russell King
 * Copyright (C) 2008-2009 Telechips
 * Copyright (C) 2010 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GNU Public License version 2.
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)			__typesafe_io(a)
#define __mem_pci(a)		(a)

#endif
