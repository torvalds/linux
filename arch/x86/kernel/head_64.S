/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/arch/x86/kernel/head_64.S -- start in 32bit and switch to 64bit
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2000 Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2000 Karsten Keil <kkeil@suse.de>
 *  Copyright (C) 2001,2002 Andi Kleen <ak@suse.de>
 *  Copyright (C) 2005 Eric Biederman <ebiederm@xmission.com>
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/pgtable.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/cache.h>
#include <asm/processor-flags.h>
#include <asm/percpu.h>
#include <asm/nops.h>
#include "../entry/calling.h"
#include <asm/nospec-branch.h>
#include <asm/apicdef.h>
#include <asm/fixmap.h>
#include <asm/smp.h>
#include <asm/thread_info.h>

/*
 * We are not able to switch in one step to the final KERNEL ADDRESS SPACE
 * because we need identity-mapped pages.
 */

	__HEAD
	.code64
SYM_CODE_START_NOALIGN(startup_64)
	UNWIND_HINT_END_OF_STACK
	/*
	 * At this point the CPU runs in 64bit mode CS.L = 1 CS.D = 0,
	 * and someone has loaded an identity mapped page table
	 * for us.  These identity mapped page tables map all of the
	 * kernel pages and possibly all of memory.
	 *
	 * %RSI holds the physical address of the boot_params structure
	 * provided by the bootloader. Preserve it in %R15 so C function calls
	 * will not clobber it.
	 *
	 * We come here either directly from a 64bit bootloader, or from
	 * arch/x86/boot/compressed/head_64.S.
	 *
	 * We only come here initially at boot nothing else comes here.
	 *
	 * Since we may be loaded at an address different from what we were
	 * compiled to run at we first fixup the physical addresses in our page
	 * tables and then reload them.
	 */
	mov	%rsi, %r15

	/* Set up the stack for verify_cpu() */
	leaq	__top_init_kernel_stack(%rip), %rsp

	/* Setup GSBASE to allow stack canary access for C code */
	movl	$MSR_GS_BASE, %ecx
	leaq	INIT_PER_CPU_VAR(fixed_percpu_data)(%rip), %rdx
	movl	%edx, %eax
	shrq	$32,  %rdx
	wrmsr

	call	startup_64_setup_gdt_idt

	/* Now switch to __KERNEL_CS so IRET works reliably */
	pushq	$__KERNEL_CS
	leaq	.Lon_kernel_cs(%rip), %rax
	pushq	%rax
	lretq

.Lon_kernel_cs:
	ANNOTATE_NOENDBR
	UNWIND_HINT_END_OF_STACK

#ifdef CONFIG_AMD_MEM_ENCRYPT
	/*
	 * Activate SEV/SME memory encryption if supported/enabled. This needs to
	 * be done now, since this also includes setup of the SEV-SNP CPUID table,
	 * which needs to be done before any CPUID instructions are executed in
	 * subsequent code. Pass the boot_params pointer as the first argument.
	 */
	movq	%r15, %rdi
	call	sme_enable
#endif

	/* Sanitize CPU configuration */
	call verify_cpu

	/*
	 * Derive the kernel's physical-to-virtual offset from the physical and
	 * virtual addresses of common_startup_64().
	 */
	leaq	common_startup_64(%rip), %rdi
	subq	.Lcommon_startup_64(%rip), %rdi

	/*
	 * Perform pagetable fixups. Additionally, if SME is active, encrypt
	 * the kernel and retrieve the modifier (SME encryption mask if SME
	 * is active) to be added to the initial pgdir entry that will be
	 * programmed into CR3.
	 */
	movq	%r15, %rsi
	call	__startup_64

	/* Form the CR3 value being sure to include the CR3 modifier */
	leaq	early_top_pgt(%rip), %rcx
	addq	%rcx, %rax

#ifdef CONFIG_AMD_MEM_ENCRYPT
	mov	%rax, %rdi

	/*
	 * For SEV guests: Verify that the C-bit is correct. A malicious
	 * hypervisor could lie about the C-bit position to perform a ROP
	 * attack on the guest by writing to the unencrypted stack and wait for
	 * the next RET instruction.
	 */
	call	sev_verify_cbit
