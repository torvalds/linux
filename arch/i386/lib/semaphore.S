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
#include <asm/alternative-asm.i>
#include <asm/frame.i>
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
	.section .sched.text
ENTRY(__down_failed)
	CFI_STARTPROC
	FRAME
	pushl %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx,0
	pushl %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	call __down
	popl %ecx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ecx
	popl %edx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE edx
	ENDFRAME
	ret
	CFI_ENDPROC
	END(__down_failed)

ENTRY(__down_failed_interruptible)
	CFI_STARTPROC
	FRAME
	pushl %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx,0
	pushl %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	call __down_interruptible
	popl %ecx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ecx
	popl %edx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE edx
	ENDFRAME
	ret
	CFI_ENDPROC
	END(__down_failed_interruptible)

ENTRY(__down_failed_trylock)
	CFI_STARTPROC
	FRAME
	pushl %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx,0
	pushl %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	call __down_trylock
	popl %ecx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ecx
	popl %edx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE edx
	ENDFRAME
	ret
	CFI_ENDPROC
	END(__down_failed_trylock)

ENTRY(__up_wakeup)
	CFI_STARTPROC
	FRAME
	pushl %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx,0
	pushl %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	call __up
	popl %ecx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE ecx
	popl %edx
	CFI_ADJUST_CFA_OFFSET -4
	CFI_RESTORE edx
	ENDFRAME
	ret
	CFI_ENDPROC
	END(__up_wakeup)

/*
 * rw spinlock fallbacks
 */
#ifdef CONFIG_SMP
ENTRY(__write_lock_failed)
	CFI_STARTPROC simple
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
	END(__write_lock_failed)

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
	END(__read_lock_failed)

#endif

#ifdef CONFIG_RWSEM_XCHGADD_ALGORITHM

/* Fix up special calling conventions */
ENTRY(call_rwsem_down_read_failed)
	CFI_STARTPROC
	push %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	push %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx,0
	call rwsem_down_read_failed
	pop %edx
	CFI_ADJUST_CFA_OFFSET -4
	pop %ecx
	CFI_ADJUST_CFA_OFFSET -4
	ret
	CFI_ENDPROC
	END(call_rwsem_down_read_failed)

ENTRY(call_rwsem_down_write_failed)
	CFI_STARTPROC
	push %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	calll rwsem_down_write_failed
	pop %ecx
	CFI_ADJUST_CFA_OFFSET -4
	ret
	CFI_ENDPROC
	END(call_rwsem_down_write_failed)

ENTRY(call_rwsem_wake)
	CFI_STARTPROC
	decw %dx    /* do nothing if still outstanding active readers */
	jnz 1f
	push %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	call rwsem_wake
	pop %ecx
	CFI_ADJUST_CFA_OFFSET -4
1:	ret
	CFI_ENDPROC
	END(call_rwsem_wake)

/* Fix up special calling conventions */
ENTRY(call_rwsem_downgrade_wake)
	CFI_STARTPROC
	push %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx,0
	push %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx,0
	call rwsem_downgrade_wake
	pop %edx
	CFI_ADJUST_CFA_OFFSET -4
	pop %ecx
	CFI_ADJUST_CFA_OFFSET -4
	ret
	CFI_ENDPROC
	END(call_rwsem_downgrade_wake)

#endif
