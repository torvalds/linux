/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * relocate_kernel.S - put the kernel image in place to boot
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 */

#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/kexec.h>
#include <asm/processor-flags.h>

/*
 * Must be relocatable PIC code callable as a C function
 */

#define PTR(x) (x << 2)

/*
 * control_page + KEXEC_CONTROL_CODE_MAX_SIZE
 * ~ control_page + PAGE_SIZE are used as data storage and stack for
 * jumping back
 */
#define DATA(offset)		(KEXEC_CONTROL_CODE_MAX_SIZE+(offset))

/* Minimal CPU state */
#define ESP			DATA(0x0)
#define CR0			DATA(0x4)
#define CR3			DATA(0x8)
#define CR4			DATA(0xc)

/* other data */
#define CP_VA_CONTROL_PAGE	DATA(0x10)
#define CP_PA_PGD		DATA(0x14)
#define CP_PA_SWAP_PAGE		DATA(0x18)
#define CP_PA_BACKUP_PAGES_MAP	DATA(0x1c)

	.text
SYM_CODE_START_NOALIGN(relocate_kernel)
	/* Save the CPU context, used for jumping back */

	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	pushf

	movl	20+8(%esp), %ebp /* list of pages */
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %edi
	movl	%esp, ESP(%edi)
	movl	%cr0, %eax
	movl	%eax, CR0(%edi)
	movl	%cr3, %eax
	movl	%eax, CR3(%edi)
	movl	%cr4, %eax
	movl	%eax, CR4(%edi)

	/* read the arguments and say goodbye to the stack */
	movl  20+4(%esp), %ebx /* page_list */
	movl  20+8(%esp), %ebp /* list of pages */
	movl  20+12(%esp), %edx /* start address */
	movl  20+16(%esp), %ecx /* cpu_has_pae */
	movl  20+20(%esp), %esi /* preserve_context */

	/* zero out flags, and disable interrupts */
	pushl $0
	popfl

	/* save some information for jumping back */
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %edi
	movl	%edi, CP_VA_CONTROL_PAGE(%edi)
	movl	PTR(PA_PGD)(%ebp), %eax
	movl	%eax, CP_PA_PGD(%edi)
	movl	PTR(PA_SWAP_PAGE)(%ebp), %eax
	movl	%eax, CP_PA_SWAP_PAGE(%edi)
	movl	%ebx, CP_PA_BACKUP_PAGES_MAP(%edi)

	/*
	 * get physical address of control page now
	 * this is impossible after page table switch
	 */
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %edi

	/* switch to new set of page tables */
	movl	PTR(PA_PGD)(%ebp), %eax
	movl	%eax, %cr3

	/* setup a new stack at the end of the physical control page */
	lea	PAGE_SIZE(%edi), %esp

	/* jump to identity mapped page */
	movl    %edi, %eax
	addl    $(identity_mapped - relocate_kernel), %eax
	pushl   %eax
	ret
SYM_CODE_END(relocate_kernel)

SYM_CODE_START_LOCAL_NOALIGN(identity_mapped)
	/* set return address to 0 if not preserving context */
	pushl	$0
	/* store the start address on the stack */
	pushl   %edx

	/*
	 * Set cr0 to a known state:
	 *  - Paging disabled
	 *  - Alignment check disabled
	 *  - Write protect disabled
	 *  - No task switch
	 *  - Don't do FP software emulation.
	 *  - Protected mode enabled
	 */
	movl	%cr0, %eax
	andl	$~(X86_CR0_PG | X86_CR0_AM | X86_CR0_WP | X86_CR0_TS | X86_CR0_EM), %eax
	orl	$(X86_CR0_PE), %eax
	movl	%eax, %cr0

	/* clear cr4 if applicable */
	testl	%ecx, %ecx
	jz	1f
	/*
	 * Set cr4 to a known state:
	 * Setting everything to zero seems safe.
	 */
	xorl	%eax, %eax
	movl	%eax, %cr4

	jmp 1f
