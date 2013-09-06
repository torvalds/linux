/*
 * arch/arm/include/asm/fncpy.h - helper macros for function body copying
 *
 * Copyright (C) 2011 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_FNCPY_H
#define __ASM_FNCPY_H

#include <linux/types.h>

/*
 * NOTE: in order for embedded literals and data to get referenced correctly,
 * the alignment of functions must be preserved when copying.  To ensure this,
 * the source and destination addresses for fncpy() must be aligned to a
 * multiple of 8 bytes: you will be get a BUG() if this condition is not met.
 * You will typically need a ".align 3" directive in the assembler where the
 * function to be copied is defined, and ensure that your allocator for the
 * destination buffer returns 8-byte-aligned pointers.
*/
#define ARCH_FNCPY_ALIGN	3

/* Clear the Thumb bit */
#define fnptr_to_addr(funcp) ({						\
	uintptr_t __funcp_address;					\
									\
	asm("" : "=r" (__funcp_address) : "0" (funcp));			\
	__funcp_address & ~1;						\
})

/* Put the Thumb bit back */
#define fnptr_translate(orig_funcp, new_addr) ({			\
	uintptr_t __funcp_address;					\
	typeof(orig_funcp) __result;					\
									\
	asm("" : "=r" (__funcp_address) : "0" (orig_funcp));		\
	asm("" : "=r" (__result)					\
		: "0" ((uintptr_t)(new_addr) | (__funcp_address & 1)));	\
									\
	__result;							\
})

#include <asm-generic/fncpy.h>

#endif /* !__ASM_FNCPY_H */