#endif

	/*
	 * Switch to early_top_pgt which still has the identity mappings
	 * present.
	 */
	movq	%rax, %cr3

	/* Branch to the common startup code at its kernel virtual address */
	ANNOTATE_RETPOLINE_SAFE
	jmp	*.Lcommon_startup_64(%rip)
SYM_CODE_END(startup_64)

	__INITRODATA
SYM_DATA_LOCAL(.Lcommon_startup_64, .quad common_startup_64)

	.text
SYM_CODE_START(secondary_startup_64)
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR
	/*
	 * At this point the CPU runs in 64bit mode CS.L = 1 CS.D = 0,
	 * and someone has loaded a mapped page table.
	 *
	 * We come here either from startup_64 (using physical addresses)
	 * or from trampoline.S (using virtual addresses).
	 *
	 * Using virtual addresses from trampoline.S removes the need
	 * to have any identity mapped pages in the kernel page table
	 * after the boot processor executes this code.
	 */

	/* Sanitize CPU configuration */
	call verify_cpu

	/*
	 * The secondary_startup_64_no_verify entry point is only used by
	 * SEV-ES guests. In those guests the call to verify_cpu() would cause
	 * #VC exceptions which can not be handled at this stage of secondary
	 * CPU bringup.
	 *
	 * All non SEV-ES systems, especially Intel systems, need to execute
	 * verify_cpu() above to make sure NX is enabled.
	 */
SYM_INNER_LABEL(secondary_startup_64_no_verify, SYM_L_GLOBAL)
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR

	/* Clear %R15 which holds the boot_params pointer on the boot CPU */
	xorl	%r15d, %r15d

	/* Derive the runtime physical address of init_top_pgt[] */
	movq	phys_base(%rip), %rax
	addq	$(init_top_pgt - __START_KERNEL_map), %rax

	/*
	 * Retrieve the modifier (SME encryption mask if SME is active) to be
	 * added to the initial pgdir entry that will be programmed into CR3.
	 */
#ifdef CONFIG_AMD_MEM_ENCRYPT
	addq	sme_me_mask(%rip), %rax
#endif
	/*
	 * Switch to the init_top_pgt here, away from the trampoline_pgd and
	 * unmap the identity mapped ranges.
	 */
	movq	%rax, %cr3

SYM_INNER_LABEL(common_startup_64, SYM_L_LOCAL)
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR

	/*
	 * Create a mask of CR4 bits to preserve. Omit PGE in order to flush
	 * global 1:1 translations from the TLBs.
	 *
	 * From the SDM:
	 * "If CR4.PGE is changing from 0 to 1, there were no global TLB
	 *  entries before the execution; if CR4.PGE is changing from 1 to 0,
	 *  there will be no global TLB entries after the execution."
	 */
	movl	$(X86_CR4_PAE | X86_CR4_LA57), %edx
#ifdef CONFIG_X86_MCE
	/*
	 * Preserve CR4.MCE if the kernel will enable #MC support.
	 * Clearing MCE may fault in some environments (that also force #MC
	 * support). Any machine check that occurs before #MC support is fully
	 * configured will crash the system regardless of the CR4.MCE value set
	 * here.
	 */
	orl	$X86_CR4_MCE, %edx
#endif
	movq	%cr4, %rcx
	andl	%edx, %ecx

	/* Even if ignored in long mode, set PSE uniformly on all logical CPUs. */
	btsl	$X86_CR4_PSE_BIT, %ecx
	movq	%rcx, %cr4

	/*
	 * Set CR4.PGE to re-enable global translations.
	 */
	btsl	$X86_CR4_PGE_BIT, %ecx
	movq	%rcx, %cr4

#ifdef CONFIG_SMP
	/*
	 * For parallel boot, the APIC ID is read from the APIC, and then
	 * used to look up the CPU number.  For booting a single CPU, the
	 * CPU number is encoded in smpboot_control.
	 *
	 * Bit 31	STARTUP_READ_APICID (Read APICID from APIC)
	 * Bit 0-23	CPU# if STARTUP_xx flags are not set
	 */
	movl	smpboot_control(%rip), %ecx
	testl	$STARTUP_READ_APICID, %ecx
	jnz	.Lread_apicid
	/*
	 * No control bit set, single CPU bringup. CPU number is provided
	 * in bit 0-23. This is also the boot CPU case (CPU number 0).
	 */
	andl	$(~STARTUP_PARALLEL_MASK), %ecx
	jmp	.Lsetup_cpu

