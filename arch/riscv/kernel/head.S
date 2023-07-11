/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/csr.h>
#include <asm/cpu_ops_sbi.h>
#include <asm/hwcap.h>
#include <asm/image.h>
#include <asm/xip_fixup.h>
#include "efi-header.S"

__HEAD
ENTRY(_start)
	/*
	 * Image header expected by Linux boot-loaders. The image header data
	 * structure is described in asm/image.h.
	 * Do not modify it without modifying the structure and all bootloaders
	 * that expects this header format!!
	 */
#ifdef CONFIG_EFI
	/*
	 * This instruction decodes to "MZ" ASCII required by UEFI.
	 */
	c.li s4,-13
	j _start_kernel
#else
	/* jump to start kernel */
	j _start_kernel
	/* reserved */
	.word 0
#endif
	.balign 8
#ifdef CONFIG_RISCV_M_MODE
	/* Image load offset (0MB) from start of RAM for M-mode */
	.dword 0
#else
#if __riscv_xlen == 64
	/* Image load offset(2MB) from start of RAM */
	.dword 0x200000
#else
	/* Image load offset(4MB) from start of RAM */
	.dword 0x400000
#endif
#endif
	/* Effective size of kernel image */
	.dword _end - _start
	.dword __HEAD_FLAGS
	.word RISCV_HEADER_VERSION
	.word 0
	.dword 0
	.ascii RISCV_IMAGE_MAGIC
	.balign 4
	.ascii RISCV_IMAGE_MAGIC2
#ifdef CONFIG_EFI
	.word pe_head_start - _start
pe_head_start:

	__EFI_PE_HEADER
#else
	.word 0
#endif

.align 2
#ifdef CONFIG_MMU
	.global relocate_enable_mmu
relocate_enable_mmu:
	/* Relocate return address */
	la a1, kernel_map
	XIP_FIXUP_OFFSET a1
	REG_L a1, KERNEL_MAP_VIRT_ADDR(a1)
	la a2, _start
	sub a1, a1, a2
	add ra, ra, a1

	/* Point stvec to virtual address of intruction after satp write */
	la a2, 1f
	add a2, a2, a1
	csrw CSR_TVEC, a2

	/* Compute satp for kernel page tables, but don't load it yet */
	srl a2, a0, PAGE_SHIFT
	la a1, satp_mode
	REG_L a1, 0(a1)
	or a2, a2, a1

	/*
	 * Load trampoline page directory, which will cause us to trap to
	 * stvec if VA != PA, or simply fall through if VA == PA.  We need a
	 * full fence here because setup_vm() just wrote these PTEs and we need
	 * to ensure the new translations are in use.
	 */
	la a0, trampoline_pg_dir
	XIP_FIXUP_OFFSET a0
	srl a0, a0, PAGE_SHIFT
	or a0, a0, a1
	sfence.vma
	csrw CSR_SATP, a0
.align 2
1:
	/* Set trap vector to spin forever to help debug */
	la a0, .Lsecondary_park
	csrw CSR_TVEC, a0

	/* Reload the global pointer */
.option push
.option norelax
	la gp, __global_pointer$
.option pop

	/*
	 * Switch to kernel page tables.  A full fence is necessary in order to
	 * avoid using the trampoline translations, which are only correct for
	 * the first superpage.  Fetching the fence is guaranteed to work
	 * because that first superpage is translated the same way.
	 */
	csrw CSR_SATP, a2
	sfence.vma

	ret
#endif /* CONFIG_MMU */
#ifdef CONFIG_SMP
	.global secondary_start_sbi
secondary_start_sbi:
	/* Mask all interrupts */
	csrw CSR_IE, zero
	csrw CSR_IP, zero

	/* Load the global pointer */
	.option push
	.option norelax
		la gp, __global_pointer$
	.option pop

	/*
	 * Disable FPU & VECTOR to detect illegal usage of
	 * floating point or vector in kernel space
	 */
	li t0, SR_FS_VS
	csrc CSR_STATUS, t0

	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw CSR_TVEC, a3

	/* a0 contains the hartid & a1 contains boot data */
	li a2, SBI_HART_BOOT_TASK_PTR_OFFSET
	XIP_FIXUP_OFFSET a2
	add a2, a2, a1
	REG_L tp, (a2)
	li a3, SBI_HART_BOOT_STACK_PTR_OFFSET
	XIP_FIXUP_OFFSET a3
	add a3, a3, a1
	REG_L sp, (a3)

.Lsecondary_start_common:

