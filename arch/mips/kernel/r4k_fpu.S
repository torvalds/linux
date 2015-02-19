/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 98, 99, 2000, 01 Ralf Baechle
 *
 * Multi-arch abstraction and asm macros for easier reading:
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 * Copyright (C) 1999, 2001 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

/* preprocessor replaces the fp in ".set fp=64" with $30 otherwise */
#undef fp

	.macro	EX insn, reg, src
	.set	push
	SET_HARDFLOAT
	.set	nomacro
.ex\@:	\insn	\reg, \src
	.set	pop
	.section __ex_table,"a"
	PTR	.ex\@, fault
	.previous
	.endm

	.set	noreorder
	.set	arch=r4000

LEAF(_save_fp_context)
	.set	push
	SET_HARDFLOAT
	cfc1	t1, fcr31
	.set	pop

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	.set	push
	SET_HARDFLOAT
#ifdef CONFIG_CPU_MIPS32_R2
	.set	mips32r2
	.set	fp=64
	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f			# skip storing odd if FR=0
	 nop
#endif
	/* Store the 16 odd double precision registers */
	EX	sdc1 $f1, SC_FPREGS+8(a0)
	EX	sdc1 $f3, SC_FPREGS+24(a0)
	EX	sdc1 $f5, SC_FPREGS+40(a0)
	EX	sdc1 $f7, SC_FPREGS+56(a0)
	EX	sdc1 $f9, SC_FPREGS+72(a0)
	EX	sdc1 $f11, SC_FPREGS+88(a0)
	EX	sdc1 $f13, SC_FPREGS+104(a0)
	EX	sdc1 $f15, SC_FPREGS+120(a0)
	EX	sdc1 $f17, SC_FPREGS+136(a0)
	EX	sdc1 $f19, SC_FPREGS+152(a0)
	EX	sdc1 $f21, SC_FPREGS+168(a0)
	EX	sdc1 $f23, SC_FPREGS+184(a0)
	EX	sdc1 $f25, SC_FPREGS+200(a0)
	EX	sdc1 $f27, SC_FPREGS+216(a0)
	EX	sdc1 $f29, SC_FPREGS+232(a0)
	EX	sdc1 $f31, SC_FPREGS+248(a0)
1:	.set	pop
#endif

	.set push
	SET_HARDFLOAT
	/* Store the 16 even double precision registers */
	EX	sdc1 $f0, SC_FPREGS+0(a0)
	EX	sdc1 $f2, SC_FPREGS+16(a0)
	EX	sdc1 $f4, SC_FPREGS+32(a0)
	EX	sdc1 $f6, SC_FPREGS+48(a0)
	EX	sdc1 $f8, SC_FPREGS+64(a0)
	EX	sdc1 $f10, SC_FPREGS+80(a0)
	EX	sdc1 $f12, SC_FPREGS+96(a0)
	EX	sdc1 $f14, SC_FPREGS+112(a0)
	EX	sdc1 $f16, SC_FPREGS+128(a0)
	EX	sdc1 $f18, SC_FPREGS+144(a0)
	EX	sdc1 $f20, SC_FPREGS+160(a0)
	EX	sdc1 $f22, SC_FPREGS+176(a0)
	EX	sdc1 $f24, SC_FPREGS+192(a0)
	EX	sdc1 $f26, SC_FPREGS+208(a0)
	EX	sdc1 $f28, SC_FPREGS+224(a0)
	EX	sdc1 $f30, SC_FPREGS+240(a0)
	EX	sw t1, SC_FPC_CSR(a0)
	jr	ra
	 li	v0, 0					# success
	.set pop
	END(_save_fp_context)

#ifdef CONFIG_MIPS32_COMPAT
	/* Save 32-bit process floating point context */
