/*    Copyright (c) 1997 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Initial Power Macintosh COFF version.
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *      Modifications for IBM PowerPC 400-class processor evaluation
 *      boards.
 *
 *    Module name: crt0.S
 *
 *    Description:
 *      Boot loader execution entry point. Clears out .bss section as per
 *      ANSI C requirements. Invalidates and flushes the caches over the
 *      range covered by the boot loader's .text section. Sets up a stack
 *      below the .text section entry point.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/ppc_asm.h>

	.text

	.globl	_start
_start:
#ifdef XCOFF
	.long	__start,0,0

	.globl	__start
__start:
#endif

	## Flush and invalidate the caches for the range in memory covering
	## the .text section of the boot loader

	lis	r9,_start@h		# r9 = &_start
	lis	r8,_etext@ha		#
	addi	r8,r8,_etext@l		# r8 = &_etext
3:	dcbf	r0,r9			# Flush the data cache
	icbi	r0,r9			# Invalidate the instruction cache
	addi	r9,r9,0x10		# Increment by one cache line
	cmplw	cr0,r9,r8		# Are we at the end yet?
	blt	3b			# No, keep flushing and invalidating
	sync				# sync ; isync after flushing the icache
	isync

	## Clear out the BSS as per ANSI C requirements

	lis	r7,_end@ha
	addi	r7,r7,_end@l		# r7 = &_end
	lis	r8,__bss_start@ha	#
	addi	r8,r8,__bss_start@l	# r8 = &_bss_start

	## Determine how large an area, in number of words, to clear

	subf	r7,r8,r7		# r7 = &_end - &_bss_start + 1
	addi	r7,r7,3			# r7 += 3
	srwi.	r7,r7,2			# r7 = size in words.
	beq	2f			# If the size is zero, do not bother
	addi	r8,r8,-4		# r8 -= 4
	mtctr	r7			# SPRN_CTR = number of words to clear
	li	r0,0			# r0 = 0
1:	stwu	r0,4(r8)		# Clear out a word
	bdnz	1b			# If we are not done yet, keep clearing
2:

#ifdef CONFIG_40x
	## Set up the stack

	lis	r9,_start@h		# r9 = &_start (text section entry)
	ori	r9,r9,_start@l
	subi	r1,r9,64		# Start the stack 64 bytes below _start
	clrrwi	r1,r1,4			# Make sure it is aligned on 16 bytes.
	li	r0,0
	stwu	r0,-16(r1)
	mtlr	r9
#endif

	b	start			# All done, start the real work.
