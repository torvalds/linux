/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/csr.h>
#include <asm/hwcap.h>
#include <asm/image.h>

__INIT
ENTRY(_start)
	/*
	 * Image header expected by Linux boot-loaders. The image header data
	 * structure is described in asm/image.h.
	 * Do not modify it without modifying the structure and all bootloaders
	 * that expects this header format!!
	 */
	/* jump to start kernel */
	j _start_kernel
	/* reserved */
	.word 0
	.balign 8
#if __riscv_xlen == 64
	/* Image load offset(2MB) from start of RAM */
	.dword 0x200000
#else
	/* Image load offset(4MB) from start of RAM */
	.dword 0x400000
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
	.word 0

.global _start_kernel
_start_kernel:
	/* Mask all interrupts */
	csrw CSR_IE, zero
	csrw CSR_IP, zero

#ifdef CONFIG_RISCV_M_MODE
	/* flush the instruction cache */
	fence.i

	/* Reset all registers except ra, a0, a1 */
	call reset_regs

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
	 * Disable FPU to detect illegal usage of
	 * floating point in kernel space
	 */
	li t0, SR_FS
	csrc CSR_STATUS, t0

#ifdef CONFIG_SMP
	li t0, CONFIG_NR_CPUS
	blt a0, t0, .Lgood_cores
	tail .Lsecondary_park
.Lgood_cores:
#endif

	/* Pick one hart to run the main boot sequence */
	la a3, hart_lottery
	li a2, 1
	amoadd.w a3, a2, (a3)
	bnez a3, .Lsecondary_start

	/* Clear BSS for flat non-ELF images */
	la a3, __bss_start
	la a4, __bss_stop
	ble a4, a3, clear_bss_done
clear_bss:
	REG_S zero, (a3)
	add a3, a3, RISCV_SZPTR
	blt a3, a4, clear_bss
clear_bss_done:

	/* Save hart ID and DTB physical address */
	mv s0, a0
	mv s1, a1
	la a2, boot_cpu_hartid
	REG_S a0, (a2)

	/* Initialize page tables and relocate to virtual addresses */
	la sp, init_thread_union + THREAD_SIZE
	mv a0, s1
	call setup_vm
#ifdef CONFIG_MMU
	la a0, early_pg_dir
	call relocate
#endif /* CONFIG_MMU */

	/* Restore C environment */
	la tp, init_task
	sw zero, TASK_TI_CPU(tp)
	la sp, init_thread_union + THREAD_SIZE

	/* Start the kernel */
	call parse_dtb
	tail start_kernel

#ifdef CONFIG_MMU
relocate:
	/* Relocate return address */
	li a1, PAGE_OFFSET
	la a2, _start
	sub a1, a1, a2
	add ra, ra, a1

	/* Point stvec to virtual address of intruction after satp write */
	la a2, 1f
	add a2, a2, a1
	csrw CSR_TVEC, a2

	/* Compute satp for kernel page tables, but don't load it yet */
	srl a2, a0, PAGE_SHIFT
	li a1, SATP_MODE
	or a2, a2, a1

	/*
	 * Load trampoline page directory, which will cause us to trap to
	 * stvec if VA != PA, or simply fall through if VA == PA.  We need a
	 * full fence here because setup_vm() just wrote these PTEs and we need
	 * to ensure the new translations are in use.
	 */
	la a0, trampoline_pg_dir
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
	 * the first superpage.  Fetching the fence is guarnteed to work
	 * because that first superpage is translated the same way.
	 */
	csrw CSR_SATP, a2
	sfence.vma

	ret
#endif /* CONFIG_MMU */

.Lsecondary_start:
#ifdef CONFIG_SMP
	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw CSR_TVEC, a3

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

#ifdef CONFIG_MMU
	/* Enable virtual memory and relocate to virtual address */
	la a0, swapper_pg_dir
	call relocate
#endif

	tail smp_callin
#endif

END(_start)

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
	beqz	t0, .Lreset_regs_done

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
#endif /* CONFIG_FPU */
.Lreset_regs_done:
	ret
END(reset_regs)
#endif /* CONFIG_RISCV_M_MODE */

.section ".text", "ax",@progbits
.align 2
.Lsecondary_park:
	/* We lack SMP support or have too many harts, so park this hart */
	wfi
	j .Lsecondary_park

__PAGE_ALIGNED_BSS
	/* Empty zero page */
	.balign PAGE_SIZE