LEAF(_save_fp_context32)
	.set push
	SET_HARDFLOAT
	cfc1	t1, fcr31

	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f			# skip storing odd if FR=0
	 nop

	/* Store the 16 odd double precision registers */
	EX      sdc1 $f1, SC32_FPREGS+8(a0)
	EX      sdc1 $f3, SC32_FPREGS+24(a0)
	EX      sdc1 $f5, SC32_FPREGS+40(a0)
	EX      sdc1 $f7, SC32_FPREGS+56(a0)
	EX      sdc1 $f9, SC32_FPREGS+72(a0)
	EX      sdc1 $f11, SC32_FPREGS+88(a0)
	EX      sdc1 $f13, SC32_FPREGS+104(a0)
	EX      sdc1 $f15, SC32_FPREGS+120(a0)
	EX      sdc1 $f17, SC32_FPREGS+136(a0)
	EX      sdc1 $f19, SC32_FPREGS+152(a0)
	EX      sdc1 $f21, SC32_FPREGS+168(a0)
	EX      sdc1 $f23, SC32_FPREGS+184(a0)
	EX      sdc1 $f25, SC32_FPREGS+200(a0)
	EX      sdc1 $f27, SC32_FPREGS+216(a0)
	EX      sdc1 $f29, SC32_FPREGS+232(a0)
	EX      sdc1 $f31, SC32_FPREGS+248(a0)

	/* Store the 16 even double precision registers */
1:	EX	sdc1 $f0, SC32_FPREGS+0(a0)
	EX	sdc1 $f2, SC32_FPREGS+16(a0)
	EX	sdc1 $f4, SC32_FPREGS+32(a0)
	EX	sdc1 $f6, SC32_FPREGS+48(a0)
	EX	sdc1 $f8, SC32_FPREGS+64(a0)
	EX	sdc1 $f10, SC32_FPREGS+80(a0)
	EX	sdc1 $f12, SC32_FPREGS+96(a0)
	EX	sdc1 $f14, SC32_FPREGS+112(a0)
	EX	sdc1 $f16, SC32_FPREGS+128(a0)
	EX	sdc1 $f18, SC32_FPREGS+144(a0)
	EX	sdc1 $f20, SC32_FPREGS+160(a0)
	EX	sdc1 $f22, SC32_FPREGS+176(a0)
	EX	sdc1 $f24, SC32_FPREGS+192(a0)
	EX	sdc1 $f26, SC32_FPREGS+208(a0)
	EX	sdc1 $f28, SC32_FPREGS+224(a0)
	EX	sdc1 $f30, SC32_FPREGS+240(a0)
	EX	sw t1, SC32_FPC_CSR(a0)
	cfc1	t0, $0				# implementation/version
	EX	sw t0, SC32_FPC_EIR(a0)
	.set pop

	jr	ra
	 li	v0, 0					# success
	END(_save_fp_context32)
#endif

/*
 * Restore FPU state:
 *  - fp gp registers
 *  - cp1 status/control register
 */
LEAF(_restore_fp_context)
	EX	lw t1, SC_FPC_CSR(a0)

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	.set	push
	SET_HARDFLOAT
#ifdef CONFIG_CPU_MIPS32_R2
	.set	mips32r2
	.set	fp=64
	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f			# skip loading odd if FR=0
	 nop
#endif
	EX	ldc1 $f1, SC_FPREGS+8(a0)
	EX	ldc1 $f3, SC_FPREGS+24(a0)
	EX	ldc1 $f5, SC_FPREGS+40(a0)
	EX	ldc1 $f7, SC_FPREGS+56(a0)
	EX	ldc1 $f9, SC_FPREGS+72(a0)
	EX	ldc1 $f11, SC_FPREGS+88(a0)
	EX	ldc1 $f13, SC_FPREGS+104(a0)
	EX	ldc1 $f15, SC_FPREGS+120(a0)
	EX	ldc1 $f17, SC_FPREGS+136(a0)
	EX	ldc1 $f19, SC_FPREGS+152(a0)
	EX	ldc1 $f21, SC_FPREGS+168(a0)
	EX	ldc1 $f23, SC_FPREGS+184(a0)
	EX	ldc1 $f25, SC_FPREGS+200(a0)
	EX	ldc1 $f27, SC_FPREGS+216(a0)
	EX	ldc1 $f29, SC_FPREGS+232(a0)
	EX	ldc1 $f31, SC_FPREGS+248(a0)