#ifdef CONFIG_MMU
	/* Enable virtual memory and relocate to virtual address */
	la a0, swapper_pg_dir
	XIP_FIXUP_OFFSET a0
	call relocate_enable_mmu
#endif
	call setup_trap_vector
	tail smp_callin
#endif /* CONFIG_SMP */

.align 2
setup_trap_vector:
	/* Set trap vector to exception handler */
	la a0, handle_exception
	csrw CSR_TVEC, a0

	/*
	 * Set sup0 scratch register to 0, indicating to exception vector that
	 * we are presently executing in kernel.
	 */
	csrw CSR_SCRATCH, zero
	ret

.align 2
.Lsecondary_park:
	/* We lack SMP support or have too many harts, so park this hart */
	wfi
	j .Lsecondary_park

END(_start)

ENTRY(_start_kernel)
	/* Mask all interrupts */
	csrw CSR_IE, zero
	csrw CSR_IP, zero

#ifdef CONFIG_RISCV_M_MODE
	/* flush the instruction cache */
	fence.i

	/* Reset all registers except ra, a0, a1 */
	call reset_regs

	/*
	 * Setup a PMP to permit access to all of memory.  Some machines may
	 * not implement PMPs, so we set up a quick trap handler to just skip
	 * touching the PMPs on any trap.
	 */
	la a0, pmp_done
	csrw CSR_TVEC, a0

	li a0, -1
	csrw CSR_PMPADDR0, a0
	li a0, (PMP_A_NAPOT | PMP_R | PMP_W | PMP_X)
	csrw CSR_PMPCFG0, a0
.align 2
pmp_done:

	/*
	 * The hartid in a0 is expected later on, and we have no firmware
	 * to hand it to us.
	 */
	csrr a0, CSR_MHARTID
#endif /* CONFIG_RISCV_M_MODE */

	/* Load the global pointer */
.option push
.option norelax
	la gp, __global_pointer$
.option pop

	/*
	 * Disable FPU & VECTOR to detect illegal usage of
	 * floating point or vector in kernel space
	 */
	li t0, SR_FS_VS
	csrc CSR_STATUS, t0

#ifdef CONFIG_RISCV_BOOT_SPINWAIT
	li t0, CONFIG_NR_CPUS
	blt a0, t0, .Lgood_cores
	tail .Lsecondary_park
.Lgood_cores:

	/* The lottery system is only required for spinwait booting method */
#ifndef CONFIG_XIP_KERNEL
	/* Pick one hart to run the main boot sequence */
	la a3, hart_lottery
	li a2, 1
	amoadd.w a3, a2, (a3)
	bnez a3, .Lsecondary_start

#else
	/* hart_lottery in flash contains a magic number */
	la a3, hart_lottery
	mv a2, a3
	XIP_FIXUP_OFFSET a2
	XIP_FIXUP_FLASH_OFFSET a3
	lw t1, (a3)
	amoswap.w t0, t1, (a2)
	/* first time here if hart_lottery in RAM is not set */
	beq t0, t1, .Lsecondary_start

#endif /* CONFIG_XIP */
#endif /* CONFIG_RISCV_BOOT_SPINWAIT */

#ifdef CONFIG_XIP_KERNEL
	la sp, _end + THREAD_SIZE
	XIP_FIXUP_OFFSET sp
	mv s0, a0
	call __copy_data

	/* Restore a0 copy */
	mv a0, s0
#endif

#ifndef CONFIG_XIP_KERNEL
	/* Clear BSS for flat non-ELF images */
	la a3, __bss_start
	la a4, __bss_stop
	ble a4, a3, clear_bss_done
clear_bss:
	REG_S zero, (a3)
	add a3, a3, RISCV_SZPTR
	blt a3, a4, clear_bss
clear_bss_done:
#endif
	/* Save hart ID and DTB physical address */
	mv s0, a0
	mv s1, a1

	la a2, boot_cpu_hartid
	XIP_FIXUP_OFFSET a2
	REG_S a0, (a2)

	/* Initialize page tables and relocate to virtual addresses */
	la tp, init_task
	la sp, init_thread_union + THREAD_SIZE
	XIP_FIXUP_OFFSET sp
	addi sp, sp, -PT_SIZE_ON_STACK
#ifdef CONFIG_BUILTIN_DTB
	la a0, __dtb_start
	XIP_FIXUP_OFFSET a0
#else
	mv a0, s1
#endif /* CONFIG_BUILTIN_DTB */
	call setup_vm
