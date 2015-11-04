/*
 * Code for the vDSO.  This version uses the old int $0x80 method.
*/

#include <asm/dwarf2.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>

/*
 * First get the common code for the sigreturn entry points.
 * This must come first.
 */
#include "sigreturn.S"

	.text
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
	ALIGN
__kernel_vsyscall:
	CFI_STARTPROC
	/*
	 * Reshuffle regs so that all of any of the entry instructions
	 * will preserve enough state.
	 */
	pushl	%edx
	CFI_ADJUST_CFA_OFFSET	4
	CFI_REL_OFFSET		edx, 0
	pushl	%ecx
	CFI_ADJUST_CFA_OFFSET	4
	CFI_REL_OFFSET		ecx, 0
	movl	%esp, %ecx

#ifdef CONFIG_X86_64
	/* If SYSENTER (Intel) or SYSCALL32 (AMD) is available, use it. */
	ALTERNATIVE_2 "", "sysenter", X86_FEATURE_SYSENTER32, \
	                  "syscall",  X86_FEATURE_SYSCALL32
#else
	ALTERNATIVE "", "sysenter", X86_FEATURE_SEP
#endif

	/* Enter using int $0x80 */
	movl	(%esp), %ecx
	int	$0x80
GLOBAL(int80_landing_pad)

	/* Restore ECX and EDX in case they were clobbered. */
	popl	%ecx
	CFI_RESTORE		ecx
	CFI_ADJUST_CFA_OFFSET	-4
	popl	%edx
	CFI_RESTORE		edx
	CFI_ADJUST_CFA_OFFSET	-4
	ret
	CFI_ENDPROC

	.size __kernel_vsyscall,.-__kernel_vsyscall
	.previous
