/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 99, 2001 Ralf Baechle
 * Copyright (C) 1994, 1995, 1996 Paul M. Antoine.
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2007  Maciej W. Rozycki
 */
#ifndef _ASM_STACKFRAME_H
#define _ASM_STACKFRAME_H

#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

/* Make the addition of cfi info a little easier. */
	.macro cfi_rel_offset reg offset=0 docfi=0
	.if \docfi
	.cfi_rel_offset \reg, \offset
	.endif
	.endm

	.macro cfi_st reg offset=0 docfi=0
	LONG_S	\reg, \offset(sp)
	cfi_rel_offset \reg, \offset, \docfi
	.endm

	.macro cfi_restore reg offset=0 docfi=0
	.if \docfi
	.cfi_restore \reg
	.endif
	.endm

	.macro cfi_ld reg offset=0 docfi=0
	LONG_L	\reg, \offset(sp)
	cfi_restore \reg \offset \docfi
	.endm

#if defined(CONFIG_CPU_R3000)
#define STATMASK 0x3f
#else
#define STATMASK 0x1f
#endif

		.macro	SAVE_AT docfi=0
		.set	push
		.set	noat
		cfi_st	$1, PT_R1, \docfi
		.set	pop
		.endm

		.macro	SAVE_TEMP docfi=0
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		mflhxu	v1
		LONG_S	v1, PT_LO(sp)
		mflhxu	v1
		LONG_S	v1, PT_HI(sp)
		mflhxu	v1
		LONG_S	v1, PT_ACX(sp)
#elif !defined(CONFIG_CPU_MIPSR6)
		mfhi	v1
#endif
#ifdef CONFIG_32BIT
		cfi_st	$8, PT_R8, \docfi
		cfi_st	$9, PT_R9, \docfi
#endif
		cfi_st	$10, PT_R10, \docfi
		cfi_st	$11, PT_R11, \docfi
		cfi_st	$12, PT_R12, \docfi
#if !defined(CONFIG_CPU_HAS_SMARTMIPS) && !defined(CONFIG_CPU_MIPSR6)
		LONG_S	v1, PT_HI(sp)
		mflo	v1
#endif
		cfi_st	$13, PT_R13, \docfi
		cfi_st	$14, PT_R14, \docfi
		cfi_st	$15, PT_R15, \docfi
		cfi_st	$24, PT_R24, \docfi
#if !defined(CONFIG_CPU_HAS_SMARTMIPS) && !defined(CONFIG_CPU_MIPSR6)
		LONG_S	v1, PT_LO(sp)
#endif
#ifdef CONFIG_CPU_CAVIUM_OCTEON
		/*
		 * The Octeon multiplier state is affected by general
		 * multiply instructions. It must be saved before and
		 * kernel code might corrupt it
		 */
		jal     octeon_mult_save
#endif
		.endm

		.macro	SAVE_STATIC docfi=0
		cfi_st	$16, PT_R16, \docfi
		cfi_st	$17, PT_R17, \docfi
		cfi_st	$18, PT_R18, \docfi
		cfi_st	$19, PT_R19, \docfi
		cfi_st	$20, PT_R20, \docfi
		cfi_st	$21, PT_R21, \docfi
		cfi_st	$22, PT_R22, \docfi
		cfi_st	$23, PT_R23, \docfi
		cfi_st	$30, PT_R30, \docfi
		.endm

/*
 * get_saved_sp returns the SP for the current CPU by looking in the
 * kernelsp array for it.  If tosp is set, it stores the current sp in
 * k0 and loads the new value in sp.  If not, it clobbers k0 and
 * stores the new value in k1, leaving sp unaffected.
 */
#ifdef CONFIG_SMP

		/* SMP variation */
		.macro	get_saved_sp docfi=0 tosp=0
		ASM_CPUID_MFC0	k0, ASM_SMP_CPUID_REG
#if defined(CONFIG_32BIT) || defined(KBUILD_64BIT_SYM32)
		lui	k1, %hi(kernelsp)
#else
		lui	k1, %highest(kernelsp)
		daddiu	k1, %higher(kernelsp)
		dsll	k1, 16
		daddiu	k1, %hi(kernelsp)
		dsll	k1, 16
