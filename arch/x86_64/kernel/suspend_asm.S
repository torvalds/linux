/* Copyright 2004,2005 Pavel Machek <pavel@suse.cz>, Andi Kleen <ak@suse.de>, Rafael J. Wysocki <rjw@sisk.pl>
 *
 * Distribute under GPLv2.
 *
 * swsusp_arch_resume may not use any stack, nor any variable that is
 * not "NoSave" during copying pages:
 *
 * Its rewriting one kernel image with another. What is stack in "old"
 * image could very well be data page in "new" image, and overwriting
 * your own stack under you is bad idea.
 */
	
	.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>

ENTRY(swsusp_arch_suspend)

	movq %rsp, saved_context_esp(%rip)
	movq %rax, saved_context_eax(%rip)
	movq %rbx, saved_context_ebx(%rip)
	movq %rcx, saved_context_ecx(%rip)
	movq %rdx, saved_context_edx(%rip)
	movq %rbp, saved_context_ebp(%rip)
	movq %rsi, saved_context_esi(%rip)
	movq %rdi, saved_context_edi(%rip)
	movq %r8,  saved_context_r08(%rip)
	movq %r9,  saved_context_r09(%rip)
	movq %r10, saved_context_r10(%rip)
	movq %r11, saved_context_r11(%rip)
	movq %r12, saved_context_r12(%rip)
	movq %r13, saved_context_r13(%rip)
	movq %r14, saved_context_r14(%rip)
	movq %r15, saved_context_r15(%rip)
	pushfq ; popq saved_context_eflags(%rip)

	call swsusp_save
	ret

ENTRY(restore_image)
	/* switch to temporary page tables */
	movq	$__PAGE_OFFSET, %rdx
	movq	temp_level4_pgt(%rip), %rax
	subq	%rdx, %rax
	movq	%rax, %cr3
	/* Flush TLB */
	movq	mmu_cr4_features(%rip), %rax
	movq	%rax, %rdx
	andq	$~(1<<7), %rdx	# PGE
	movq	%rdx, %cr4;  # turn off PGE
	movq	%cr3, %rcx;  # flush TLB
	movq	%rcx, %cr3;
	movq	%rax, %cr4;  # turn PGE back on

	movq	restore_pblist(%rip), %rdx
loop:
	testq	%rdx, %rdx
	jz	done

	/* get addresses from the pbe and copy the page */
	movq	pbe_address(%rdx), %rsi
	movq	pbe_orig_address(%rdx), %rdi
	movq	$512, %rcx
	rep
	movsq

	/* progress to the next pbe */
	movq	pbe_next(%rdx), %rdx
	jmp	loop
done:
	/* go back to the original page tables */
	movq    $(init_level4_pgt - __START_KERNEL_map), %rax
	addq    phys_base(%rip), %rax
	movq    %rax, %cr3

	/* Flush TLB, including "global" things (vmalloc) */
	movq	mmu_cr4_features(%rip), %rax
	movq	%rax, %rdx
	andq	$~(1<<7), %rdx;  # PGE
	movq	%rdx, %cr4;  # turn off PGE
	movq	%cr3, %rcx;  # flush TLB
	movq	%rcx, %cr3
	movq	%rax, %cr4;  # turn PGE back on

	movl	$24, %eax
	movl	%eax, %ds

	movq saved_context_esp(%rip), %rsp
	movq saved_context_ebp(%rip), %rbp
	/* Don't restore %rax, it must be 0 anyway */
	movq saved_context_ebx(%rip), %rbx
	movq saved_context_ecx(%rip), %rcx
	movq saved_context_edx(%rip), %rdx
	movq saved_context_esi(%rip), %rsi
	movq saved_context_edi(%rip), %rdi
	movq saved_context_r08(%rip), %r8
	movq saved_context_r09(%rip), %r9
	movq saved_context_r10(%rip), %r10
	movq saved_context_r11(%rip), %r11
	movq saved_context_r12(%rip), %r12
	movq saved_context_r13(%rip), %r13
	movq saved_context_r14(%rip), %r14
	movq saved_context_r15(%rip), %r15
	pushq saved_context_eflags(%rip) ; popfq

	xorq	%rax, %rax

	ret
