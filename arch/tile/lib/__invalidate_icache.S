/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 * A routine for synchronizing the instruction and data caches.
 * Useful for self-modifying code.
 *
 * r0 holds the buffer address
 * r1 holds the size in bytes
 */

#include <arch/chip.h>
#include <feedback.h>

#if defined(__NEWLIB__) || defined(__BME__)
#include <sys/page.h>
#else
#include <asm/page.h>
#endif

#ifdef __tilegx__
/* Share code among Tile family chips but adjust opcodes appropriately. */
#define slt cmpltu
#define bbst blbst
#define bnezt bnzt
#endif

#if defined(__tilegx__) && __SIZEOF_POINTER__ == 4
/* Force 32-bit ops so pointers wrap around appropriately. */
#define ADD_PTR addx
#define ADDI_PTR addxi
#else
#define ADD_PTR add
#define ADDI_PTR addi
#endif

        .section .text.__invalidate_icache, "ax"
        .global __invalidate_icache
        .type __invalidate_icache,@function
        .hidden __invalidate_icache
        .align 8
__invalidate_icache:
        FEEDBACK_ENTER(__invalidate_icache)
        {
         ADD_PTR r1, r0, r1       /* end of buffer */
         blez r1, .Lexit      /* skip out if size <= 0 */
        }
        {
         ADDI_PTR r1, r1, -1      /* point to last byte to flush */
         andi r0, r0, -CHIP_L1I_LINE_SIZE()  /* align to cache-line size */
        }
        {
         andi r1, r1, -CHIP_L1I_LINE_SIZE()  /* last cache line to flush */
         mf
        }
#if CHIP_L1I_CACHE_SIZE() > PAGE_SIZE
        {
         moveli r4, CHIP_L1I_CACHE_SIZE() / PAGE_SIZE  /* loop counter */
         move r2, r0          /* remember starting address */
        }
#endif
        drain
	{
         slt r3, r0, r1       /* set up loop invariant */
#if CHIP_L1I_CACHE_SIZE() > PAGE_SIZE
	 moveli r6, PAGE_SIZE
#endif
	}
.Lentry:
        {
         icoh r0
         ADDI_PTR r0, r0, CHIP_L1I_LINE_SIZE()   /* advance buffer */
        }
        {
         slt r3, r0, r1       /* check if buffer < buffer + size */
         bbst r3, .Lentry     /* loop if buffer < buffer + size */
        }
#if CHIP_L1I_CACHE_SIZE() > PAGE_SIZE
        {
         ADD_PTR r2, r2, r6
         ADD_PTR r1, r1, r6
        }
	{
         move r0, r2
         addi r4, r4, -1
	}
	{
         slt r3, r0, r1        /* set up loop invariant */
         bnezt r4, .Lentry
	}
#endif
        drain
.Lexit:
        jrp lr

.Lend___invalidate_icache:
        .size __invalidate_icache, \
		.Lend___invalidate_icache - __invalidate_icache