#ifdef CONFIG_MMU
	la a0, early_pg_dir
	XIP_FIXUP_OFFSET a0
	call relocate_enable_mmu
#endif /* CONFIG_MMU */

	call setup_trap_vector
	/* Restore C environment */
	la tp, init_task
	la sp, init_thread_union + THREAD_SIZE
	addi sp, sp, -PT_SIZE_ON_STACK

#ifdef CONFIG_KASAN
	call kasan_early_init
#endif
	/* Start the kernel */
	call soc_early_init
	tail start_kernel

#ifdef CONFIG_RISCV_BOOT_SPINWAIT
.Lsecondary_start:
	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw CSR_TVEC, a3

	slli a3, a0, LGREG
	la a1, __cpu_spinwait_stack_pointer
	XIP_FIXUP_OFFSET a1
	la a2, __cpu_spinwait_task_pointer
	XIP_FIXUP_OFFSET a2
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

	tail .Lsecondary_start_common
#endif /* CONFIG_RISCV_BOOT_SPINWAIT */

END(_start_kernel)

#ifdef CONFIG_RISCV_M_MODE
ENTRY(reset_regs)
	li	sp, 0
	li	gp, 0
	li	tp, 0
	li	t0, 0
	li	t1, 0
	li	t2, 0
	li	s0, 0
	li	s1, 0
	li	a2, 0
	li	a3, 0
	li	a4, 0
	li	a5, 0
	li	a6, 0
	li	a7, 0
	li	s2, 0
	li	s3, 0
	li	s4, 0
	li	s5, 0
	li	s6, 0
	li	s7, 0
	li	s8, 0
	li	s9, 0
	li	s10, 0
	li	s11, 0
	li	t3, 0
	li	t4, 0
	li	t5, 0
	li	t6, 0
	csrw	CSR_SCRATCH, 0

#ifdef CONFIG_FPU
	csrr	t0, CSR_MISA
	andi	t0, t0, (COMPAT_HWCAP_ISA_F | COMPAT_HWCAP_ISA_D)
	beqz	t0, .Lreset_regs_done_fpu

	li	t1, SR_FS
	csrs	CSR_STATUS, t1
	fmv.s.x	f0, zero
	fmv.s.x	f1, zero
	fmv.s.x	f2, zero
	fmv.s.x	f3, zero
	fmv.s.x	f4, zero
	fmv.s.x	f5, zero
	fmv.s.x	f6, zero
	fmv.s.x	f7, zero
	fmv.s.x	f8, zero
	fmv.s.x	f9, zero
	fmv.s.x	f10, zero
	fmv.s.x	f11, zero
	fmv.s.x	f12, zero
	fmv.s.x	f13, zero
	fmv.s.x	f14, zero
	fmv.s.x	f15, zero
	fmv.s.x	f16, zero
	fmv.s.x	f17, zero
	fmv.s.x	f18, zero
	fmv.s.x	f19, zero
	fmv.s.x	f20, zero
	fmv.s.x	f21, zero
	fmv.s.x	f22, zero
	fmv.s.x	f23, zero
	fmv.s.x	f24, zero
	fmv.s.x	f25, zero
	fmv.s.x	f26, zero
	fmv.s.x	f27, zero
	fmv.s.x	f28, zero
	fmv.s.x	f29, zero
	fmv.s.x	f30, zero
	fmv.s.x	f31, zero
	csrw	fcsr, 0
	/* note that the caller must clear SR_FS */
.Lreset_regs_done_fpu:
#endif /* CONFIG_FPU */

#ifdef CONFIG_RISCV_ISA_V
	csrr	t0, CSR_MISA
	li	t1, COMPAT_HWCAP_ISA_V
	and	t0, t0, t1
	beqz	t0, .Lreset_regs_done_vector

	/*
	 * Clear vector registers and reset vcsr
	 * VLMAX has a defined value, VLEN is a constant,
	 * and this form of vsetvli is defined to set vl to VLMAX.
	 */
	li	t1, SR_VS
	csrs	CSR_STATUS, t1
	csrs	CSR_VCSR, x0
	vsetvli t1, x0, e8, m8, ta, ma
	vmv.v.i v0, 0
	vmv.v.i v8, 0
	vmv.v.i v16, 0
	vmv.v.i v24, 0
	/* note that the caller must clear SR_VS */
.Lreset_regs_done_vector:
#endif /* CONFIG_RISCV_ISA_V */
	ret
END(reset_regs)
#endif /* CONFIG_RISCV_M_MODE */
