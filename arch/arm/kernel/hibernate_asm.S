/*
 * Hibernation support specific for ARM
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/linkage.h>
	.text
ENTRY(swsusp_arch_suspend)
	/*
	 * Save current program status register
	 */
	ldr     r3, .Lsaved_cpsr
	mrs     r0, cpsr
	str     r0, [r3]

	/*
	 * Change to system(user) mode
	 */
	mov     r1, r0
	orr     r1, r1, #0x1f
	msr     cpsr_c, r1

	/*
	 * Save User context
	 */
	ldr     r3, .Lsaved_context_r0
	stmia   r3, {r0-r14}

	/*
	 * Go back to original SVC mode
	 */
	msr     cpsr_c, r0

	/*
	 * Save SVC context
	 */
	ldr     r3, .Lsaved_context_r13_svc
	stmia   r3, {r13-r14}
	ldr     r3, .Lsaved_spsr_svc
	mrs     r1, spsr
	str     r1, [r3]

	bl      swsusp_save

	/*
	 * Restore return address
	 */
	ldr     r3, .Lsaved_context_r14_svc
	ldr     lr, [r3]
	mov     pc, lr
ENDPROC(swsusp_arch_suspend)

ENTRY(swsusp_arch_resume)
	/*
	 * Restore_pblist is the starting point for loaded pages
	 */
	ldr     r0, .Lrestore_pblist
	ldr     r6, [r0]

.Lcopy_loop:
	ldr     r4, [r6]     /* src IOW present address */
	ldr     r5, [r6, #4] /* dst IOW original address*/

	/* No. of entries in one page, where each entry is 4 bytes */
	mov     r9, #1024

.Lcopy_one_page:
	/*
	 * This loop could be optimized by using stm and ldm.
	 */
	ldr     r8, [r4], #4
	str     r8, [r5], #4
	subs    r9, r9, #1
	bne     .Lcopy_one_page

	/*
	 * The last field of struct pbe is a pointer to the next pbe structure
	 */
	ldr     r6, [r6, #8]
	cmp     r6, #0
	bne     .Lcopy_loop

	/*
	 * Restore SVC context
	 */
	ldr     r3, .Lsaved_context_r13_svc
	ldmia   r3, {r13-r14}
	ldr     r3, .Lsaved_spsr_svc
	ldr     r1, [r3]
	msr     spsr_cxsf, r1

	mrs     r0, cpsr	/* Save current mode into r0 */

	/*
	 * Change to system(user) mode
	 */
	mov     r1, r0
	orr     r1, r1, #0x1f
	msr     cpsr_c, r1

	/*
	 * Restore User context
	 */
	ldr     r3, .Lsaved_context_r0
	ldmia   r3, {r0-r14}
	ldr     r3, .Lsaved_cpsr
	ldr     r1, [r3]
	msr     cpsr_cxsf, r1

	msr     cpsr_c, r0	/* Restore original mode from r0 */

	/*
	 * Flush TLB (Invalidate unified TLB unlocked entries)
	 */
	mov     r1, #0
	mcr     p15, 0, r1, c8, c7, 0

	/* Set the return value */
	mov	r0, #0

	/* Restore return address */
	ldr     r3, .Lsaved_context_r14_svc
	ldr     lr, [r3]
	mov     pc, lr
ENDPROC(swsusp_arch_resume)
	.align	4
.Lsaved_context_r0:		.long	saved_context_r0
.Lsaved_cpsr:			.long	saved_cpsr
.Lsaved_context_r13_svc: 	.long	saved_context_r13_svc
.Lsaved_context_r14_svc:	.long	saved_context_r14_svc
.Lsaved_spsr_svc:		.long	saved_spsr_svc
.Lrestore_pblist:		.long	restore_pblist
