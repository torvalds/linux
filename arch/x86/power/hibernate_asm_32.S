/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This may not use any stack, nor any variable that is not "NoSave":
 *
 * Its rewriting one kernel image with another. What is stack in "old"
 * image could very well be data page in "new" image, and overwriting
 * your own stack under you is bad idea.
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>

.text

ENTRY(swsusp_arch_suspend)
	movl %esp, saved_context_esp
	movl %ebx, saved_context_ebx
	movl %ebp, saved_context_ebp
	movl %esi, saved_context_esi
	movl %edi, saved_context_edi
	pushfl
	popl saved_context_eflags

	call swsusp_save
	ret

ENTRY(restore_image)
	movl	mmu_cr4_features, %ecx
	movl	resume_pg_dir, %eax
	subl	$__PAGE_OFFSET, %eax
	movl	%eax, %cr3

	jecxz	1f	# cr4 Pentium and higher, skip if zero
	andl	$~(X86_CR4_PGE), %ecx
	movl	%ecx, %cr4;  # turn off PGE
	movl	%cr3, %eax;  # flush TLB
	movl	%eax, %cr3
1:
	movl	restore_pblist, %edx
	.p2align 4,,7

copy_loop:
	testl	%edx, %edx
	jz	done

	movl	pbe_address(%edx), %esi
	movl	pbe_orig_address(%edx), %edi

	movl	$1024, %ecx
	rep
	movsl

	movl	pbe_next(%edx), %edx
	jmp	copy_loop
	.p2align 4,,7

done:
	/* go back to the original page tables */
	movl	$swapper_pg_dir, %eax
	subl	$__PAGE_OFFSET, %eax
	movl	%eax, %cr3
	movl	mmu_cr4_features, %ecx
	jecxz	1f	# cr4 Pentium and higher, skip if zero
	movl	%ecx, %cr4;  # turn PGE back on
1:

	movl saved_context_esp, %esp
	movl saved_context_ebp, %ebp
	movl saved_context_ebx, %ebx
	movl saved_context_esi, %esi
	movl saved_context_edi, %edi

	pushl saved_context_eflags
	popfl

	/* Saved in save_processor_state. */
	movl $saved_context, %eax
	lgdt saved_context_gdt_desc(%eax)

	xorl	%eax, %eax

	ret
