/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/vdso.h>
#include <asm/unistd.h>
#include <asm/asm-offsets.h>
#include <asm/dwarf.h>
#include <asm/ptrace.h>

#define WRAPPER_FRAME_SIZE (STACK_FRAME_OVERHEAD+8)

/*
 * Older glibc version called vdso without allocating a stackframe. This wrapper
 * is just used to allocate a stackframe. See
 * https://sourceware.org/git/?p=glibc.git;a=commit;h=478593e6374f3818da39332260dc453cb19cfa1e
 * for details.
 */
.macro vdso_func func
	.globl __kernel_\func
	.type  __kernel_\func,@function
	__ALIGN
__kernel_\func:
	CFI_STARTPROC
	aghi	%r15,-WRAPPER_FRAME_SIZE
	CFI_DEF_CFA_OFFSET (STACK_FRAME_OVERHEAD + WRAPPER_FRAME_SIZE)
	CFI_VAL_OFFSET 15, -STACK_FRAME_OVERHEAD
	stg	%r14,STACK_FRAME_OVERHEAD(%r15)
	CFI_REL_OFFSET 14, STACK_FRAME_OVERHEAD
	brasl	%r14,__s390_vdso_\func
	lg	%r14,STACK_FRAME_OVERHEAD(%r15)
	CFI_RESTORE 14
	aghi	%r15,WRAPPER_FRAME_SIZE
	CFI_DEF_CFA_OFFSET STACK_FRAME_OVERHEAD
	CFI_RESTORE 15
	br	%r14
	CFI_ENDPROC
	.size	__kernel_\func,.-__kernel_\func
.endm

vdso_func gettimeofday
vdso_func clock_getres
vdso_func clock_gettime
vdso_func getcpu

.macro vdso_syscall func,syscall
	.globl __kernel_\func
	.type  __kernel_\func,@function
	__ALIGN
__kernel_\func:
	CFI_STARTPROC
	svc	\syscall
	/* Make sure we notice when a syscall returns, which shouldn't happen */
	.word	0
	CFI_ENDPROC
	.size	__kernel_\func,.-__kernel_\func
.endm

vdso_syscall restart_syscall,__NR_restart_syscall
vdso_syscall sigreturn,__NR_sigreturn
vdso_syscall rt_sigreturn,__NR_rt_sigreturn
