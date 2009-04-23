/*
 *
 * arch/arm/mach-u300/include/mach/memory.h
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Memory virtual/physical mapping constants.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */

#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

#ifdef CONFIG_MACH_U300_DUAL_RAM

#define PHYS_OFFSET		UL(0x48000000)
#define BOOT_PARAMS_OFFSET	(PHYS_OFFSET + 0x100)

#else

#ifdef CONFIG_MACH_U300_2MB_ALIGNMENT_FIX
#define PHYS_OFFSET (0x28000000 + \
	     (CONFIG_MACH_U300_ACCESS_MEM_SIZE - \
	     (CONFIG_MACH_U300_ACCESS_MEM_SIZE & 1))*1024*1024)
#else
#define PHYS_OFFSET (0x28000000 + \
	     (CONFIG_MACH_U300_ACCESS_MEM_SIZE +	\
	     (CONFIG_MACH_U300_ACCESS_MEM_SIZE & 1))*1024*1024)
#endif
#define BOOT_PARAMS_OFFSET (0x28000000 + \
	    (CONFIG_MACH_U300_ACCESS_MEM_SIZE +		\
	    (CONFIG_MACH_U300_ACCESS_MEM_SIZE & 1))*1024*1024 + 0x100)
#endif

/*
 * We enable a real big DMA buffer if need be.
 */
#define CONSISTENT_DMA_SIZE SZ_4M

#endif
