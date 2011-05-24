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

/*
 * These macros are intended for use when there is a need to copy a low-level
 * function body into special memory.
 *
 * For example, when reconfiguring the SDRAM controller, the code doing the
 * reconfiguration may need to run from SRAM.
 *
 * NOTE: that the copied function body must be entirely self-contained and
 * position-independent in order for this to work properly.
 *
 * NOTE: in order for embedded literals and data to get referenced correctly,
 * the alignment of functions must be preserved when copying.  To ensure this,
 * the source and destination addresses for fncpy() must be aligned to a
 * multiple of 8 bytes: you will be get a BUG() if this condition is not met.
 * You will typically need a ".align 3" directive in the assembler where the
 * function to be copied is defined, and ensure that your allocator for the
 * destination buffer returns 8-byte-aligned pointers.
 *
 * Typical usage example:
 *
 * extern int f(args);
 * extern uint32_t size_of_f;
 * int (*copied_f)(args);
 * void *sram_buffer;
 *
 * copied_f = fncpy(sram_buffer, &f, size_of_f);
 *
 * ... later, call the function: ...
 *
 * copied_f(args);
 *
 * The size of the function to be copied can't be determined from C:
 * this must be determined by other means, such as adding assmbler directives
 * in the file where f is defined.
 */

#ifndef __ASM_FNCPY_H
#define __ASM_FNCPY_H

#include <linux/types.h>
#include <linux/string.h>

#include <asm/bug.h>
#include <asm/cacheflush.h>

/*
 * Minimum alignment requirement for the source and destination addresses
 * for function copying.
 */
#define FNCPY_ALIGN 8

#define fncpy(dest_buf, funcp, size) ({					\
	uintptr_t __funcp_address;					\
	typeof(funcp) __result;						\
									\
	asm("" : "=r" (__funcp_address) : "0" (funcp));			\
									\
	/*								\
	 * Ensure alignment of source and destination addresses,	\
	 * disregarding the function's Thumb bit:			\
	 */								\
	BUG_ON((uintptr_t)(dest_buf) & (FNCPY_ALIGN - 1) ||		\
		(__funcp_address & ~(uintptr_t)1 & (FNCPY_ALIGN - 1)));	\
									\
	memcpy(dest_buf, (void const *)(__funcp_address & ~1), size);	\
	flush_icache_range((unsigned long)(dest_buf),			\
		(unsigned long)(dest_buf) + (size));			\
									\
	asm("" : "=r" (__result)					\
		: "0" ((uintptr_t)(dest_buf) | (__funcp_address & 1)));	\
									\
	__result;							\
})

#endif /* !__ASM_FNCPY_H */
