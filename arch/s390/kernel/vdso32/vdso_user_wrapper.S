/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/unistd.h>
#include <asm/dwarf.h>

.macro vdso_syscall func,syscall
	.globl __kernel_compat_\func
	.type  __kernel_compat_\func,@function
	.align 8
__kernel_compat_\func:
	CFI_STARTPROC
	svc	\syscall
	/* Make sure we notice when a syscall returns, which shouldn't happen */
	.word	0
	CFI_ENDPROC
	.size	__kernel_compat_\func,.-__kernel_compat_\func
.endm

vdso_syscall restart_syscall,__NR_restart_syscall
vdso_syscall sigreturn,__NR_sigreturn
vdso_syscall rt_sigreturn,__NR_rt_sigreturn