.Lread_apicid:
	/* Check whether X2APIC mode is already enabled */
	mov	$MSR_IA32_APICBASE, %ecx
	rdmsr
	testl	$X2APIC_ENABLE, %eax
	jnz	.Lread_apicid_msr

#ifdef CONFIG_X86_X2APIC
	/*
	 * If system is in X2APIC mode then MMIO base might not be
	 * mapped causing the MMIO read below to fault. Faults can't
	 * be handled at that point.
	 */
	cmpl	$0, x2apic_mode(%rip)
	jz	.Lread_apicid_mmio

	/* Force the AP into X2APIC mode. */
	orl	$X2APIC_ENABLE, %eax
	wrmsr
	jmp	.Lread_apicid_msr
#endif

.Lread_apicid_mmio:
	/* Read the APIC ID from the fix-mapped MMIO space. */
	movq	apic_mmio_base(%rip), %rcx
	addq	$APIC_ID, %rcx
	movl	(%rcx), %eax
	shr	$24, %eax
	jmp	.Llookup_AP

.Lread_apicid_msr:
	mov	$APIC_X2APIC_ID_MSR, %ecx
	rdmsr

.Llookup_AP:
	/* EAX contains the APIC ID of the current CPU */
	xorl	%ecx, %ecx
	leaq	cpuid_to_apicid(%rip), %rbx

.Lfind_cpunr:
	cmpl	(%rbx,%rcx,4), %eax
	jz	.Lsetup_cpu
	inc	%ecx
#ifdef CONFIG_FORCE_NR_CPUS
	cmpl	$NR_CPUS, %ecx
#else
	cmpl	nr_cpu_ids(%rip), %ecx
#endif
	jb	.Lfind_cpunr

	/*  APIC ID not found in the table. Drop the trampoline lock and bail. */
	movq	trampoline_lock(%rip), %rax
	movl	$0, (%rax)

1:	cli
	hlt
	jmp	1b

.Lsetup_cpu:
	/* Get the per cpu offset for the given CPU# which is in ECX */
	movq	__per_cpu_offset(,%rcx,8), %rdx
#else
	xorl	%edx, %edx /* zero-extended to clear all of RDX */
#endif /* CONFIG_SMP */

	/*
	 * Setup a boot time stack - Any secondary CPU will have lost its stack
	 * by now because the cr3-switch above unmaps the real-mode stack.
	 *
	 * RDX contains the per-cpu offset
	 */
	movq	pcpu_hot + X86_current_task(%rdx), %rax
	movq	TASK_threadsp(%rax), %rsp

	/*
	 * Now that this CPU is running on its own stack, drop the realmode
	 * protection. For the boot CPU the pointer is NULL!
	 */
	movq	trampoline_lock(%rip), %rax
	testq	%rax, %rax
	jz	.Lsetup_gdt
	movl	$0, (%rax)

.Lsetup_gdt:
	/*
	 * We must switch to a new descriptor in kernel space for the GDT
	 * because soon the kernel won't have access anymore to the userspace
	 * addresses where we're currently running on. We have to do that here
	 * because in 32bit we couldn't load a 64bit linear address.
	 */
	subq	$16, %rsp
	movw	$(GDT_SIZE-1), (%rsp)
	leaq	gdt_page(%rdx), %rax
	movq	%rax, 2(%rsp)
	lgdt	(%rsp)
	addq	$16, %rsp

	/* set up data segments */
	xorl %eax,%eax
	movl %eax,%ds
	movl %eax,%ss
	movl %eax,%es

	/*
	 * We don't really need to load %fs or %gs, but load them anyway
	 * to kill any stale realmode selectors.  This allows execution
	 * under VT hardware.
	 */
	movl %eax,%fs
	movl %eax,%gs

	/* Set up %gs.
	 *
	 * The base of %gs always points to fixed_percpu_data. If the
	 * stack protector canary is enabled, it is located at %gs:40.
	 * Note that, on SMP, the boot cpu uses init data section until
	 * the per cpu areas are set up.
	 */
	movl	$MSR_GS_BASE,%ecx
