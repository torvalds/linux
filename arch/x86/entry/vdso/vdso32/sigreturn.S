/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/unistd_32.h>
#include <asm/dwarf2.h>
#include <asm/asm-offsets.h>

.macro STARTPROC_SIGNAL_FRAME sc
	CFI_STARTPROC	simple
	CFI_SIGNAL_FRAME
	/* -4 as pretcode has already been popped */
	CFI_DEF_CFA	esp,	\sc - 4
	CFI_OFFSET	eip,    IA32_SIGCONTEXT_ip
	CFI_OFFSET	eax,    IA32_SIGCONTEXT_ax
	CFI_OFFSET	ebx,    IA32_SIGCONTEXT_bx
	CFI_OFFSET	ecx,    IA32_SIGCONTEXT_cx
	CFI_OFFSET	edx,    IA32_SIGCONTEXT_dx
	CFI_OFFSET	esp,    IA32_SIGCONTEXT_sp
	CFI_OFFSET	ebp,    IA32_SIGCONTEXT_bp
	CFI_OFFSET	esi,    IA32_SIGCONTEXT_si
	CFI_OFFSET	edi,    IA32_SIGCONTEXT_di
	CFI_OFFSET	es,     IA32_SIGCONTEXT_es
	CFI_OFFSET	cs,     IA32_SIGCONTEXT_cs
	CFI_OFFSET	ss,     IA32_SIGCONTEXT_ss
	CFI_OFFSET	ds,     IA32_SIGCONTEXT_ds
/*
 * .cfi_offset eflags requires LLVM 16 or newer:
 *
 *   https://github.com/llvm/llvm-project/commit/67bd3c58c0c7389e39c5a2f4d3b1a30459ccf5b7
 *
 * Check for 16.0.1 to ensure the support is present, as 16.0.0 may be a
 * prerelease version.
 */
#if defined(CONFIG_AS_IS_GNU) || (defined(CONFIG_AS_IS_LLVM) && CONFIG_AS_VERSION >= 160001)
	CFI_OFFSET	eflags, IA32_SIGCONTEXT_flags
#endif
.endm

	.text
	.globl __kernel_sigreturn
	.type __kernel_sigreturn,@function
	ALIGN
__kernel_sigreturn:
	STARTPROC_SIGNAL_FRAME IA32_SIGFRAME_sigcontext
	popl %eax
	CFI_ADJUST_CFA_OFFSET -4
	movl $__NR_sigreturn, %eax
	int $0x80
SYM_INNER_LABEL(vdso32_sigreturn_landing_pad, SYM_L_GLOBAL)
	ud2a
	CFI_ENDPROC
	.size __kernel_sigreturn,.-__kernel_sigreturn

	.globl __kernel_rt_sigreturn
	.type __kernel_rt_sigreturn,@function
	ALIGN
__kernel_rt_sigreturn:
	STARTPROC_SIGNAL_FRAME IA32_RT_SIGFRAME_sigcontext
	movl $__NR_rt_sigreturn, %eax
	int $0x80
SYM_INNER_LABEL(vdso32_rt_sigreturn_landing_pad, SYM_L_GLOBAL)
	ud2a
	CFI_ENDPROC
	.size __kernel_rt_sigreturn,.-__kernel_rt_sigreturn
	.previous
