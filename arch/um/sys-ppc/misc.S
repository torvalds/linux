/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * A couple of functions stolen from arch/ppc/kernel/misc.S for UML
 * by Chris Emerson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <asm/processor.h>
#include "ppc_asm.h"

#if defined(CONFIG_4xx) || defined(CONFIG_8xx)
#define CACHE_LINE_SIZE		16
#define LG_CACHE_LINE_SIZE	4
#define MAX_COPY_PREFETCH	1
#elif !defined(CONFIG_PPC64BRIDGE)
#define CACHE_LINE_SIZE		32
#define LG_CACHE_LINE_SIZE	5
#define MAX_COPY_PREFETCH	4
#else
#define CACHE_LINE_SIZE		128
#define LG_CACHE_LINE_SIZE	7
#define MAX_COPY_PREFETCH	1
#endif /* CONFIG_4xx || CONFIG_8xx */

	.text

/*
 * Clear a page using the dcbz instruction, which doesn't cause any
 * memory traffic (except to write out any cache lines which get
 * displaced).  This only works on cacheable memory.
 */
_GLOBAL(clear_page)
	li	r0,4096/CACHE_LINE_SIZE
	mtctr	r0
#ifdef CONFIG_8xx
	li	r4, 0
1:	stw	r4, 0(r3)
	stw	r4, 4(r3)
	stw	r4, 8(r3)
	stw	r4, 12(r3)
#else
1:	dcbz	0,r3
#endif
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	1b
	blr

/*
 * Copy a whole page.  We use the dcbz instruction on the destination
 * to reduce memory traffic (it eliminates the unnecessary reads of
 * the destination into cache).  This requires that the destination
 * is cacheable.
 */
#define COPY_16_BYTES		\
	lwz	r6,4(r4);	\
	lwz	r7,8(r4);	\
	lwz	r8,12(r4);	\
	lwzu	r9,16(r4);	\
	stw	r6,4(r3);	\
	stw	r7,8(r3);	\
	stw	r8,12(r3);	\
	stwu	r9,16(r3)

_GLOBAL(copy_page)
	addi	r3,r3,-4
	addi	r4,r4,-4
	li	r5,4

#ifndef CONFIG_8xx
#if MAX_COPY_PREFETCH > 1
	li	r0,MAX_COPY_PREFETCH
	li	r11,4
	mtctr	r0
11:	dcbt	r11,r4
	addi	r11,r11,CACHE_LINE_SIZE
	bdnz	11b
#else /* MAX_COPY_PREFETCH == 1 */
	dcbt	r5,r4
	li	r11,CACHE_LINE_SIZE+4
#endif /* MAX_COPY_PREFETCH */
#endif /* CONFIG_8xx */

	li	r0,4096/CACHE_LINE_SIZE
	mtctr	r0
1:
#ifndef CONFIG_8xx
	dcbt	r11,r4
	dcbz	r5,r3
#endif
	COPY_16_BYTES
#if CACHE_LINE_SIZE >= 32
	COPY_16_BYTES
#if CACHE_LINE_SIZE >= 64
	COPY_16_BYTES
	COPY_16_BYTES
#if CACHE_LINE_SIZE >= 128
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
#endif
#endif
#endif
	bdnz	1b
	blr