#endif
		LONG_SRL	k0, SMP_CPUID_PTRSHIFT
		LONG_ADDU	k1, k0
		.if \tosp
		move	k0, sp
		.if \docfi
		.cfi_register sp, k0
		.endif
		LONG_L	sp, %lo(kernelsp)(k1)
		.else
		LONG_L	k1, %lo(kernelsp)(k1)
		.endif
		.endm

		.macro	set_saved_sp stackp temp temp2
		ASM_CPUID_MFC0	\temp, ASM_SMP_CPUID_REG
		LONG_SRL	\temp, SMP_CPUID_PTRSHIFT
		LONG_S	\stackp, kernelsp(\temp)
		.endm
#else /* !CONFIG_SMP */
		/* Uniprocessor variation */
		.macro	get_saved_sp docfi=0 tosp=0
#ifdef CONFIG_CPU_JUMP_WORKAROUNDS
		/*
		 * Clear BTB (branch target buffer), forbid RAS (return address
		 * stack) to workaround the Out-of-order Issue in Loongson2F
		 * via its diagnostic register.
		 */
		move	k0, ra
		jal	1f
		 nop
1:		jal	1f
		 nop
1:		jal	1f
		 nop
1:		jal	1f
		 nop
1:		move	ra, k0
		li	k0, 3
		mtc0	k0, $22
#endif /* CONFIG_CPU_JUMP_WORKAROUNDS */
#if defined(CONFIG_32BIT) || defined(KBUILD_64BIT_SYM32)
		lui	k1, %hi(kernelsp)
#else
		lui	k1, %highest(kernelsp)
		daddiu	k1, %higher(kernelsp)
		dsll	k1, k1, 16
		daddiu	k1, %hi(kernelsp)
		dsll	k1, k1, 16
#endif
		.if \tosp
		move	k0, sp
		.if \docfi
		.cfi_register sp, k0
		.endif
		LONG_L	sp, %lo(kernelsp)(k1)
		.else
		LONG_L	k1, %lo(kernelsp)(k1)
		.endif
		.endm

		.macro	set_saved_sp stackp temp temp2
		LONG_S	\stackp, kernelsp
		.endm
#endif

		.macro	SAVE_SOME docfi=0
		.set	push
		.set	noat
		.set	reorder
		mfc0	k0, CP0_STATUS
		sll	k0, 3		/* extract cu0 bit */
		.set	noreorder
		bltz	k0, 8f
		 move	k0, sp
		.if \docfi
		.cfi_register sp, k0
		.endif
#ifdef CONFIG_EVA
		/*
		 * Flush interAptiv's Return Prediction Stack (RPS) by writing
		 * EntryHi. Toggling Config7.RPS is slower and less portable.
		 *
		 * The RPS isn't automatically flushed when exceptions are
		 * taken, which can result in kernel mode speculative accesses
		 * to user addresses if the RPS mispredicts. That's harmless
		 * when user and kernel share the same address space, but with
		 * EVA the same user segments may be unmapped to kernel mode,
		 * even containing sensitive MMIO regions or invalid memory.
		 *
		 * This can happen when the kernel sets the return address to
		 * ret_from_* and jr's to the exception handler, which looks
		 * more like a tail call than a function call. If nested calls
		 * don't evict the last user address in the RPS, it will
		 * mispredict the return and fetch from a user controlled
		 * address into the icache.
		 *
		 * More recent EVA-capable cores with MAAR to restrict
		 * speculative accesses aren't affected.
		 */
		MFC0	k0, CP0_ENTRYHI
		MTC0	k0, CP0_ENTRYHI
#endif
		.set	reorder
		/* Called from user mode, new stack. */
		get_saved_sp docfi=\docfi tosp=1
8:
#ifdef CONFIG_CPU_DADDI_WORKAROUNDS
		.set	at=k1
#endif
		PTR_SUBU sp, PT_SIZE
#ifdef CONFIG_CPU_DADDI_WORKAROUNDS
		.set	noat