1:	.set pop
#endif
	.set push
	SET_HARDFLOAT
	EX	ldc1 $f0, SC_FPREGS+0(a0)
	EX	ldc1 $f2, SC_FPREGS+16(a0)
	EX	ldc1 $f4, SC_FPREGS+32(a0)
	EX	ldc1 $f6, SC_FPREGS+48(a0)
	EX	ldc1 $f8, SC_FPREGS+64(a0)
	EX	ldc1 $f10, SC_FPREGS+80(a0)
	EX	ldc1 $f12, SC_FPREGS+96(a0)
	EX	ldc1 $f14, SC_FPREGS+112(a0)
	EX	ldc1 $f16, SC_FPREGS+128(a0)
	EX	ldc1 $f18, SC_FPREGS+144(a0)
	EX	ldc1 $f20, SC_FPREGS+160(a0)
	EX	ldc1 $f22, SC_FPREGS+176(a0)
	EX	ldc1 $f24, SC_FPREGS+192(a0)
	EX	ldc1 $f26, SC_FPREGS+208(a0)
	EX	ldc1 $f28, SC_FPREGS+224(a0)
	EX	ldc1 $f30, SC_FPREGS+240(a0)
	ctc1	t1, fcr31
	.set pop
	jr	ra
	 li	v0, 0					# success
	END(_restore_fp_context)

#ifdef CONFIG_MIPS32_COMPAT
LEAF(_restore_fp_context32)
	/* Restore an o32 sigcontext.  */
	.set push
	SET_HARDFLOAT
	EX	lw t1, SC32_FPC_CSR(a0)

	mfc0	t0, CP0_STATUS
	sll	t0, t0, 5
	bgez	t0, 1f			# skip loading odd if FR=0
	 nop

	EX      ldc1 $f1, SC32_FPREGS+8(a0)
	EX      ldc1 $f3, SC32_FPREGS+24(a0)
	EX      ldc1 $f5, SC32_FPREGS+40(a0)
	EX      ldc1 $f7, SC32_FPREGS+56(a0)
	EX      ldc1 $f9, SC32_FPREGS+72(a0)
	EX      ldc1 $f11, SC32_FPREGS+88(a0)
	EX      ldc1 $f13, SC32_FPREGS+104(a0)
	EX      ldc1 $f15, SC32_FPREGS+120(a0)
	EX      ldc1 $f17, SC32_FPREGS+136(a0)
	EX      ldc1 $f19, SC32_FPREGS+152(a0)
	EX      ldc1 $f21, SC32_FPREGS+168(a0)
	EX      ldc1 $f23, SC32_FPREGS+184(a0)
	EX      ldc1 $f25, SC32_FPREGS+200(a0)
	EX      ldc1 $f27, SC32_FPREGS+216(a0)
	EX      ldc1 $f29, SC32_FPREGS+232(a0)
	EX      ldc1 $f31, SC32_FPREGS+248(a0)

1:	EX	ldc1 $f0, SC32_FPREGS+0(a0)
	EX	ldc1 $f2, SC32_FPREGS+16(a0)
	EX	ldc1 $f4, SC32_FPREGS+32(a0)
	EX	ldc1 $f6, SC32_FPREGS+48(a0)
	EX	ldc1 $f8, SC32_FPREGS+64(a0)
	EX	ldc1 $f10, SC32_FPREGS+80(a0)
	EX	ldc1 $f12, SC32_FPREGS+96(a0)
	EX	ldc1 $f14, SC32_FPREGS+112(a0)
	EX	ldc1 $f16, SC32_FPREGS+128(a0)
	EX	ldc1 $f18, SC32_FPREGS+144(a0)
	EX	ldc1 $f20, SC32_FPREGS+160(a0)
	EX	ldc1 $f22, SC32_FPREGS+176(a0)
	EX	ldc1 $f24, SC32_FPREGS+192(a0)
	EX	ldc1 $f26, SC32_FPREGS+208(a0)
	EX	ldc1 $f28, SC32_FPREGS+224(a0)
	EX	ldc1 $f30, SC32_FPREGS+240(a0)
	ctc1	t1, fcr31
	jr	ra
	 li	v0, 0					# success
	.set pop
	END(_restore_fp_context32)
#endif

	.set	reorder

	.type	fault@function
	.ent	fault
fault:	li	v0, -EFAULT				# failure
	jr	ra
	.end	fault
