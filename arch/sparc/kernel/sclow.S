/* sclow.S: Low level special syscall handling.
 *          Basically these are cases where we can completely
 *          handle the system call without saving any state
 *          because we know that the process will not sleep.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/ptrace.h>
#include <asm/asm_offsets.h>
#include <asm/errno.h>
#include <asm/winmacro.h>
#include <asm/thread_info.h>
#include <asm/psr.h>
#include <asm/page.h>

#define CC_AND_RETT  \
	set	PSR_C, %l4; \
	andn	%l0, %l4, %l4; \
	wr	%l4, 0x0, %psr; \
	nop; nop; nop; \
	jmp	%l2; \
	rett	%l2 + 4;

#define SC_AND_RETT  \
	set	PSR_C, %l4; \
	or	%l0, %l4, %l4; \
	wr	%l4, 0x0, %psr; \
	nop; nop; nop; \
	jmp	%l2; \
	rett	%l2 + 4;

#define LABEL(func)  func##_low

	.globl	LABEL(sunosnop)
LABEL(sunosnop):
	CC_AND_RETT

#if (ASIZ_task_uid == 2 && ASIZ_task_euid == 2)
	.globl	LABEL(sunosgetuid)
LABEL(sunosgetuid):
	LOAD_CURRENT(l4, l5)
	ld	[%l4 + TI_TASK], %l4
	lduh	[%l4 + AOFF_task_uid], %i0
	lduh	[%l4 + AOFF_task_euid], %i1
	CC_AND_RETT
#endif

#if (ASIZ_task_gid == 2 && ASIZ_task_egid == 2)
	.globl	LABEL(sunosgetgid)
LABEL(sunosgetgid):
	LOAD_CURRENT(l4, l5)
	ld	[%l4 + TI_TASK], %l4
	lduh	[%l4 + AOFF_task_gid], %i0
	lduh	[%l4 + AOFF_task_egid], %i1
	CC_AND_RETT
#endif

	.globl	LABEL(sunosmctl)
LABEL(sunosmctl):
	mov	0, %i0
	CC_AND_RETT

	.globl	LABEL(sunosgdtsize)
LABEL(sunosgdtsize):	
	mov	256, %i0
	CC_AND_RETT

	.globl	LABEL(getpagesize)
LABEL(getpagesize):
	set	PAGE_SIZE, %i0
	CC_AND_RETT

	/* XXX sys_nice() XXX */
	/* XXX sys_setpriority() XXX */
	/* XXX sys_getpriority() XXX */
	/* XXX sys_setregid() XXX */
	/* XXX sys_setgid() XXX */
	/* XXX sys_setreuid() XXX */
	/* XXX sys_setuid() XXX */
	/* XXX sys_setfsuid() XXX */
	/* XXX sys_setfsgid() XXX */
	/* XXX sys_setpgid() XXX */
	/* XXX sys_getpgid() XXX */
	/* XXX sys_setsid() XXX */
	/* XXX sys_getsid() XXX */
