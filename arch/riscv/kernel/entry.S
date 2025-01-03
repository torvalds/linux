/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/scs.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/errata_list.h>
#include <linux/sizes.h>

	.section .irqentry.text, "ax"

.macro new_vmalloc_check
	REG_S 	a0, TASK_TI_A0(tp)
	csrr 	a0, CSR_CAUSE
	/* Exclude IRQs */
	blt  	a0, zero, .Lnew_vmalloc_restore_context_a0

	REG_S 	a1, TASK_TI_A1(tp)
	/* Only check new_vmalloc if we are in page/protection fault */
	li   	a1, EXC_LOAD_PAGE_FAULT
	beq  	a0, a1, .Lnew_vmalloc_kernel_address
	li   	a1, EXC_STORE_PAGE_FAULT
	beq  	a0, a1, .Lnew_vmalloc_kernel_address
	li   	a1, EXC_INST_PAGE_FAULT
	bne  	a0, a1, .Lnew_vmalloc_restore_context_a1

.Lnew_vmalloc_kernel_address:
	/* Is it a kernel address? */
	csrr 	a0, CSR_TVAL
	bge 	a0, zero, .Lnew_vmalloc_restore_context_a1

	/* Check if a new vmalloc mapping appeared that could explain the trap */
	REG_S	a2, TASK_TI_A2(tp)
	/*
	 * Computes:
	 * a0 = &new_vmalloc[BIT_WORD(cpu)]
	 * a1 = BIT_MASK(cpu)
	 */
	REG_L 	a2, TASK_TI_CPU(tp)
	/*
	 * Compute the new_vmalloc element position:
	 * (cpu / 64) * 8 = (cpu >> 6) << 3
	 */
	srli	a1, a2, 6
	slli	a1, a1, 3
	la	a0, new_vmalloc
	add	a0, a0, a1
	/*
	 * Compute the bit position in the new_vmalloc element:
	 * bit_pos = cpu % 64 = cpu - (cpu / 64) * 64 = cpu - (cpu >> 6) << 6
	 * 	   = cpu - ((cpu >> 6) << 3) << 3
	 */
	slli	a1, a1, 3
	sub	a1, a2, a1
	/* Compute the "get mask": 1 << bit_pos */
	li	a2, 1
	sll	a1, a2, a1

	/* Check the value of new_vmalloc for this cpu */
	REG_L	a2, 0(a0)
	and	a2, a2, a1
	beq	a2, zero, .Lnew_vmalloc_restore_context

	/* Atomically reset the current cpu bit in new_vmalloc */
	amoxor.d	a0, a1, (a0)

	/* Only emit a sfence.vma if the uarch caches invalid entries */
	ALTERNATIVE("sfence.vma", "nop", 0, RISCV_ISA_EXT_SVVPTC, 1)

	REG_L	a0, TASK_TI_A0(tp)
	REG_L	a1, TASK_TI_A1(tp)
	REG_L	a2, TASK_TI_A2(tp)
	csrw	CSR_SCRATCH, x0
	sret

.Lnew_vmalloc_restore_context:
	REG_L 	a2, TASK_TI_A2(tp)
.Lnew_vmalloc_restore_context_a1:
	REG_L 	a1, TASK_TI_A1(tp)
.Lnew_vmalloc_restore_context_a0:
	REG_L	a0, TASK_TI_A0(tp)
.endm


SYM_CODE_START(handle_exception)
	/*
	 * If coming from userspace, preserve the user thread pointer and load
	 * the kernel thread pointer.  If we came from the kernel, the scratch
	 * register will contain 0, and we should continue on the current TP.
	 */
	csrrw tp, CSR_SCRATCH, tp
	bnez tp, .Lsave_context

.Lrestore_kernel_tpsp:
	csrr tp, CSR_SCRATCH

#ifdef CONFIG_64BIT
	/*
	 * The RISC-V kernel does not eagerly emit a sfence.vma after each
	 * new vmalloc mapping, which may result in exceptions:
	 * - if the uarch caches invalid entries, the new mapping would not be
	 *   observed by the page table walker and an invalidation is needed.
	 * - if the uarch does not cache invalid entries, a reordered access
	 *   could "miss" the new mapping and traps: in that case, we only need
	 *   to retry the access, no sfence.vma is required.
	 */
	new_vmalloc_check
#endif

	REG_S sp, TASK_TI_KERNEL_SP(tp)

