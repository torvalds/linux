/*
 * i386 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Portions Copyright 1999 Red Hat, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * rw semaphores implemented November 1999 by Benjamin LaHaise <bcrl@kvack.org>
 */

#include <linux/linkage.h>
#include <asm/rwlock.h>
#include <asm/alternative-asm.h>
#include <asm/frame.h>
#include <asm/dwarf2.h>

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 *
 * %eax contains the semaphore pointer on entry. Save the C-clobbered
 * registers (%eax, %edx and %ecx) except %eax whish is either a return
 * value or just clobbered..
 */
	.section .sched.text, "ax"

/*
 * rw spinlock fallbacks
 */
#ifdef CONFIG_SMP
ENTRY(__write_lock_failed)
	CFI_STARTPROC
	FRAME
2: 	LOCK_PREFIX
	addl	$ RW_LOCK_BIAS,(%eax)
1:	rep; nop
	cmpl	$ RW_LOCK_BIAS,(%eax)
	jne	1b
	LOCK_PREFIX
	subl	$ RW_LOCK_BIAS,(%eax)
	jnz	2b
	ENDFRAME
	ret
	CFI_ENDPROC
	ENDPROC(__write_lock_failed)

ENTRY(__read_lock_failed)
	CFI_STARTPROC
	FRAME
2: 	LOCK_PREFIX
	incl	(%eax)
1:	rep; nop
	cmpl	$1,(%eax)
	js	1b
	LOCK_PREFIX
	decl	(%eax)
	js	2b
	ENDFRAME
	ret
	CFI_ENDPROC
	ENDPROC(__read_lock_failed)

#endif

#ifdef CONFIG_RWSEM_XCHGADD_ALGORITHM

/* Fix up special calling conventions */
ENTRY(call_rwsem_down_read_failed)
	CFI_STARTPROC
	pushl_cfi %ecx
	CFI_REL_OFFSET ecx,0
	pushl_cfi %edx
	CFI_REL_OFFSET edx,0
	call rwsem_down_read_failed
	popl_cfi %edx
	popl_cfi %ecx
	ret
	CFI_ENDPROC
	ENDPROC(call_rwsem_down_read_failed)

ENTRY(call_rwsem_down_write_failed)
	CFI_STARTPROC
	pushl_cfi %ecx
	CFI_REL_OFFSET ecx,0
	calll rwsem_down_write_failed
	popl_cfi %ecx
	ret
	CFI_ENDPROC
	ENDPROC(call_rwsem_down_write_failed)

ENTRY(call_rwsem_wake)
	CFI_STARTPROC
	decw %dx    /* do nothing if still outstanding active readers */
	jnz 1f
	pushl_cfi %ecx
	CFI_REL_OFFSET ecx,0
	call rwsem_wake
	popl_cfi %ecx
1:	ret
	CFI_ENDPROC
	ENDPROC(call_rwsem_wake)

/* Fix up special calling conventions */
ENTRY(call_rwsem_downgrade_wake)
	CFI_STARTPROC
	pushl_cfi %ecx
	CFI_REL_OFFSET ecx,0
	pushl_cfi %edx
	CFI_REL_OFFSET edx,0
	call rwsem_downgrade_wake
	popl_cfi %edx
	popl_cfi %ecx
	ret
	CFI_ENDPROC
	ENDPROC(call_rwsem_downgrade_wake)

#endif
