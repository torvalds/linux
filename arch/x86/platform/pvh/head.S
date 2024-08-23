/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright C 2016, Oracle and/or its affiliates. All rights reserved.
 */

	.code32
	.text
#define _pa(x)          ((x) - __START_KERNEL_map)
#define rva(x)          ((x) - pvh_start_xen)

#include <linux/elfnote.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/desc_defs.h>
#include <asm/segment.h>
#include <asm/asm.h>
#include <asm/boot.h>
#include <asm/processor-flags.h>
#include <asm/msr.h>
#include <asm/nospec-branch.h>
#include <xen/interface/elfnote.h>

	__HEAD

/*
 * Entry point for PVH guests.
 *
 * Xen ABI specifies the following register state when we come here:
 *
 * - `ebx`: contains the physical memory address where the loader has placed
 *          the boot start info structure.
 * - `cr0`: bit 0 (PE) must be set. All the other writeable bits are cleared.
 * - `cr4`: all bits are cleared.
 * - `cs `: must be a 32-bit read/execute code segment with a base of `0`
 *          and a limit of `0xFFFFFFFF`. The selector value is unspecified.
 * - `ds`, `es`: must be a 32-bit read/write data segment with a base of
 *               `0` and a limit of `0xFFFFFFFF`. The selector values are all
 *               unspecified.
 * - `tr`: must be a 32-bit TSS (active) with a base of '0' and a limit
 *         of '0x67'.
 * - `eflags`: bit 17 (VM) must be cleared. Bit 9 (IF) must be cleared.
 *             Bit 8 (TF) must be cleared. Other bits are all unspecified.
 *
 * All other processor registers and flag bits are unspecified. The OS is in
 * charge of setting up its own stack, GDT and IDT.
 */

#define PVH_GDT_ENTRY_CS	1
#define PVH_GDT_ENTRY_DS	2
#define PVH_CS_SEL		(PVH_GDT_ENTRY_CS * 8)
#define PVH_DS_SEL		(PVH_GDT_ENTRY_DS * 8)

SYM_CODE_START_LOCAL(pvh_start_xen)
	UNWIND_HINT_END_OF_STACK
	cld

	/*
	 * See the comment for startup_32 for more details.  We need to
	 * execute a call to get the execution address to be position
	 * independent, but we don't have a stack.  Save and restore the
	 * magic field of start_info in ebx, and use that as the stack.
	 */
	mov  (%ebx), %eax
	leal 4(%ebx), %esp
	ANNOTATE_INTRA_FUNCTION_CALL
	call 1f
1:	popl %ebp
	mov  %eax, (%ebx)
	subl $rva(1b), %ebp
	movl $0, %esp

	leal rva(gdt)(%ebp), %eax
	leal rva(gdt_start)(%ebp), %ecx
	movl %ecx, 2(%eax)
	lgdt (%eax)

	mov $PVH_DS_SEL,%eax
	mov %eax,%ds
	mov %eax,%es
	mov %eax,%ss

	/* Stash hvm_start_info. */
	leal rva(pvh_start_info)(%ebp), %edi
	mov %ebx, %esi
	movl rva(pvh_start_info_sz)(%ebp), %ecx
	shr $2,%ecx
	rep
	movsl

	leal rva(early_stack_end)(%ebp), %esp

	/* Enable PAE mode. */
	mov %cr4, %eax
	orl $X86_CR4_PAE, %eax
	mov %eax, %cr4

#ifdef CONFIG_X86_64
	/* Enable Long mode. */
	mov $MSR_EFER, %ecx
	rdmsr
	btsl $_EFER_LME, %eax
	wrmsr

	/* Enable pre-constructed page tables. */
	leal rva(init_top_pgt)(%ebp), %eax
	mov %eax, %cr3
	mov $(X86_CR0_PG | X86_CR0_PE), %eax
	mov %eax, %cr0

	/* Jump to 64-bit mode. */
	pushl $PVH_CS_SEL
	leal  rva(1f)(%ebp), %eax
	pushl %eax
	lretl

	/* 64-bit entry point. */
	.code64
1:
	UNWIND_HINT_END_OF_STACK

	/* Set base address in stack canary descriptor. */
	mov $MSR_GS_BASE,%ecx
	leal canary(%rip), %eax
	xor %edx, %edx
	wrmsr

	call xen_prepare_pvh

	/* startup_64 expects boot_params in %rsi. */
	lea pvh_bootparams(%rip), %rsi
	jmp startup_64

#else /* CONFIG_X86_64 */

	call mk_early_pgtbl_32

	mov $_pa(initial_page_table), %eax
	mov %eax, %cr3

	mov %cr0, %eax
	or $(X86_CR0_PG | X86_CR0_PE), %eax
	mov %eax, %cr0

	ljmp $PVH_CS_SEL, $1f
1:
	call xen_prepare_pvh
	mov $_pa(pvh_bootparams), %esi

	/* startup_32 doesn't expect paging and PAE to be on. */
	ljmp $PVH_CS_SEL, $_pa(2f)
2:
	mov %cr0, %eax
	and $~X86_CR0_PG, %eax
	mov %eax, %cr0
	mov %cr4, %eax
	and $~X86_CR4_PAE, %eax
	mov %eax, %cr4

	ljmp $PVH_CS_SEL, $_pa(startup_32)
#endif
SYM_CODE_END(pvh_start_xen)

	.section ".init.data","aw"
	.balign 8
SYM_DATA_START_LOCAL(gdt)
	.word gdt_end - gdt_start
	.long _pa(gdt_start) /* x86-64 will overwrite if relocated. */
	.word 0
SYM_DATA_END(gdt)
SYM_DATA_START_LOCAL(gdt_start)
	.quad 0x0000000000000000            /* NULL descriptor */
#ifdef CONFIG_X86_64
	.quad GDT_ENTRY(DESC_CODE64, 0, 0xfffff) /* PVH_CS_SEL */
#else
	.quad GDT_ENTRY(DESC_CODE32, 0, 0xfffff) /* PVH_CS_SEL */
#endif
	.quad GDT_ENTRY(DESC_DATA32, 0, 0xfffff) /* PVH_DS_SEL */
SYM_DATA_END_LABEL(gdt_start, SYM_L_LOCAL, gdt_end)

	.balign 16
SYM_DATA_LOCAL(canary, .fill 48, 1, 0)

SYM_DATA_START_LOCAL(early_stack)
	.fill BOOT_STACK_SIZE, 1, 0
SYM_DATA_END_LABEL(early_stack, SYM_L_LOCAL, early_stack_end)

	ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY,
	             _ASM_PTR (pvh_start_xen - __START_KERNEL_map))
