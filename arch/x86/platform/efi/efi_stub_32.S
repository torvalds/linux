/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EFI call stub for IA32.
 *
 * This stub allows us to make EFI calls in physical mode with interrupts
 * turned off.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>
#include <asm/page_types.h>

	__INIT
SYM_FUNC_START(efi_call_svam)
	push	%ebp
	movl	%esp, %ebp
	push	%ebx

	push	16(%esp)
	push	16(%esp)
	push	%ecx
	push	%edx
	movl	%eax, %ebx		// &systab_phys->runtime

	/*
	 * Switch to the flat mapped alias of this routine, by jumping to the
	 * address of label '1' after subtracting PAGE_OFFSET from it.
	 */
	movl	$1f, %edx
	subl	$__PAGE_OFFSET, %edx
	jmp	*%edx
1:

	/* disable paging */
	movl	%cr0, %edx
	andl	$0x7fffffff, %edx
	movl	%edx, %cr0

	/* convert the stack pointer to a flat mapped address */
	subl	$__PAGE_OFFSET, %esp

	/* call the EFI routine */
	movl	(%eax), %eax
	call	*EFI_svam(%eax)

	/* grab the virtually remapped EFI runtime services table pointer */
	movl	(%ebx), %ecx
	movl	36(%esp), %edx		// &efi.runtime
	movl	%ecx, (%edx)

	/* re-enable paging */
	movl	%cr0, %edx
	orl	$0x80000000, %edx
	movl	%edx, %cr0

	movl	16(%esp), %ebx
	leave
	RET
SYM_FUNC_END(efi_call_svam)