#endif
		.if \docfi
		.cfi_def_cfa sp,0
		.endif
		cfi_st	k0, PT_R29, \docfi
		cfi_rel_offset  sp, PT_R29, \docfi
		cfi_st	v1, PT_R3, \docfi
		/*
		 * You might think that you don't need to save $0,
		 * but the FPU emulator and gdb remote debug stub
		 * need it to operate correctly
		 */
		LONG_S	$0, PT_R0(sp)
		mfc0	v1, CP0_STATUS
		cfi_st	v0, PT_R2, \docfi
		LONG_S	v1, PT_STATUS(sp)
		cfi_st	$4, PT_R4, \docfi
		mfc0	v1, CP0_CAUSE
		cfi_st	$5, PT_R5, \docfi
		LONG_S	v1, PT_CAUSE(sp)
		cfi_st	$6, PT_R6, \docfi
		cfi_st	ra, PT_R31, \docfi
		MFC0	ra, CP0_EPC
		cfi_st	$7, PT_R7, \docfi
#ifdef CONFIG_64BIT
		cfi_st	$8, PT_R8, \docfi
		cfi_st	$9, PT_R9, \docfi
#endif
		LONG_S	ra, PT_EPC(sp)
		.if \docfi
		.cfi_rel_offset ra, PT_EPC
		.endif
		cfi_st	$25, PT_R25, \docfi
		cfi_st	$28, PT_R28, \docfi

		/* Set thread_info if we're coming from user mode */
		mfc0	k0, CP0_STATUS
		sll	k0, 3		/* extract cu0 bit */
		bltz	k0, 9f

		ori	$28, sp, _THREAD_MASK
		xori	$28, _THREAD_MASK
#ifdef CONFIG_CPU_CAVIUM_OCTEON
		.set    mips64
		pref    0, 0($28)       /* Prefetch the current pointer */
#endif
9:
		.set	pop
		.endm

		.macro	SAVE_ALL docfi=0
		SAVE_SOME \docfi
		SAVE_AT \docfi
		SAVE_TEMP \docfi
		SAVE_STATIC \docfi
		.endm

		.macro	RESTORE_AT docfi=0
		.set	push
		.set	noat
		cfi_ld	$1, PT_R1, \docfi
		.set	pop
		.endm

		.macro	RESTORE_TEMP docfi=0
#ifdef CONFIG_CPU_CAVIUM_OCTEON
		/* Restore the Octeon multiplier state */
		jal	octeon_mult_restore
#endif
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		LONG_L	$14, PT_ACX(sp)
		LONG_L	$24, PT_LO(sp)
		LONG_L	$15, PT_HI(sp)
#elif !defined(CONFIG_CPU_MIPSR6)
		LONG_L	$24, PT_LO(sp)
		LONG_L	$15, PT_HI(sp)
#endif
#ifdef CONFIG_32BIT
		cfi_ld	$8, PT_R8, \docfi
		cfi_ld	$9, PT_R9, \docfi
#endif
		cfi_ld	$10, PT_R10, \docfi
		cfi_ld	$11, PT_R11, \docfi
		cfi_ld	$12, PT_R12, \docfi
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		mtlhx	$14
		mtlhx	$15
		mtlhx	$24
#elif !defined(CONFIG_CPU_MIPSR6)
		mtlo	$24
		mthi	$15
#endif
		cfi_ld	$13, PT_R13, \docfi
		cfi_ld	$14, PT_R14, \docfi
		cfi_ld	$15, PT_R15, \docfi
		cfi_ld	$24, PT_R24, \docfi
		.endm

		.macro	RESTORE_STATIC docfi=0
		cfi_ld	$16, PT_R16, \docfi
		cfi_ld	$17, PT_R17, \docfi
		cfi_ld	$18, PT_R18, \docfi
		cfi_ld	$19, PT_R19, \docfi
		cfi_ld	$20, PT_R20, \docfi
		cfi_ld	$21, PT_R21, \docfi
		cfi_ld	$22, PT_R22, \docfi
		cfi_ld	$23, PT_R23, \docfi
		cfi_ld	$30, PT_R30, \docfi
		.endm

		.macro	RESTORE_SP docfi=0
		cfi_ld	sp, PT_R29, \docfi
		.endm