#ifdef CONFIG_VMAP_STACK
	addi sp, sp, -(PT_SIZE_ON_STACK)
	srli sp, sp, THREAD_SHIFT
	andi sp, sp, 0x1
	bnez sp, handle_kernel_stack_overflow
	REG_L sp, TASK_TI_KERNEL_SP(tp)
#endif

.Lsave_context:
	REG_S sp, TASK_TI_USER_SP(tp)
	REG_L sp, TASK_TI_KERNEL_SP(tp)
	addi sp, sp, -(PT_SIZE_ON_STACK)
	REG_S x1,  PT_RA(sp)
	REG_S x3,  PT_GP(sp)
	REG_S x5,  PT_T0(sp)
	save_from_x6_to_x31

	/*
	 * Disable user-mode memory access as it should only be set in the
	 * actual user copy routines.
	 *
	 * Disable the FPU/Vector to detect illegal usage of floating point
	 * or vector in kernel space.
	 */
	li t0, SR_SUM | SR_FS_VS

	REG_L s0, TASK_TI_USER_SP(tp)
	csrrc s1, CSR_STATUS, t0
	csrr s2, CSR_EPC
	csrr s3, CSR_TVAL
	csrr s4, CSR_CAUSE
	csrr s5, CSR_SCRATCH
	REG_S s0, PT_SP(sp)
	REG_S s1, PT_STATUS(sp)
	REG_S s2, PT_EPC(sp)
	REG_S s3, PT_BADADDR(sp)
	REG_S s4, PT_CAUSE(sp)
	REG_S s5, PT_TP(sp)

	/*
	 * Set the scratch register to 0, so that if a recursive exception
	 * occurs, the exception vector knows it came from the kernel
	 */
	csrw CSR_SCRATCH, x0

	/* Load the global pointer */
	load_global_pointer

	/* Load the kernel shadow call stack pointer if coming from userspace */
	scs_load_current_if_task_changed s5

#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
	move a0, sp
	call riscv_v_context_nesting_start
#endif
	move a0, sp /* pt_regs */

	/*
	 * MSB of cause differentiates between
	 * interrupts and exceptions
	 */
	bge s4, zero, 1f

	/* Handle interrupts */
	call do_irq
	j ret_from_exception
1:
	/* Handle other exceptions */
	slli t0, s4, RISCV_LGPTR
	la t1, excp_vect_table
	la t2, excp_vect_table_end
	add t0, t1, t0
	/* Check if exception code lies within bounds */
	bgeu t0, t2, 3f
	REG_L t1, 0(t0)
2:	jalr t1
	j ret_from_exception
3:

	la t1, do_trap_unknown
	j 2b
SYM_CODE_END(handle_exception)
ASM_NOKPROBE(handle_exception)

/*
 * The ret_from_exception must be called with interrupt disabled. Here is the
 * caller list:
 *  - handle_exception
 *  - ret_from_fork
 */
SYM_CODE_START_NOALIGN(ret_from_exception)
	REG_L s0, PT_STATUS(sp)
#ifdef CONFIG_RISCV_M_MODE
	/* the MPP value is too large to be used as an immediate arg for addi */
	li t0, SR_MPP
	and s0, s0, t0
#else
	andi s0, s0, SR_SPP
#endif
	bnez s0, 1f

#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
	call	stackleak_erase_on_task_stack
#endif

	/* Save unwound kernel stack pointer in thread_info */
	addi s0, sp, PT_SIZE_ON_STACK
	REG_S s0, TASK_TI_KERNEL_SP(tp)

	/* Save the kernel shadow call stack pointer */
	scs_save_current

	/*
	 * Save TP into the scratch register , so we can find the kernel data
	 * structures again.
	 */
	csrw CSR_SCRATCH, tp
1:
#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
	move a0, sp
	call riscv_v_context_nesting_end
