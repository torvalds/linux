/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * X86 specific Hyper-V kdump/crash related code.
 *
 * Copyright (C) 2025, Microsoft, Inc.
 *
 */
#include <linux/linkage.h>
#include <asm/alternative.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/nospec-branch.h>

/*
 * void noreturn hv_crash_asm32(arg1)
 *    arg1 == edi == 32bit PA of struct hv_crash_tramp_data
 *
 * The hypervisor jumps here upon devirtualization in protected mode. This
 * code gets copied to a page in the low 4G ie, 32bit space so it can run
 * in the protected mode. Hence we cannot use any compile/link time offsets or
 * addresses. It restores long mode via temporary gdt and page tables and
 * eventually jumps to kernel code entry at HV_CRASHDATA_OFFS_C_entry.
 *
 * PreCondition (ie, Hypervisor call back ABI):
 *  o CR0 is set to 0x0021: PE(prot mode) and NE are set, paging is disabled
 *  o CR4 is set to 0x0
 *  o IA32_EFER is set to 0x901 (SCE and NXE are set)
 *  o EDI is set to the Arg passed to HVCALL_DISABLE_HYP_EX.
 *  o CS, DS, ES, FS, GS are all initialized with a base of 0 and limit 0xFFFF
 *  o IDTR, TR and GDTR are initialized with a base of 0 and limit of 0xFFFF
 *  o LDTR is initialized as invalid (limit of 0)
 *  o MSR PAT is power on default.
 *  o Other state/registers are cleared. All TLBs flushed.
 */

#define HV_CRASHDATA_OFFS_TRAMPCR3    0x0    /*  0 */
#define HV_CRASHDATA_OFFS_KERNCR3     0x8    /*  8 */
#define HV_CRASHDATA_OFFS_GDTRLIMIT  0x12    /* 18 */
#define HV_CRASHDATA_OFFS_CS_JMPTGT  0x28    /* 40 */
#define HV_CRASHDATA_OFFS_C_entry    0x30    /* 48 */

	.text
	.code32

SYM_CODE_START(hv_crash_asm32)
	UNWIND_HINT_UNDEFINED
	ENDBR
	movl	$X86_CR4_PAE, %ecx
	movl	%ecx, %cr4

	movl %edi, %ebx
	add $HV_CRASHDATA_OFFS_TRAMPCR3, %ebx
	movl %cs:(%ebx), %eax
	movl %eax, %cr3

	/* Setup EFER for long mode now */
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr

	/* Turn paging on using the temp 32bit trampoline page table */
	movl %cr0, %eax
	orl $(X86_CR0_PG), %eax
	movl %eax, %cr0

	/* since kernel cr3 could be above 4G, we need to be in the long mode
	 * before we can load 64bits of the kernel cr3. We use a temp gdt for
	 * that with CS.L=1 and CS.D=0 */
	mov %edi, %eax
	add $HV_CRASHDATA_OFFS_GDTRLIMIT, %eax
	lgdtl %cs:(%eax)

	/* not done yet, restore CS now to switch to CS.L=1 */
	mov %edi, %eax
	add $HV_CRASHDATA_OFFS_CS_JMPTGT, %eax
	ljmp %cs:*(%eax)
SYM_CODE_END(hv_crash_asm32)

	/* we now run in full 64bit IA32-e long mode, CS.L=1 and CS.D=0 */
	.code64
	.balign 8
SYM_CODE_START(hv_crash_asm64)
	UNWIND_HINT_UNDEFINED
	ENDBR
	/* restore kernel page tables so we can jump to kernel code */
	mov %edi, %eax
	add $HV_CRASHDATA_OFFS_KERNCR3, %eax
	movq %cs:(%eax), %rbx
	movq %rbx, %cr3

	mov %edi, %eax
	add $HV_CRASHDATA_OFFS_C_entry, %eax
	movq %cs:(%eax), %rbx
	ANNOTATE_RETPOLINE_SAFE
	jmp *%rbx

	int $3

SYM_INNER_LABEL(hv_crash_asm_end, SYM_L_GLOBAL)
SYM_CODE_END(hv_crash_asm64)