1:

	/* Flush the TLB (needed?) */
	xorl	%eax, %eax
	movl	%eax, %cr3

	movl	CP_PA_SWAP_PAGE(%edi), %eax
	pushl	%eax
	pushl	%ebx
	call	swap_pages
	addl	$8, %esp

	/*
	 * To be certain of avoiding problems with self-modifying code
	 * I need to execute a serializing instruction here.
	 * So I flush the TLB, it's handy, and not processor dependent.
	 */
	xorl	%eax, %eax
	movl	%eax, %cr3

	/*
	 * set all of the registers to known values
	 * leave %esp alone
	 */

	testl	%esi, %esi
	jnz 1f
	xorl	%edi, %edi
	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl    %ecx, %ecx
	xorl    %edx, %edx
	xorl    %esi, %esi
	xorl    %ebp, %ebp
	ret
1:
	popl	%edx
	movl	CP_PA_SWAP_PAGE(%edi), %esp
	addl	$PAGE_SIZE, %esp
2:
	call	*%edx

	/* get the re-entry point of the peer system */
	movl	0(%esp), %ebp
	call	1f
1:
	popl	%ebx
	subl	$(1b - relocate_kernel), %ebx
	movl	CP_VA_CONTROL_PAGE(%ebx), %edi
	lea	PAGE_SIZE(%ebx), %esp
	movl	CP_PA_SWAP_PAGE(%ebx), %eax
	movl	CP_PA_BACKUP_PAGES_MAP(%ebx), %edx
	pushl	%eax
	pushl	%edx
	call	swap_pages
	addl	$8, %esp
	movl	CP_PA_PGD(%ebx), %eax
	movl	%eax, %cr3
	movl	%cr0, %eax
	orl	$X86_CR0_PG, %eax
	movl	%eax, %cr0
	lea	PAGE_SIZE(%edi), %esp
	movl	%edi, %eax
	addl	$(virtual_mapped - relocate_kernel), %eax
	pushl	%eax
	ret
SYM_CODE_END(identity_mapped)

SYM_CODE_START_LOCAL_NOALIGN(virtual_mapped)
	movl	CR4(%edi), %eax
	movl	%eax, %cr4
	movl	CR3(%edi), %eax
	movl	%eax, %cr3
	movl	CR0(%edi), %eax
	movl	%eax, %cr0
	movl	ESP(%edi), %esp
	movl	%ebp, %eax

	popf
	popl	%ebp
	popl	%edi
	popl	%esi
	popl	%ebx
	ret
SYM_CODE_END(virtual_mapped)

	/* Do the copies */
SYM_CODE_START_LOCAL_NOALIGN(swap_pages)
	movl	8(%esp), %edx
	movl	4(%esp), %ecx
	pushl	%ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi
	movl	%ecx, %ebx
	jmp	1f

0:	/* top, read another word from the indirection page */
	movl	(%ebx), %ecx
	addl	$4, %ebx
1:
	testb	$0x1, %cl     /* is it a destination page */
	jz	2f
	movl	%ecx,	%edi
	andl	$0xfffff000, %edi
	jmp     0b
2:
	testb	$0x2, %cl    /* is it an indirection page */
	jz	2f
	movl	%ecx,	%ebx
	andl	$0xfffff000, %ebx
	jmp     0b
2:
	testb   $0x4, %cl    /* is it the done indicator */
	jz      2f
	jmp     3f
2:
	testb   $0x8, %cl    /* is it the source indicator */
	jz      0b	     /* Ignore it otherwise */
	movl    %ecx,   %esi /* For every source page do a copy */
	andl    $0xfffff000, %esi

	movl	%edi, %eax
	movl	%esi, %ebp

	movl	%edx, %edi
	movl    $1024, %ecx
	rep ; movsl

	movl	%ebp, %edi
	movl	%eax, %esi
	movl	$1024, %ecx
	rep ; movsl

	movl	%eax, %edi
	movl	%edx, %esi
	movl	$1024, %ecx
	rep ; movsl

	lea	PAGE_SIZE(%ebp), %esi
	jmp     0b
3:
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	ret
SYM_CODE_END(swap_pages)

	.globl kexec_control_code_size
.set kexec_control_code_size, . - relocate_kernel