#endif
	REG_L a0, PT_STATUS(sp)
	/*
	 * The current load reservation is effectively part of the processor's
	 * state, in the sense that load reservations cannot be shared between
	 * different hart contexts.  We can't actually save and restore a load
	 * reservation, so instead here we clear any existing reservation --
	 * it's always legal for implementations to clear load reservations at
	 * any point (as long as the forward progress guarantee is kept, but
	 * we'll ignore that here).
	 *
	 * Dangling load reservations can be the result of taking a trap in the
	 * middle of an LR/SC sequence, but can also be the result of a taken
	 * forward branch around an SC -- which is how we implement CAS.  As a
	 * result we need to clear reservations between the last CAS and the
	 * jump back to the new context.  While it is unlikely the store
	 * completes, implementations are allowed to expand reservations to be
	 * arbitrarily large.
	 */
	REG_L  a2, PT_EPC(sp)
	REG_SC x0, a2, PT_EPC(sp)

	csrw CSR_STATUS, a0
	csrw CSR_EPC, a2

	REG_L x1,  PT_RA(sp)
	REG_L x3,  PT_GP(sp)
	REG_L x4,  PT_TP(sp)
	REG_L x5,  PT_T0(sp)
	restore_from_x6_to_x31

	REG_L x2,  PT_SP(sp)

#ifdef CONFIG_RISCV_M_MODE
	mret
#else
	sret
#endif
SYM_INNER_LABEL(ret_from_exception_end, SYM_L_GLOBAL)
SYM_CODE_END(ret_from_exception)
ASM_NOKPROBE(ret_from_exception)

#ifdef CONFIG_VMAP_STACK
SYM_CODE_START_LOCAL(handle_kernel_stack_overflow)
	/* we reach here from kernel context, sscratch must be 0 */
	csrrw x31, CSR_SCRATCH, x31
	asm_per_cpu sp, overflow_stack, x31
	li x31, OVERFLOW_STACK_SIZE
	add sp, sp, x31
	/* zero out x31 again and restore x31 */
	xor x31, x31, x31
	csrrw x31, CSR_SCRATCH, x31

	addi sp, sp, -(PT_SIZE_ON_STACK)

	//save context to overflow stack
	REG_S x1,  PT_RA(sp)
	REG_S x3,  PT_GP(sp)
	REG_S x5,  PT_T0(sp)
	save_from_x6_to_x31

	REG_L s0, TASK_TI_KERNEL_SP(tp)
	csrr s1, CSR_STATUS
	csrr s2, CSR_EPC
	csrr s3, CSR_TVAL
	csrr s4, CSR_CAUSE
	csrr s5, CSR_SCRATCH
	REG_S s0, PT_SP(sp)
	REG_S s1, PT_STATUS(sp)
	REG_S s2, PT_EPC(sp)
	REG_S s3, PT_BADADDR(sp)
	REG_S s4, PT_CAUSE(sp)
	REG_S s5, PT_TP(sp)
	move a0, sp
	tail handle_bad_stack
SYM_CODE_END(handle_kernel_stack_overflow)
ASM_NOKPROBE(handle_kernel_stack_overflow)
#endif

SYM_CODE_START(ret_from_fork)
	call schedule_tail
	beqz s0, 1f	/* not from kernel thread */
	/* Call fn(arg) */
	move a0, s1
	jalr s0
1:
	move a0, sp /* pt_regs */
	call syscall_exit_to_user_mode
	j ret_from_exception
SYM_CODE_END(ret_from_fork)

#ifdef CONFIG_IRQ_STACKS
/*
 * void call_on_irq_stack(struct pt_regs *regs,
 * 		          void (*func)(struct pt_regs *));
 *
 * Calls func(regs) using the per-CPU IRQ stack.
 */
SYM_FUNC_START(call_on_irq_stack)
	/* Create a frame record to save ra and s0 (fp) */
	addi	sp, sp, -STACKFRAME_SIZE_ON_STACK
	REG_S	ra, STACKFRAME_RA(sp)
	REG_S	s0, STACKFRAME_FP(sp)
	addi	s0, sp, STACKFRAME_SIZE_ON_STACK

	/* Switch to the per-CPU shadow call stack */
	scs_save_current
	scs_load_irq_stack t0

	/* Switch to the per-CPU IRQ stack and call the handler */
	load_per_cpu t0, irq_stack_ptr, t1
	li	t1, IRQ_STACK_SIZE
	add	sp, t0, t1
	jalr	a1

	/* Switch back to the thread shadow call stack */
	scs_load_current

	/* Switch back to the thread stack and restore ra and s0 */
	addi	sp, s0, -STACKFRAME_SIZE_ON_STACK
	REG_L	ra, STACKFRAME_RA(sp)
	REG_L	s0, STACKFRAME_FP(sp)
	addi	sp, sp, STACKFRAME_SIZE_ON_STACK

	ret
