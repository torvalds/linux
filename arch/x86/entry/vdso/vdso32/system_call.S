/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AT_SYSINFO entry point
*/

#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>

	.text
	.globl __kernel_vsyscall
	.type __kernel_vsyscall,@function
	ALIGN
__kernel_vsyscall:
	CFI_STARTPROC

	/*
	 * If using int $0x80, there is no reason to muck about with the
	 * stack here. Unfortunately just overwriting the push instructions
	 * would mess up the CFI annotations, but it is only a 3-byte
	 * NOP in that case. This could be avoided by patching the
	 * vdso symbol table (not the code) and entry point, but that
	 * would a fair bit of tooling work or by simply compiling
	 * two different vDSO images, but that doesn't seem worth it.
	 */
	ALTERNATIVE "int $0x80; ret", "", X86_FEATURE_SYSFAST32

	/*
	 * Reshuffle regs so that all of any of the entry instructions
	 * will preserve enough state.
	 *
	 * A really nice entry sequence would be:
	 *  pushl %edx
	 *  pushl %ecx
	 *  movl  %esp, %ecx
	 *
	 * Unfortunately, naughty Android versions between July and December
	 * 2015 actually hardcode the traditional Linux SYSENTER entry
	 * sequence.  That is severely broken for a number of reasons (ask
	 * anyone with an AMD CPU, for example).  Nonetheless, we try to keep
	 * it working approximately as well as it ever worked.
	 *
	 * This link may elucidate some of the history:
	 *   https://android-review.googlesource.com/#/q/Iac3295376d61ef83e713ac9b528f3b50aa780cd7
	 * personally, I find it hard to understand what's going on there.
	 *
	 * Note to future user developers: DO NOT USE SYSENTER IN YOUR CODE.
	 * Execute an indirect call to the address in the AT_SYSINFO auxv
	 * entry.  That is the ONLY correct way to make a fast 32-bit system
	 * call on Linux.  (Open-coding int $0x80 is also fine, but it's
	 * slow.)
	 */
	pushl	%ecx
	CFI_ADJUST_CFA_OFFSET	4
	CFI_REL_OFFSET		ecx, 0
	pushl	%edx
	CFI_ADJUST_CFA_OFFSET	4
	CFI_REL_OFFSET		edx, 0
	pushl	%ebp
	CFI_ADJUST_CFA_OFFSET	4
	CFI_REL_OFFSET		ebp, 0

	#define SYSENTER_SEQUENCE	"movl %esp, %ebp; sysenter"
	#define SYSCALL_SEQUENCE	"movl %ecx, %ebp; syscall"

	ALTERNATIVE SYSENTER_SEQUENCE, SYSCALL_SEQUENCE, X86_FEATURE_SYSCALL32

	/* Re-enter using int $0x80 */
	int	$0x80
SYM_INNER_LABEL(int80_landing_pad, SYM_L_GLOBAL)

	/*
	 * Restore EDX and ECX in case they were clobbered.  EBP is not
	 * clobbered (the kernel restores it), but it's cleaner and
	 * probably faster to pop it than to adjust ESP using addl.
	 */
	popl	%ebp
	CFI_RESTORE		ebp
	CFI_ADJUST_CFA_OFFSET	-4
	popl	%edx
	CFI_RESTORE		edx
	CFI_ADJUST_CFA_OFFSET	-4
	popl	%ecx
	CFI_RESTORE		ecx
	CFI_ADJUST_CFA_OFFSET	-4
	RET
	CFI_ENDPROC

	.size __kernel_vsyscall,.-__kernel_vsyscall
	.previous
