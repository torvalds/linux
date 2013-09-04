/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998, 1999, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 MIPS Technologies, Inc.
 *    written by Carsten Langgaard, carstenl@mips.com
 */
#include <asm/asm.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable-bits.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

#include <asm/asmmacro.h>

/*
 * Offset to the current process status flags, the first 32 bytes of the
 * stack are not used.
 */
#define ST_OFF (_THREAD_SIZE - 32 - PT_SIZE + PT_STATUS)

/*
 * FPU context is saved iff the process has used it's FPU in the current
 * time slice as indicated by _TIF_USEDFPU.  In any case, the CU1 bit for user
 * space STATUS register should be 0, so that a process *always* starts its
 * userland with FPU disabled after each context switch.
 *
 * FPU will be enabled as soon as the process accesses FPU again, through
 * do_cpu() trap.
 */

/*
 * task_struct *resume(task_struct *prev, task_struct *next,
 *		       struct thread_info *next_ti, int usedfpu)
 */
	.align	5
	LEAF(resume)
	mfc0	t1, CP0_STATUS
	LONG_S	t1, THREAD_STATUS(a0)
	cpu_save_nonscratch a0
	LONG_S	ra, THREAD_REG31(a0)

	/*
	 * check if we need to save FPU registers
	 */

	beqz	a3, 1f

	PTR_L	t3, TASK_THREAD_INFO(a0)
	/*
	 * clear saved user stack CU1 bit
	 */
	LONG_L	t0, ST_OFF(t3)
	li	t1, ~ST0_CU1
	and	t0, t0, t1
	LONG_S	t0, ST_OFF(t3)

	fpu_save_double a0 t0 t1		# c0_status passed in t0
						# clobbers t1
1:

#if defined(CONFIG_CC_STACKPROTECTOR) && !defined(CONFIG_SMP)
	PTR_L	t8, __stack_chk_guard
	LONG_L	t9, TASK_STACK_CANARY(a1)
	LONG_S	t9, 0(t8)
#endif

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a2
	cpu_restore_nonscratch a1

	PTR_ADDU	t0, $28, _THREAD_SIZE - 32
	set_saved_sp	t0, t1, t2
#ifdef CONFIG_MIPS_MT_SMTC
	/* Read-modify-writes of Status must be atomic on a VPE */
	mfc0	t2, CP0_TCSTATUS
	ori	t1, t2, TCSTATUS_IXMT
	mtc0	t1, CP0_TCSTATUS
	andi	t2, t2, TCSTATUS_IXMT
	_ehb
	DMT	8				# dmt	t0
	move	t1,ra
	jal	mips_ihb
	move	ra,t1
#endif /* CONFIG_MIPS_MT_SMTC */
	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff01
	and	t1, a3
	LONG_L	a2, THREAD_STATUS(a1)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
#ifdef CONFIG_MIPS_MT_SMTC
	_ehb
	andi	t0, t0, VPECONTROL_TE
	beqz	t0, 1f
	emt
1:
	mfc0	t1, CP0_TCSTATUS
	xori	t1, t1, TCSTATUS_IXMT
	or	t1, t1, t2
	mtc0	t1, CP0_TCSTATUS
	_ehb
#endif /* CONFIG_MIPS_MT_SMTC */
	move	v0, a0
	jr	ra
	END(resume)

/*
 * Save a thread's fp context.
 */
LEAF(_save_fp)
#ifdef CONFIG_64BIT
	mfc0	t0, CP0_STATUS
#endif
	fpu_save_double a0 t0 t1		# clobbers t1
	jr	ra
	END(_save_fp)

/*
 * Restore a thread's fp context.
 */
LEAF(_restore_fp)
#ifdef CONFIG_64BIT
	mfc0	t0, CP0_STATUS
#endif
	fpu_restore_double a0 t0 t1		# clobbers t1
	jr	ra
	END(_restore_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * We initialize fcr31 to rounding to nearest, no exceptions.
 */

#define FPU_DEFAULT  0x00000000

LEAF(_init_fpu)
#ifdef CONFIG_MIPS_MT_SMTC
	/* Rather than manipulate per-VPE Status, set per-TC bit in TCStatus */
	mfc0	t0, CP0_TCSTATUS
	/* Bit position is the same for Status, TCStatus */
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_TCSTATUS
#else /* Normal MIPS CU1 enable */
	mfc0	t0, CP0_STATUS
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_STATUS
#endif /* CONFIG_MIPS_MT_SMTC */
	enable_fpu_hazard

	li	t1, FPU_DEFAULT
	ctc1	t1, fcr31

	li	t1, -1				# SNaN

#ifdef CONFIG_64BIT
	sll	t0, t0, 5
	bgez	t0, 1f				# 16 / 32 register mode?

	dmtc1	t1, $f1
	dmtc1	t1, $f3
	dmtc1	t1, $f5
	dmtc1	t1, $f7
	dmtc1	t1, $f9
	dmtc1	t1, $f11
	dmtc1	t1, $f13
	dmtc1	t1, $f15
	dmtc1	t1, $f17
	dmtc1	t1, $f19
	dmtc1	t1, $f21
	dmtc1	t1, $f23
	dmtc1	t1, $f25
	dmtc1	t1, $f27
	dmtc1	t1, $f29
	dmtc1	t1, $f31
1:
#endif

#ifdef CONFIG_CPU_MIPS32
	mtc1	t1, $f0
	mtc1	t1, $f1
	mtc1	t1, $f2
	mtc1	t1, $f3
	mtc1	t1, $f4
	mtc1	t1, $f5
	mtc1	t1, $f6
	mtc1	t1, $f7
	mtc1	t1, $f8
	mtc1	t1, $f9
	mtc1	t1, $f10
	mtc1	t1, $f11
	mtc1	t1, $f12
	mtc1	t1, $f13
	mtc1	t1, $f14
	mtc1	t1, $f15
	mtc1	t1, $f16
	mtc1	t1, $f17
	mtc1	t1, $f18
	mtc1	t1, $f19
	mtc1	t1, $f20
	mtc1	t1, $f21
	mtc1	t1, $f22
	mtc1	t1, $f23
	mtc1	t1, $f24
	mtc1	t1, $f25
	mtc1	t1, $f26
	mtc1	t1, $f27
	mtc1	t1, $f28
	mtc1	t1, $f29
	mtc1	t1, $f30
	mtc1	t1, $f31
#else
	.set	mips3
	dmtc1	t1, $f0
	dmtc1	t1, $f2
	dmtc1	t1, $f4
	dmtc1	t1, $f6
	dmtc1	t1, $f8
	dmtc1	t1, $f10
	dmtc1	t1, $f12
	dmtc1	t1, $f14
	dmtc1	t1, $f16
	dmtc1	t1, $f18
	dmtc1	t1, $f20
	dmtc1	t1, $f22
	dmtc1	t1, $f24
	dmtc1	t1, $f26
	dmtc1	t1, $f28
	dmtc1	t1, $f30
#endif
	jr	ra
	END(_init_fpu)