SYM_FUNC_END(call_on_irq_stack)
#endif /* CONFIG_IRQ_STACKS */

/*
 * Integer register context switch
 * The callee-saved registers must be saved and restored.
 *
 *   a0: previous task_struct (must be preserved across the switch)
 *   a1: next task_struct
 *
 * The value of a0 and a1 must be preserved by this function, as that's how
 * arguments are passed to schedule_tail.
 */
SYM_FUNC_START(__switch_to)
	/* Save context into prev->thread */
	li    a4,  TASK_THREAD_RA
	add   a3, a0, a4
	add   a4, a1, a4
	REG_S ra,  TASK_THREAD_RA_RA(a3)
	REG_S sp,  TASK_THREAD_SP_RA(a3)
	REG_S s0,  TASK_THREAD_S0_RA(a3)
	REG_S s1,  TASK_THREAD_S1_RA(a3)
	REG_S s2,  TASK_THREAD_S2_RA(a3)
	REG_S s3,  TASK_THREAD_S3_RA(a3)
	REG_S s4,  TASK_THREAD_S4_RA(a3)
	REG_S s5,  TASK_THREAD_S5_RA(a3)
	REG_S s6,  TASK_THREAD_S6_RA(a3)
	REG_S s7,  TASK_THREAD_S7_RA(a3)
	REG_S s8,  TASK_THREAD_S8_RA(a3)
	REG_S s9,  TASK_THREAD_S9_RA(a3)
	REG_S s10, TASK_THREAD_S10_RA(a3)
	REG_S s11, TASK_THREAD_S11_RA(a3)
	/* Save the kernel shadow call stack pointer */
	scs_save_current
	/* Restore context from next->thread */
	REG_L ra,  TASK_THREAD_RA_RA(a4)
	REG_L sp,  TASK_THREAD_SP_RA(a4)
	REG_L s0,  TASK_THREAD_S0_RA(a4)
	REG_L s1,  TASK_THREAD_S1_RA(a4)
	REG_L s2,  TASK_THREAD_S2_RA(a4)
	REG_L s3,  TASK_THREAD_S3_RA(a4)
	REG_L s4,  TASK_THREAD_S4_RA(a4)
	REG_L s5,  TASK_THREAD_S5_RA(a4)
	REG_L s6,  TASK_THREAD_S6_RA(a4)
	REG_L s7,  TASK_THREAD_S7_RA(a4)
	REG_L s8,  TASK_THREAD_S8_RA(a4)
	REG_L s9,  TASK_THREAD_S9_RA(a4)
	REG_L s10, TASK_THREAD_S10_RA(a4)
	REG_L s11, TASK_THREAD_S11_RA(a4)
	/* The offset of thread_info in task_struct is zero. */
	move tp, a1
	/* Switch to the next shadow call stack */
	scs_load_current
	ret
SYM_FUNC_END(__switch_to)

#ifndef CONFIG_MMU
#define do_page_fault do_trap_unknown
#endif

	.section ".rodata"
	.align LGREG
	/* Exception vector table */
SYM_DATA_START_LOCAL(excp_vect_table)
	RISCV_PTR do_trap_insn_misaligned
	ALT_INSN_FAULT(RISCV_PTR do_trap_insn_fault)
	RISCV_PTR do_trap_insn_illegal
	RISCV_PTR do_trap_break
	RISCV_PTR do_trap_load_misaligned
	RISCV_PTR do_trap_load_fault
	RISCV_PTR do_trap_store_misaligned
	RISCV_PTR do_trap_store_fault
	RISCV_PTR do_trap_ecall_u /* system call */
	RISCV_PTR do_trap_ecall_s
	RISCV_PTR do_trap_unknown
	RISCV_PTR do_trap_ecall_m
	/* instruciton page fault */
	ALT_PAGE_FAULT(RISCV_PTR do_page_fault)
	RISCV_PTR do_page_fault   /* load page fault */
	RISCV_PTR do_trap_unknown
	RISCV_PTR do_page_fault   /* store page fault */
SYM_DATA_END_LABEL(excp_vect_table, SYM_L_LOCAL, excp_vect_table_end)

#ifndef CONFIG_MMU
SYM_DATA_START(__user_rt_sigreturn)
	li a7, __NR_rt_sigreturn
	ecall
SYM_DATA_END(__user_rt_sigreturn)
#endif
