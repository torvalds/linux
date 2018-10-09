/*
 * Copyright C 2016, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

	.code32
	.text
#define _pa(x)          ((x) - __START_KERNEL_map)

#include <linux/elfnote.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/asm.h>
#include <asm/boot.h>
#include <asm/processor-flags.h>
#include <asm/msr.h>
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
 * - `cs `: must be a 32-bit read/execute code segment with a base of ‘0’
 *          and a limit of ‘0xFFFFFFFF’. The selector value is unspecified.
 * - `ds`, `es`: must be a 32-bit read/write data segment with a base of
 *               ‘0’ and a limit of ‘0xFFFFFFFF’. The selector values are all
 *               unspecified.
 * - `tr`: must be a 32-bit TSS (active) with a base of '0' and a limit
 *         of '0x67'.
 * - `eflags`: bit 17 (VM) must be cleared. Bit 9 (IF) must be cleared.
 *             Bit 8 (TF) must be cleared. Other bits are all unspecified.
 *
 * All other processor registers and flag bits are unspecified. The OS is in
 * charge of setting up it's own stack, GDT and IDT.
 */

#define PVH_GDT_ENTRY_CS	1
#define PVH_GDT_ENTRY_DS	2
#define PVH_GDT_ENTRY_CANARY	3
#define PVH_CS_SEL		(PVH_GDT_ENTRY_CS * 8)
#define PVH_DS_SEL		(PVH_GDT_ENTRY_DS * 8)
#define PVH_CANARY_SEL		(PVH_GDT_ENTRY_CANARY * 8)

ENTRY(pvh_start_xen)
	cld

	lgdt (_pa(gdt))

	mov $PVH_DS_SEL,%eax
	mov %eax,%ds
	mov %eax,%es
	mov %eax,%ss

	/* Stash hvm_start_info. */
	mov $_pa(pvh_start_info), %edi
	mov %ebx, %esi
	mov _pa(pvh_start_info_sz), %ecx
	shr $2,%ecx
	rep
	movsl

	mov $_pa(early_stack_end), %esp

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
	mov $_pa(init_top_pgt), %eax
	mov %eax, %cr3
	mov $(X86_CR0_PG | X86_CR0_PE), %eax
	mov %eax, %cr0

	/* Jump to 64-bit mode. */
	ljmp $PVH_CS_SEL, $_pa(1f)

	/* 64-bit entry point. */
	.code64
1:
	/* Set base address in stack canary descriptor. */
	mov $MSR_GS_BASE,%ecx
	mov $_pa(canary), %eax
	xor %edx, %edx
	wrmsr

	call xen_prepare_pvh

	/* startup_64 expects boot_params in %rsi. */
	mov $_pa(pvh_bootparams), %rsi
	mov $_pa(startup_64), %rax
	jmp *%rax

#else /* CONFIG_X86_64 */

	/* Set base address in stack canary descriptor. */
	movl $_pa(gdt_start),%eax
	movl $_pa(canary),%ecx
	movw %cx, (PVH_GDT_ENTRY_CANARY * 8) + 2(%eax)
	shrl $16, %ecx
	movb %cl, (PVH_GDT_ENTRY_CANARY * 8) + 4(%eax)
	movb %ch, (PVH_GDT_ENTRY_CANARY * 8) + 7(%eax)

	mov $PVH_CANARY_SEL,%eax
	mov %eax,%gs

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
END(pvh_start_xen)

	.section ".init.data","aw"
	.balign 8
gdt:
	.word gdt_end - gdt_start
	.long _pa(gdt_start)
	.word 0
gdt_start:
	.quad 0x0000000000000000            /* NULL descriptor */
#ifdef CONFIG_X86_64
	.quad GDT_ENTRY(0xa09a, 0, 0xfffff) /* PVH_CS_SEL */
#else
	.quad GDT_ENTRY(0xc09a, 0, 0xfffff) /* PVH_CS_SEL */
#endif
	.quad GDT_ENTRY(0xc092, 0, 0xfffff) /* PVH_DS_SEL */
	.quad GDT_ENTRY(0x4090, 0, 0x18)    /* PVH_CANARY_SEL */
gdt_end:

	.balign 16
canary:
	.fill 48, 1, 0

early_stack:
	.fill BOOT_STACK_SIZE, 1, 0
early_stack_end:

	ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY,
	             _ASM_PTR (pvh_start_xen - __START_KERNEL_map))
