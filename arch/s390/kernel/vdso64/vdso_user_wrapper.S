/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/vdso.h>
#include <asm/unistd.h>
#include <asm/asm-offsets.h>
#include <asm/dwarf.h>
#include <asm/ptrace.h>

/*
 * Older glibc version called vdso without allocating a stackframe. This wrapper
 * is just used to allocate a stackframe. See
 * https://sourceware.org/git/?p=glibc.git;a=commit;h=478593e6374f3818da39332260dc453cb19cfa1e
 * for details.
 */
.macro vdso_func func
SYM_FUNC_START(__kernel_\func)
	CFI_STARTPROC
	aghi	%r15,-STACK_FRAME_VDSO_OVERHEAD
	CFI_DEF_CFA_OFFSET (STACK_FRAME_USER_OVERHEAD + STACK_FRAME_VDSO_OVERHEAD)
	CFI_VAL_OFFSET 15,-STACK_FRAME_USER_OVERHEAD
	stg	%r14,__SFVDSO_RETURN_ADDRESS(%r15)
	CFI_REL_OFFSET 14,__SFVDSO_RETURN_ADDRESS
	xc	__SFUSER_BACKCHAIN(8,%r15),__SFUSER_BACKCHAIN(%r15)
	brasl	%r14,__s390_vdso_\func
	lg	%r14,__SFVDSO_RETURN_ADDRESS(%r15)
	CFI_RESTORE 14
	aghi	%r15,STACK_FRAME_VDSO_OVERHEAD
	CFI_DEF_CFA_OFFSET STACK_FRAME_USER_OVERHEAD
	CFI_RESTORE 15
	br	%r14
	CFI_ENDPROC
SYM_FUNC_END(__kernel_\func)
.endm

vdso_func gettimeofday
vdso_func clock_getres
vdso_func clock_gettime
vdso_func getcpu

.macro vdso_syscall func,syscall
SYM_FUNC_START(__kernel_\func)
	CFI_STARTPROC
	svc	\syscall
	/* Make sure we notice when a syscall returns, which shouldn't happen */
	.word	0
	CFI_ENDPROC
SYM_FUNC_END(__kernel_\func)
.endm

vdso_syscall restart_syscall,__NR_restart_syscall
vdso_syscall sigreturn,__NR_sigreturn
vdso_syscall rt_sigreturn,__NR_rt_sigreturn
