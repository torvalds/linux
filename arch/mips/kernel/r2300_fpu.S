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
#include <asm/errno.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

#define EX(a,b)							\
9:	a,##b;							\
	.section __ex_table,"a";				\
	PTR	9b,fault;					\
	.previous

	.set	noreorder
	.set	mips1

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
	EX(swc1 $f0, 0(a0))
	EX(swc1 $f1, 8(a0))
	EX(swc1 $f2, 16(a0))
	EX(swc1 $f3, 24(a0))
	EX(swc1 $f4, 32(a0))
	EX(swc1 $f5, 40(a0))
	EX(swc1 $f6, 48(a0))
	EX(swc1 $f7, 56(a0))
	EX(swc1 $f8, 64(a0))
	EX(swc1 $f9, 72(a0))
	EX(swc1 $f10, 80(a0))
	EX(swc1 $f11, 88(a0))
	EX(swc1 $f12, 96(a0))
	EX(swc1 $f13, 104(a0))
	EX(swc1 $f14, 112(a0))
	EX(swc1 $f15, 120(a0))
	EX(swc1 $f16, 128(a0))
	EX(swc1 $f17, 136(a0))
	EX(swc1 $f18, 144(a0))
	EX(swc1 $f19, 152(a0))
	EX(swc1 $f20, 160(a0))
	EX(swc1 $f21, 168(a0))
	EX(swc1 $f22, 176(a0))
	EX(swc1 $f23, 184(a0))
	EX(swc1 $f24, 192(a0))
	EX(swc1 $f25, 200(a0))
	EX(swc1 $f26, 208(a0))
	EX(swc1 $f27, 216(a0))
	EX(swc1 $f28, 224(a0))
	EX(swc1 $f29, 232(a0))
	EX(swc1 $f30, 240(a0))
	EX(swc1 $f31, 248(a0))
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
	EX(lwc1 $f0, 0(a0))
	EX(lwc1 $f1, 8(a0))
	EX(lwc1 $f2, 16(a0))
	EX(lwc1 $f3, 24(a0))
	EX(lwc1 $f4, 32(a0))
	EX(lwc1 $f5, 40(a0))
	EX(lwc1 $f6, 48(a0))
	EX(lwc1 $f7, 56(a0))
	EX(lwc1 $f8, 64(a0))
	EX(lwc1 $f9, 72(a0))
	EX(lwc1 $f10, 80(a0))
	EX(lwc1 $f11, 88(a0))
	EX(lwc1 $f12, 96(a0))
	EX(lwc1 $f13, 104(a0))
	EX(lwc1 $f14, 112(a0))
	EX(lwc1 $f15, 120(a0))
	EX(lwc1 $f16, 128(a0))
	EX(lwc1 $f17, 136(a0))
	EX(lwc1 $f18, 144(a0))
	EX(lwc1 $f19, 152(a0))
	EX(lwc1 $f20, 160(a0))
	EX(lwc1 $f21, 168(a0))
	EX(lwc1 $f22, 176(a0))
	EX(lwc1 $f23, 184(a0))
	EX(lwc1 $f24, 192(a0))
	EX(lwc1 $f25, 200(a0))
	EX(lwc1 $f26, 208(a0))
	EX(lwc1 $f27, 216(a0))
	EX(lwc1 $f28, 224(a0))
	EX(lwc1 $f29, 232(a0))
	EX(lwc1 $f30, 240(a0))
	EX(lwc1 $f31, 248(a0))
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
