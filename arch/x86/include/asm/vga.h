/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _ASM_X86_VGA_H
#define _ASM_X86_VGA_H

#include <asm/set_memory.h>

/*
 *	On the PC, we can just recalculate addresses and then
 *	access the videoram directly without any black magic.
 *	To support memory encryption however, we need to access
 *	the videoram as decrypted memory.
 */

#define VGA_MAP_MEM(x, s)					\
({								\
	unsigned long start = (unsigned long)phys_to_virt(x);	\
								\
	if (IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT))			\
		set_memory_decrypted(start, (s) >> PAGE_SHIFT);	\
								\
	start;							\
})

#define vga_readb(x) (*(x))
#define vga_writeb(x, y) (*(y) = (x))

#endif /* _ASM_X86_VGA_H */