#ifndef CONFIG_SMP
	leaq	INIT_PER_CPU_VAR(fixed_percpu_data)(%rip), %rdx
#endif
	movl	%edx, %eax
	shrq	$32, %rdx
	wrmsr

	/* Setup and Load IDT */
	call	early_setup_idt

	/* Check if nx is implemented */
	movl	$0x80000001, %eax
	cpuid
	movl	%edx,%edi

	/* Setup EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	rdmsr
	/*
	 * Preserve current value of EFER for comparison and to skip
	 * EFER writes if no change was made (for TDX guest)
	 */
	movl    %eax, %edx
	btsl	$_EFER_SCE, %eax	/* Enable System Call */
	btl	$20,%edi		/* No Execute supported? */
	jnc     1f
	btsl	$_EFER_NX, %eax
	btsq	$_PAGE_BIT_NX,early_pmd_flags(%rip)

	/* Avoid writing EFER if no change was made (for TDX guest) */
1:	cmpl	%edx, %eax
	je	1f
	xor	%edx, %edx
	wrmsr				/* Make changes effective */
1:
	/* Setup cr0 */
	movl	$CR0_STATE, %eax
	/* Make changes effective */
	movq	%rax, %cr0

	/* zero EFLAGS after setting rsp */
	pushq $0
	popfq

	/* Pass the boot_params pointer as first argument */
	movq	%r15, %rdi

.Ljump_to_C_code:
	xorl	%ebp, %ebp	# clear frame pointer
	ANNOTATE_RETPOLINE_SAFE
	callq	*initial_code(%rip)
	ud2
SYM_CODE_END(secondary_startup_64)

#include "verify_cpu.S"
#include "sev_verify_cbit.S"

#if defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_AMD_MEM_ENCRYPT)
/*
 * Entry point for soft restart of a CPU. Invoked from xxx_play_dead() for
 * restarting the boot CPU or for restarting SEV guest CPUs after CPU hot
 * unplug. Everything is set up already except the stack.
 */
SYM_CODE_START(soft_restart_cpu)
	ANNOTATE_NOENDBR
	UNWIND_HINT_END_OF_STACK

	/* Find the idle task stack */
	movq	PER_CPU_VAR(pcpu_hot + X86_current_task), %rcx
	movq	TASK_threadsp(%rcx), %rsp

	jmp	.Ljump_to_C_code
SYM_CODE_END(soft_restart_cpu)
#endif

#ifdef CONFIG_AMD_MEM_ENCRYPT
/*
 * VC Exception handler used during early boot when running on kernel
 * addresses, but before the switch to the idt_table can be made.
 * The early_idt_handler_array can't be used here because it calls into a lot
 * of __init code and this handler is also used during CPU offlining/onlining.
 * Therefore this handler ends up in the .text section so that it stays around
 * when .init.text is freed.
 */
SYM_CODE_START_NOALIGN(vc_boot_ghcb)
	UNWIND_HINT_IRET_REGS offset=8
	ENDBR

	/* Build pt_regs */
	PUSH_AND_CLEAR_REGS

	/* Call C handler */
	movq    %rsp, %rdi
	movq	ORIG_RAX(%rsp), %rsi
	movq	initial_vc_handler(%rip), %rax
	ANNOTATE_RETPOLINE_SAFE
	call	*%rax

	/* Unwind pt_regs */
	POP_REGS

	/* Remove Error Code */
	addq    $8, %rsp

	iretq
SYM_CODE_END(vc_boot_ghcb)
#endif

	/* Both SMP bootup and ACPI suspend change these variables */
	__REFDATA
	.balign	8
SYM_DATA(initial_code,	.quad x86_64_start_kernel)
#ifdef CONFIG_AMD_MEM_ENCRYPT
SYM_DATA(initial_vc_handler,	.quad handle_vc_boot_ghcb)
#endif

SYM_DATA(trampoline_lock, .quad 0);
	__FINITDATA

	__INIT