#if defined(CONFIG_CPU_R3000)

		.macro	RESTORE_SOME docfi=0
		.set	push
		.set	reorder
		.set	noat
		mfc0	a0, CP0_STATUS
		li	v1, ST0_CU1 | ST0_IM
		ori	a0, STATMASK
		xori	a0, STATMASK
		mtc0	a0, CP0_STATUS
		and	a0, v1
		LONG_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
		cfi_ld	$31, PT_R31, \docfi
		cfi_ld	$28, PT_R28, \docfi
		cfi_ld	$25, PT_R25, \docfi
		cfi_ld	$7,  PT_R7, \docfi
		cfi_ld	$6,  PT_R6, \docfi
		cfi_ld	$5,  PT_R5, \docfi
		cfi_ld	$4,  PT_R4, \docfi
		cfi_ld	$3,  PT_R3, \docfi
		cfi_ld	$2,  PT_R2, \docfi
		.set	pop
		.endm

		.macro	RESTORE_SP_AND_RET docfi=0
		.set	push
		.set	noreorder
		LONG_L	k0, PT_EPC(sp)
		RESTORE_SP \docfi
		jr	k0
		 rfe
		.set	pop
		.endm

#else
		.macro	RESTORE_SOME docfi=0
		.set	push
		.set	reorder
		.set	noat
		mfc0	a0, CP0_STATUS
		ori	a0, STATMASK
		xori	a0, STATMASK
		mtc0	a0, CP0_STATUS
		li	v1, ST0_CU1 | ST0_FR | ST0_IM
		and	a0, v1
		LONG_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
		LONG_L	v1, PT_EPC(sp)
		MTC0	v1, CP0_EPC
		cfi_ld	$31, PT_R31, \docfi
		cfi_ld	$28, PT_R28, \docfi
		cfi_ld	$25, PT_R25, \docfi
#ifdef CONFIG_64BIT
		cfi_ld	$8, PT_R8, \docfi
		cfi_ld	$9, PT_R9, \docfi
#endif
		cfi_ld	$7,  PT_R7, \docfi
		cfi_ld	$6,  PT_R6, \docfi
		cfi_ld	$5,  PT_R5, \docfi
		cfi_ld	$4,  PT_R4, \docfi
		cfi_ld	$3,  PT_R3, \docfi
		cfi_ld	$2,  PT_R2, \docfi
		.set	pop
		.endm

		.macro	RESTORE_SP_AND_RET docfi=0
		RESTORE_SP \docfi
#if defined(CONFIG_CPU_MIPSR5) || defined(CONFIG_CPU_MIPSR6)
		eretnc
#else
		.set	push
		.set	arch=r4000
		eret
		.set	pop
#endif
		.endm

#endif

		.macro	RESTORE_ALL docfi=0
		RESTORE_TEMP \docfi
		RESTORE_STATIC \docfi
		RESTORE_AT \docfi
		RESTORE_SOME \docfi
		RESTORE_SP \docfi
		.endm

/*
 * Move to kernel mode and disable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	CLI
		mfc0	t0, CP0_STATUS
		li	t1, ST0_KERNEL_CUMASK | STATMASK
		or	t0, t1
		xori	t0, STATMASK
		mtc0	t0, CP0_STATUS
		irq_disable_hazard
		.endm

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	STI
		mfc0	t0, CP0_STATUS
		li	t1, ST0_KERNEL_CUMASK | STATMASK
		or	t0, t1
		xori	t0, STATMASK & ~1
		mtc0	t0, CP0_STATUS
		irq_enable_hazard
		.endm

/*
 * Just move to kernel mode and leave interrupts as they are.  Note
 * for the R3000 this means copying the previous enable from IEp.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	KMODE
		mfc0	t0, CP0_STATUS
		li	t1, ST0_KERNEL_CUMASK | (STATMASK & ~1)
#if defined(CONFIG_CPU_R3000)
		andi	t2, t0, ST0_IEP
		srl	t2, 2
		or	t0, t2
#endif
		or	t0, t1
		xori	t0, STATMASK & ~1
		mtc0	t0, CP0_STATUS
		irq_disable_hazard
		.endm

#endif /* _ASM_STACKFRAME_H */
