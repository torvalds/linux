/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1998 by Ralf Baechle
 *
 * Multi-arch abstraction and asm macros for easier reading:
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 *
 * Further modifications to make this work:
 * Copyright (c) 1998 Harald Koerfgen
 */
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/export.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

#define EX(a,b)							\
9:	a,##b;							\
	.section __ex_table,"a";				\
	PTR	9b,fault;					\
	.previous

#define EX2(a,b)						\
9:	a,##b;							\
	.section __ex_table,"a";				\
	PTR	9b,bad_stack;					\
	PTR	9b+4,bad_stack;					\
	.previous

	.set	mips1

/*
 * Save a thread's fp context.
 */
LEAF(_save_fp)
EXPORT_SYMBOL(_save_fp)
	fpu_save_single a0, t1			# clobbers t1
	jr	ra
	END(_save_fp)

/*
 * Restore a thread's fp context.
 */
LEAF(_restore_fp)
	fpu_restore_single a0, t1		# clobbers t1
	jr	ra
	END(_restore_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * The value to initialize fcr31 to comes in $a0.
 */

	.set push
	SET_HARDFLOAT

LEAF(_init_fpu)
	mfc0	t0, CP0_STATUS
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_STATUS

	ctc1	a0, fcr31

	li	t0, -1

	mtc1	t0, $f0
	mtc1	t0, $f1
	mtc1	t0, $f2
	mtc1	t0, $f3
	mtc1	t0, $f4
	mtc1	t0, $f5
	mtc1	t0, $f6
	mtc1	t0, $f7
	mtc1	t0, $f8
	mtc1	t0, $f9
	mtc1	t0, $f10
	mtc1	t0, $f11
	mtc1	t0, $f12
	mtc1	t0, $f13
	mtc1	t0, $f14
	mtc1	t0, $f15
	mtc1	t0, $f16
	mtc1	t0, $f17
	mtc1	t0, $f18
	mtc1	t0, $f19
	mtc1	t0, $f20
	mtc1	t0, $f21
	mtc1	t0, $f22
	mtc1	t0, $f23
	mtc1	t0, $f24
	mtc1	t0, $f25
	mtc1	t0, $f26
	mtc1	t0, $f27
	mtc1	t0, $f28
	mtc1	t0, $f29
	mtc1	t0, $f30
	mtc1	t0, $f31
	jr	ra
	END(_init_fpu)

	.set pop

	.set	noreorder

/**
 * _save_fp_context() - save FP context from the FPU
 * @a0 - pointer to fpregs field of sigcontext
 * @a1 - pointer to fpc_csr field of sigcontext
 *
 * Save FP context, including the 32 FP data registers and the FP
 * control & status register, from the FPU to signal context.
 */
LEAF(_save_fp_context)
	.set	push
	SET_HARDFLOAT
	li	v0, 0					# assume success
	cfc1	t1, fcr31
	EX2(s.d $f0, 0(a0))
	EX2(s.d $f2, 16(a0))
	EX2(s.d $f4, 32(a0))
	EX2(s.d $f6, 48(a0))
	EX2(s.d $f8, 64(a0))
	EX2(s.d $f10, 80(a0))
	EX2(s.d $f12, 96(a0))
	EX2(s.d $f14, 112(a0))
	EX2(s.d $f16, 128(a0))
	EX2(s.d $f18, 144(a0))
	EX2(s.d $f20, 160(a0))
	EX2(s.d $f22, 176(a0))
	EX2(s.d $f24, 192(a0))
	EX2(s.d $f26, 208(a0))
	EX2(s.d $f28, 224(a0))
	EX2(s.d $f30, 240(a0))
	jr	ra
	 EX(sw	t1, (a1))
	.set	pop
	END(_save_fp_context)

/**
 * _restore_fp_context() - restore FP context to the FPU
 * @a0 - pointer to fpregs field of sigcontext
 * @a1 - pointer to fpc_csr field of sigcontext
 *
 * Restore FP context, including the 32 FP data registers and the FP
 * control & status register, from signal context to the FPU.
 */
LEAF(_restore_fp_context)
	.set	push
	SET_HARDFLOAT
	li	v0, 0					# assume success
	EX(lw t0, (a1))
	EX2(l.d $f0, 0(a0))
	EX2(l.d $f2, 16(a0))
	EX2(l.d $f4, 32(a0))
	EX2(l.d $f6, 48(a0))
	EX2(l.d $f8, 64(a0))
	EX2(l.d $f10, 80(a0))
	EX2(l.d $f12, 96(a0))
	EX2(l.d $f14, 112(a0))
	EX2(l.d $f16, 128(a0))
	EX2(l.d $f18, 144(a0))
	EX2(l.d $f20, 160(a0))
	EX2(l.d $f22, 176(a0))
	EX2(l.d $f24, 192(a0))
	EX2(l.d $f26, 208(a0))
	EX2(l.d $f28, 224(a0))
	EX2(l.d $f30, 240(a0))
	jr	ra
	 ctc1	t0, fcr31
	.set	pop
	END(_restore_fp_context)
	.set	reorder

	.type	fault, @function
	.ent	fault
fault:	li	v0, -EFAULT
	jr	ra
	.end	fault