SYM_CODE_START(early_idt_handler_array)
	i = 0
	.rept NUM_EXCEPTION_VECTORS
	.if ((EXCEPTION_ERRCODE_MASK >> i) & 1) == 0
		UNWIND_HINT_IRET_REGS
		ENDBR
		pushq $0	# Dummy error code, to make stack frame uniform
	.else
		UNWIND_HINT_IRET_REGS offset=8
		ENDBR
	.endif
	pushq $i		# 72(%rsp) Vector number
	jmp early_idt_handler_common
	UNWIND_HINT_IRET_REGS
	i = i + 1
	.fill early_idt_handler_array + i*EARLY_IDT_HANDLER_SIZE - ., 1, 0xcc
	.endr
SYM_CODE_END(early_idt_handler_array)
	ANNOTATE_NOENDBR // early_idt_handler_array[NUM_EXCEPTION_VECTORS]

SYM_CODE_START_LOCAL(early_idt_handler_common)
	UNWIND_HINT_IRET_REGS offset=16
	/*
	 * The stack is the hardware frame, an error code or zero, and the
	 * vector number.
	 */
	cld

	incl early_recursion_flag(%rip)

	/* The vector number is currently in the pt_regs->di slot. */
	pushq %rsi				/* pt_regs->si */
	movq 8(%rsp), %rsi			/* RSI = vector number */
	movq %rdi, 8(%rsp)			/* pt_regs->di = RDI */
	pushq %rdx				/* pt_regs->dx */
	pushq %rcx				/* pt_regs->cx */
	pushq %rax				/* pt_regs->ax */
	pushq %r8				/* pt_regs->r8 */
	pushq %r9				/* pt_regs->r9 */
	pushq %r10				/* pt_regs->r10 */
	pushq %r11				/* pt_regs->r11 */
	pushq %rbx				/* pt_regs->bx */
	pushq %rbp				/* pt_regs->bp */
	pushq %r12				/* pt_regs->r12 */
	pushq %r13				/* pt_regs->r13 */
	pushq %r14				/* pt_regs->r14 */
	pushq %r15				/* pt_regs->r15 */
	UNWIND_HINT_REGS

	movq %rsp,%rdi		/* RDI = pt_regs; RSI is already trapnr */
	call do_early_exception

	decl early_recursion_flag(%rip)
	jmp restore_regs_and_return_to_kernel
SYM_CODE_END(early_idt_handler_common)

#ifdef CONFIG_AMD_MEM_ENCRYPT
/*
 * VC Exception handler used during very early boot. The
 * early_idt_handler_array can't be used because it returns via the
 * paravirtualized INTERRUPT_RETURN and pv-ops don't work that early.
 *
 * XXX it does, fix this.
 *
 * This handler will end up in the .init.text section and not be
 * available to boot secondary CPUs.
 */
SYM_CODE_START_NOALIGN(vc_no_ghcb)
	UNWIND_HINT_IRET_REGS offset=8
	ENDBR

	/* Build pt_regs */
	PUSH_AND_CLEAR_REGS

	/* Call C handler */
	movq    %rsp, %rdi
	movq	ORIG_RAX(%rsp), %rsi
	call    do_vc_no_ghcb

	/* Unwind pt_regs */
	POP_REGS

	/* Remove Error Code */
	addq    $8, %rsp

	/* Pure iret required here - don't use INTERRUPT_RETURN */
	iretq
SYM_CODE_END(vc_no_ghcb)
#endif

#ifdef CONFIG_MITIGATION_PAGE_TABLE_ISOLATION
/*
 * Each PGD needs to be 8k long and 8k aligned.  We do not
 * ever go out to userspace with these, so we do not
 * strictly *need* the second page, but this allows us to
 * have a single set_pgd() implementation that does not
 * need to worry about whether it has 4k or 8k to work
 * with.
 *
 * This ensures PGDs are 8k long:
 */
#define PTI_USER_PGD_FILL	512
/* This ensures they are 8k-aligned: */
#define SYM_DATA_START_PTI_ALIGNED(name) \
	SYM_START(name, SYM_L_GLOBAL, .balign 2 * PAGE_SIZE)
#else
#define SYM_DATA_START_PTI_ALIGNED(name) \
	SYM_DATA_START_PAGE_ALIGNED(name)
#define PTI_USER_PGD_FILL	0
#endif

	__INITDATA
	.balign 4

