/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/csr.h>

__INIT
ENTRY(_start)
	/* Mask all interrupts */
	csrw sie, zero

	/* Load the global pointer */
.option push
.option norelax
	la gp, __global_pointer$
.option pop

	/*
	 * Disable FPU to detect illegal usage of
	 * floating point in kernel space
	 */
	li t0, SR_FS
	csrc sstatus, t0

	/* Pick one hart to run the main boot sequence */
	la a3, hart_lottery
	li a2, 1
	amoadd.w a3, a2, (a3)
	bnez a3, .Lsecondary_start

	/* Save hart ID and DTB physical address */
	mv s0, a0
	mv s1, a1

	/* Initialize page tables and relocate to virtual addresses */
	la sp, init_thread_union + THREAD_SIZE
	call setup_vm
	call relocate

	/* Restore C environment */
	la tp, init_task
	sw s0, TASK_TI_CPU(tp)

	la sp, init_thread_union
	li a0, ASM_THREAD_SIZE
	add sp, sp, a0

	/* Start the kernel */
	mv a0, s0
	mv a1, s1
	call sbi_save
	tail start_kernel

relocate:
	/* Relocate return address */
	li a1, PAGE_OFFSET
	la a0, _start
	sub a1, a1, a0
	add ra, ra, a1

	/* Point stvec to virtual address of intruction after satp write */
	la a0, 1f
	add a0, a0, a1
	csrw stvec, a0

	/* Compute satp for kernel page tables, but don't load it yet */
	la a2, swapper_pg_dir
	srl a2, a2, PAGE_SHIFT
	li a1, SATP_MODE
	or a2, a2, a1

	/*
	 * Load trampoline page directory, which will cause us to trap to
	 * stvec if VA != PA, or simply fall through if VA == PA
	 */
	la a0, trampoline_pg_dir
	srl a0, a0, PAGE_SHIFT
	or a0, a0, a1
	sfence.vma
	csrw sptbr, a0
1:
	/* Set trap vector to spin forever to help debug */
	la a0, .Lsecondary_park
	csrw stvec, a0

	/* Reload the global pointer */
.option push
.option norelax
	la gp, __global_pointer$
.option pop

	/* Switch to kernel page tables */
	csrw sptbr, a2

	ret

.Lsecondary_start:
#ifdef CONFIG_SMP
	li a1, CONFIG_NR_CPUS
	bgeu a0, a1, .Lsecondary_park

	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw stvec, a3

	slli a3, a0, LGREG
	la a1, __cpu_up_stack_pointer
	la a2, __cpu_up_task_pointer
	add a1, a3, a1
	add a2, a3, a2

	/*
	 * This hart didn't win the lottery, so we wait for the winning hart to
	 * get far enough along the boot process that it should continue.
	 */
.Lwait_for_cpu_up:
	/* FIXME: We should WFI to save some energy here. */
	REG_L sp, (a1)
	REG_L tp, (a2)
	beqz sp, .Lwait_for_cpu_up
	beqz tp, .Lwait_for_cpu_up
	fence

	/* Enable virtual memory and relocate to virtual address */
	call relocate

	tail smp_callin
#endif

.Lsecondary_park:
	/* We lack SMP support or have too many harts, so park this hart */
	wfi
	j .Lsecondary_park
END(_start)

__PAGE_ALIGNED_BSS
	/* Empty zero page */
	.balign PAGE_SIZE
