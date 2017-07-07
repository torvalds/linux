/*
 * r6000_fpu.S: Save/restore floating point context for signal handlers.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 *
 * Multi-arch abstraction and asm macros for easier reading:
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */
#include <asm/asm.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

	.set	noreorder
	.set	mips2
	.set	push
	SET_HARDFLOAT

/**
 * _save_fp_context() - save FP context from the FPU
 * @a0 - pointer to fpregs field of sigcontext
 * @a1 - pointer to fpc_csr field of sigcontext
 *
 * Save FP context, including the 32 FP data registers and the FP
 * control & status register, from the FPU to signal context.
 */
	LEAF(_save_fp_context)
	mfc0	t0,CP0_STATUS
	sll	t0,t0,2
	bgez	t0,1f
	 nop

	cfc1	t1,fcr31
	/* Store the 16 double precision registers */
	sdc1	$f0,0(a0)
	sdc1	$f2,16(a0)
	sdc1	$f4,32(a0)
	sdc1	$f6,48(a0)
	sdc1	$f8,64(a0)
	sdc1	$f10,80(a0)
	sdc1	$f12,96(a0)
	sdc1	$f14,112(a0)
	sdc1	$f16,128(a0)
	sdc1	$f18,144(a0)
	sdc1	$f20,160(a0)
	sdc1	$f22,176(a0)
	sdc1	$f24,192(a0)
	sdc1	$f26,208(a0)
	sdc1	$f28,224(a0)
	sdc1	$f30,240(a0)
	jr	ra
	 sw	t0,(a1)
1:	jr	ra
	 nop
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
	mfc0	t0,CP0_STATUS
	sll	t0,t0,2

	bgez	t0,1f
	 lw	t0,(a1)
	/* Restore the 16 double precision registers */
	ldc1	$f0,0(a0)
	ldc1	$f2,16(a0)
	ldc1	$f4,32(a0)
	ldc1	$f6,48(a0)
	ldc1	$f8,64(a0)
	ldc1	$f10,80(a0)
	ldc1	$f12,96(a0)
	ldc1	$f14,112(a0)
	ldc1	$f16,128(a0)
	ldc1	$f18,144(a0)
	ldc1	$f20,160(a0)
	ldc1	$f22,176(a0)
	ldc1	$f24,192(a0)
	ldc1	$f26,208(a0)
	ldc1	$f28,224(a0)
	ldc1	$f30,240(a0)
	jr	ra
	 ctc1	t0,fcr31
1:	jr	ra
	 nop
	END(_restore_fp_context)

	.set pop	/* SET_HARDFLOAT */