SYM_DATA_START_PTI_ALIGNED(early_top_pgt)
	.fill	511,8,0
	.quad	level3_kernel_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
	.fill	PTI_USER_PGD_FILL,8,0
SYM_DATA_END(early_top_pgt)

SYM_DATA_START_PAGE_ALIGNED(early_dynamic_pgts)
	.fill	512*EARLY_DYNAMIC_PAGE_TABLES,8,0
SYM_DATA_END(early_dynamic_pgts)

SYM_DATA(early_recursion_flag, .long 0)

	.data

#if defined(CONFIG_XEN_PV) || defined(CONFIG_PVH)
SYM_DATA_START_PTI_ALIGNED(init_top_pgt)
	.quad   level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.org    init_top_pgt + L4_PAGE_OFFSET*8, 0
	.quad   level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.org    init_top_pgt + L4_START_KERNEL*8, 0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad   level3_kernel_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
	.fill	PTI_USER_PGD_FILL,8,0
SYM_DATA_END(init_top_pgt)

SYM_DATA_START_PAGE_ALIGNED(level3_ident_pgt)
	.quad	level2_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.fill	511, 8, 0
SYM_DATA_END(level3_ident_pgt)
SYM_DATA_START_PAGE_ALIGNED(level2_ident_pgt)
	/*
	 * Since I easily can, map the first 1G.
	 * Don't set NX because code runs from these pages.
	 *
	 * Note: This sets _PAGE_GLOBAL despite whether
	 * the CPU supports it or it is enabled.  But,
	 * the CPU should ignore the bit.
	 */
	PMDS(0, __PAGE_KERNEL_IDENT_LARGE_EXEC, PTRS_PER_PMD)
SYM_DATA_END(level2_ident_pgt)
#else
SYM_DATA_START_PTI_ALIGNED(init_top_pgt)
	.fill	512,8,0
	.fill	PTI_USER_PGD_FILL,8,0
SYM_DATA_END(init_top_pgt)
#endif

#ifdef CONFIG_X86_5LEVEL
SYM_DATA_START_PAGE_ALIGNED(level4_kernel_pgt)
	.fill	511,8,0
	.quad	level3_kernel_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
SYM_DATA_END(level4_kernel_pgt)
#endif

SYM_DATA_START_PAGE_ALIGNED(level3_kernel_pgt)
	.fill	L3_START_KERNEL,8,0
	/* (2^48-(2*1024*1024*1024)-((2^39)*511))/(2^30) = 510 */
	.quad	level2_kernel_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.quad	level2_fixmap_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
SYM_DATA_END(level3_kernel_pgt)

SYM_DATA_START_PAGE_ALIGNED(level2_kernel_pgt)
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
	PMDS(0, __PAGE_KERNEL_LARGE_EXEC, KERNEL_IMAGE_SIZE/PMD_SIZE)
SYM_DATA_END(level2_kernel_pgt)

SYM_DATA_START_PAGE_ALIGNED(level2_fixmap_pgt)
	.fill	(512 - 4 - FIXMAP_PMD_NUM),8,0
	pgtno = 0
	.rept (FIXMAP_PMD_NUM)
	.quad level1_fixmap_pgt + (pgtno << PAGE_SHIFT) - __START_KERNEL_map \
		+ _PAGE_TABLE_NOENC;
	pgtno = pgtno + 1
	.endr
	/* 6 MB reserved space + a 2MB hole */
	.fill	4,8,0
SYM_DATA_END(level2_fixmap_pgt)

SYM_DATA_START_PAGE_ALIGNED(level1_fixmap_pgt)
	.rept (FIXMAP_PMD_NUM)
	.fill	512,8,0
	.endr
SYM_DATA_END(level1_fixmap_pgt)

	.data
	.align 16

SYM_DATA(smpboot_control,		.long 0)

	.align 16
/* This must match the first entry in level2_kernel_pgt */
SYM_DATA(phys_base, .quad 0x0)
EXPORT_SYMBOL(phys_base)

#include "../xen/xen-head.S"

	__PAGE_ALIGNED_BSS
SYM_DATA_START_PAGE_ALIGNED(empty_zero_page)
	.skip PAGE_SIZE
SYM_DATA_END(empty_zero_page)
EXPORT_SYMBOL(empty_zero_page)

