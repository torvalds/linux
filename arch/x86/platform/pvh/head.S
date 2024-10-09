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
#include <asm/pgtable.h>
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

	mov %ebp, %ebx
	subl $_pa(pvh_start_xen), %ebx /* offset */
	jz .Lpagetable_done

	/* Fixup page-tables for relocation. */
	leal rva(pvh_init_top_pgt)(%ebp), %edi
	movl $PTRS_PER_PGD, %ecx
2:
	testl $_PAGE_PRESENT, 0x00(%edi)
	jz 1f
	addl %ebx, 0x00(%edi)
1:
	addl $8, %edi
	decl %ecx
	jnz 2b

	/* L3 ident has a single entry. */
	leal rva(pvh_level3_ident_pgt)(%ebp), %edi
	addl %ebx, 0x00(%edi)

	leal rva(pvh_level3_kernel_pgt)(%ebp), %edi
	addl %ebx, (PAGE_SIZE - 16)(%edi)
	addl %ebx, (PAGE_SIZE - 8)(%edi)

	/* pvh_level2_ident_pgt is fine - large pages */

	/* pvh_level2_kernel_pgt needs adjustment - large pages */
	leal rva(pvh_level2_kernel_pgt)(%ebp), %edi
	movl $PTRS_PER_PMD, %ecx
2:
	testl $_PAGE_PRESENT, 0x00(%edi)
	jz 1f
	addl %ebx, 0x00(%edi)
1:
	addl $8, %edi
	decl %ecx
	jnz 2b

.Lpagetable_done:
	/* Enable pre-constructed page tables. */
	leal rva(pvh_init_top_pgt)(%ebp), %eax
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

	/*
	 * Calculate load offset and store in phys_base.  __pa() needs
	 * phys_base set to calculate the hypercall page in xen_pvh_init().
	 */
	movq %rbp, %rbx
	subq $_pa(pvh_start_xen), %rbx
	movq %rbx, phys_base(%rip)

	/* Call xen_prepare_pvh() via the kernel virtual mapping */
	leaq xen_prepare_pvh(%rip), %rax
	subq phys_base(%rip), %rax
	addq $__START_KERNEL_map, %rax
	ANNOTATE_RETPOLINE_SAFE
	call *%rax

	/*
	 * Clear phys_base.  __startup_64 will *add* to its value,
	 * so reset to 0.
	 */
	xor  %rbx, %rbx
	movq %rbx, phys_base(%rip)

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

#ifdef CONFIG_X86_64
/*
 * Xen PVH needs a set of identity mapped and kernel high mapping
 * page tables.  pvh_start_xen starts running on the identity mapped
 * page tables, but xen_prepare_pvh calls into the high mapping.
 * These page tables need to be relocatable and are only used until
 * startup_64 transitions to init_top_pgt.
 */
SYM_DATA_START_PAGE_ALIGNED(pvh_init_top_pgt)
	.quad   pvh_level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.org    pvh_init_top_pgt + L4_PAGE_OFFSET * 8, 0
	.quad   pvh_level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.org    pvh_init_top_pgt + L4_START_KERNEL * 8, 0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad   pvh_level3_kernel_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
SYM_DATA_END(pvh_init_top_pgt)

SYM_DATA_START_PAGE_ALIGNED(pvh_level3_ident_pgt)
	.quad	pvh_level2_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.fill	511, 8, 0
SYM_DATA_END(pvh_level3_ident_pgt)
SYM_DATA_START_PAGE_ALIGNED(pvh_level2_ident_pgt)
	/*
	 * Since I easily can, map the first 1G.
	 * Don't set NX because code runs from these pages.
	 *
	 * Note: This sets _PAGE_GLOBAL despite whether
	 * the CPU supports it or it is enabled.  But,
	 * the CPU should ignore the bit.
	 */
	PMDS(0, __PAGE_KERNEL_IDENT_LARGE_EXEC, PTRS_PER_PMD)
SYM_DATA_END(pvh_level2_ident_pgt)
SYM_DATA_START_PAGE_ALIGNED(pvh_level3_kernel_pgt)
	.fill	L3_START_KERNEL, 8, 0
	/* (2^48-(2*1024*1024*1024)-((2^39)*511))/(2^30) = 510 */
	.quad	pvh_level2_kernel_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.quad	0 /* no fixmap */
SYM_DATA_END(pvh_level3_kernel_pgt)

SYM_DATA_START_PAGE_ALIGNED(pvh_level2_kernel_pgt)
	/*
	 * Kernel high mapping.
	 *
	 * The kernel code+data+bss must be located below KERNEL_IMAGE_SIZE in
	 * virtual address space, which is 1 GiB if RANDOMIZE_BASE is enabled,
	 * 512 MiB otherwise.
	 *
	 * (NOTE: after that starts the module area, see MODULES_VADDR.)
	 *
	 * This table is eventually used by the kernel during normal runtime.
	 * Care must be taken to clear out undesired bits later, like _PAGE_RW
	 * or _PAGE_GLOBAL in some cases.
	 */
	PMDS(0, __PAGE_KERNEL_LARGE_EXEC, KERNEL_IMAGE_SIZE / PMD_SIZE)
SYM_DATA_END(pvh_level2_kernel_pgt)

	ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_RELOC,
		     .long CONFIG_PHYSICAL_ALIGN;
		     .long LOAD_PHYSICAL_ADDR;
		     .long KERNEL_IMAGE_SIZE - 1)
#endif

	ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY,
	             _ASM_PTR (pvh_start_xen - __START_KERNEL_map))
