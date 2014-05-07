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
#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

	.macro	EX insn, reg, src
	.set	push
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
	cfc1	t1, fcr31

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	.set	push
#ifdef CONFIG_CPU_MIPS32_R2
	.set	mips64r2
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
	END(_save_fp_context)

#ifdef CONFIG_MIPS32_COMPAT
	/* Save 32-bit process floating point context */
LEAF(_save_fp_context32)
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
#ifdef CONFIG_CPU_MIPS32_R2
	.set	mips64r2
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
	jr	ra
	 li	v0, 0					# success
	END(_restore_fp_context)

#ifdef CONFIG_MIPS32_COMPAT
LEAF(_restore_fp_context32)
	/* Restore an o32 sigcontext.  */
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
	END(_restore_fp_context32)
#endif

#ifdef CONFIG_CPU_HAS_MSA

	.macro	save_sc_msareg	wr, off, sc, tmp
#ifdef CONFIG_64BIT
	copy_u_d \tmp, \wr, 1
	EX sd	\tmp, (\off+(\wr*8))(\sc)
#elif defined(CONFIG_CPU_LITTLE_ENDIAN)
	copy_u_w \tmp, \wr, 2
	EX sw	\tmp, (\off+(\wr*8)+0)(\sc)
	copy_u_w \tmp, \wr, 3
	EX sw	\tmp, (\off+(\wr*8)+4)(\sc)
#else /* CONFIG_CPU_BIG_ENDIAN */
	copy_u_w \tmp, \wr, 2
	EX sw	\tmp, (\off+(\wr*8)+4)(\sc)
	copy_u_w \tmp, \wr, 3
	EX sw	\tmp, (\off+(\wr*8)+0)(\sc)
#endif
	.endm

/*
 * int _save_msa_context(struct sigcontext *sc)
 *
 * Save the upper 64 bits of each vector register along with the MSA_CSR
 * register into sc. Returns zero on success, else non-zero.
 */
LEAF(_save_msa_context)
	save_sc_msareg	0, SC_MSAREGS, a0, t0
	save_sc_msareg	1, SC_MSAREGS, a0, t0
	save_sc_msareg	2, SC_MSAREGS, a0, t0
	save_sc_msareg	3, SC_MSAREGS, a0, t0
	save_sc_msareg	4, SC_MSAREGS, a0, t0
	save_sc_msareg	5, SC_MSAREGS, a0, t0
	save_sc_msareg	6, SC_MSAREGS, a0, t0
	save_sc_msareg	7, SC_MSAREGS, a0, t0
	save_sc_msareg	8, SC_MSAREGS, a0, t0
	save_sc_msareg	9, SC_MSAREGS, a0, t0
	save_sc_msareg	10, SC_MSAREGS, a0, t0
	save_sc_msareg	11, SC_MSAREGS, a0, t0
	save_sc_msareg	12, SC_MSAREGS, a0, t0
	save_sc_msareg	13, SC_MSAREGS, a0, t0
	save_sc_msareg	14, SC_MSAREGS, a0, t0
	save_sc_msareg	15, SC_MSAREGS, a0, t0
	save_sc_msareg	16, SC_MSAREGS, a0, t0
	save_sc_msareg	17, SC_MSAREGS, a0, t0
	save_sc_msareg	18, SC_MSAREGS, a0, t0
	save_sc_msareg	19, SC_MSAREGS, a0, t0
	save_sc_msareg	20, SC_MSAREGS, a0, t0
	save_sc_msareg	21, SC_MSAREGS, a0, t0
	save_sc_msareg	22, SC_MSAREGS, a0, t0
	save_sc_msareg	23, SC_MSAREGS, a0, t0
	save_sc_msareg	24, SC_MSAREGS, a0, t0
	save_sc_msareg	25, SC_MSAREGS, a0, t0
	save_sc_msareg	26, SC_MSAREGS, a0, t0
	save_sc_msareg	27, SC_MSAREGS, a0, t0
	save_sc_msareg	28, SC_MSAREGS, a0, t0
	save_sc_msareg	29, SC_MSAREGS, a0, t0
	save_sc_msareg	30, SC_MSAREGS, a0, t0
	save_sc_msareg	31, SC_MSAREGS, a0, t0
	jr	ra
	 li	v0, 0
	END(_save_msa_context)

#ifdef CONFIG_MIPS32_COMPAT

/*
 * int _save_msa_context32(struct sigcontext32 *sc)
 *
 * Save the upper 64 bits of each vector register along with the MSA_CSR
 * register into sc. Returns zero on success, else non-zero.
 */
LEAF(_save_msa_context32)
	save_sc_msareg	0, SC32_MSAREGS, a0, t0
	save_sc_msareg	1, SC32_MSAREGS, a0, t0
	save_sc_msareg	2, SC32_MSAREGS, a0, t0
	save_sc_msareg	3, SC32_MSAREGS, a0, t0
	save_sc_msareg	4, SC32_MSAREGS, a0, t0
	save_sc_msareg	5, SC32_MSAREGS, a0, t0
	save_sc_msareg	6, SC32_MSAREGS, a0, t0
	save_sc_msareg	7, SC32_MSAREGS, a0, t0
	save_sc_msareg	8, SC32_MSAREGS, a0, t0
	save_sc_msareg	9, SC32_MSAREGS, a0, t0
	save_sc_msareg	10, SC32_MSAREGS, a0, t0
	save_sc_msareg	11, SC32_MSAREGS, a0, t0
	save_sc_msareg	12, SC32_MSAREGS, a0, t0
	save_sc_msareg	13, SC32_MSAREGS, a0, t0
	save_sc_msareg	14, SC32_MSAREGS, a0, t0
	save_sc_msareg	15, SC32_MSAREGS, a0, t0
	save_sc_msareg	16, SC32_MSAREGS, a0, t0
	save_sc_msareg	17, SC32_MSAREGS, a0, t0
	save_sc_msareg	18, SC32_MSAREGS, a0, t0
	save_sc_msareg	19, SC32_MSAREGS, a0, t0
	save_sc_msareg	20, SC32_MSAREGS, a0, t0
	save_sc_msareg	21, SC32_MSAREGS, a0, t0
	save_sc_msareg	22, SC32_MSAREGS, a0, t0
	save_sc_msareg	23, SC32_MSAREGS, a0, t0
	save_sc_msareg	24, SC32_MSAREGS, a0, t0
	save_sc_msareg	25, SC32_MSAREGS, a0, t0
	save_sc_msareg	26, SC32_MSAREGS, a0, t0
	save_sc_msareg	27, SC32_MSAREGS, a0, t0
	save_sc_msareg	28, SC32_MSAREGS, a0, t0
	save_sc_msareg	29, SC32_MSAREGS, a0, t0
	save_sc_msareg	30, SC32_MSAREGS, a0, t0
	save_sc_msareg	31, SC32_MSAREGS, a0, t0
	jr	ra
	 li	v0, 0
	END(_save_msa_context32)

#endif /* CONFIG_MIPS32_COMPAT */

	.macro restore_sc_msareg	wr, off, sc, tmp
#ifdef CONFIG_64BIT
	EX ld	\tmp, (\off+(\wr*8))(\sc)
	insert_d \wr, 1, \tmp
#elif defined(CONFIG_CPU_LITTLE_ENDIAN)
	EX lw	\tmp, (\off+(\wr*8)+0)(\sc)
	insert_w \wr, 2, \tmp
	EX lw	\tmp, (\off+(\wr*8)+4)(\sc)
	insert_w \wr, 3, \tmp
#else /* CONFIG_CPU_BIG_ENDIAN */
	EX lw	\tmp, (\off+(\wr*8)+4)(\sc)
	insert_w \wr, 2, \tmp
	EX lw	\tmp, (\off+(\wr*8)+0)(\sc)
	insert_w \wr, 3, \tmp
#endif
	.endm

/*
 * int _restore_msa_context(struct sigcontext *sc)
 */
LEAF(_restore_msa_context)
	restore_sc_msareg	0, SC_MSAREGS, a0, t0
	restore_sc_msareg	1, SC_MSAREGS, a0, t0
	restore_sc_msareg	2, SC_MSAREGS, a0, t0
	restore_sc_msareg	3, SC_MSAREGS, a0, t0
	restore_sc_msareg	4, SC_MSAREGS, a0, t0
	restore_sc_msareg	5, SC_MSAREGS, a0, t0
	restore_sc_msareg	6, SC_MSAREGS, a0, t0
	restore_sc_msareg	7, SC_MSAREGS, a0, t0
	restore_sc_msareg	8, SC_MSAREGS, a0, t0
	restore_sc_msareg	9, SC_MSAREGS, a0, t0
	restore_sc_msareg	10, SC_MSAREGS, a0, t0
	restore_sc_msareg	11, SC_MSAREGS, a0, t0
	restore_sc_msareg	12, SC_MSAREGS, a0, t0
	restore_sc_msareg	13, SC_MSAREGS, a0, t0
	restore_sc_msareg	14, SC_MSAREGS, a0, t0
	restore_sc_msareg	15, SC_MSAREGS, a0, t0
	restore_sc_msareg	16, SC_MSAREGS, a0, t0
	restore_sc_msareg	17, SC_MSAREGS, a0, t0
	restore_sc_msareg	18, SC_MSAREGS, a0, t0
	restore_sc_msareg	19, SC_MSAREGS, a0, t0
	restore_sc_msareg	20, SC_MSAREGS, a0, t0
	restore_sc_msareg	21, SC_MSAREGS, a0, t0
	restore_sc_msareg	22, SC_MSAREGS, a0, t0
	restore_sc_msareg	23, SC_MSAREGS, a0, t0
	restore_sc_msareg	24, SC_MSAREGS, a0, t0
	restore_sc_msareg	25, SC_MSAREGS, a0, t0
	restore_sc_msareg	26, SC_MSAREGS, a0, t0
	restore_sc_msareg	27, SC_MSAREGS, a0, t0
	restore_sc_msareg	28, SC_MSAREGS, a0, t0
	restore_sc_msareg	29, SC_MSAREGS, a0, t0
	restore_sc_msareg	30, SC_MSAREGS, a0, t0
	restore_sc_msareg	31, SC_MSAREGS, a0, t0
	jr	ra
	 li	v0, 0
	END(_restore_msa_context)

#ifdef CONFIG_MIPS32_COMPAT

/*
 * int _restore_msa_context32(struct sigcontext32 *sc)
 */
LEAF(_restore_msa_context32)
	restore_sc_msareg	0, SC32_MSAREGS, a0, t0
	restore_sc_msareg	1, SC32_MSAREGS, a0, t0
	restore_sc_msareg	2, SC32_MSAREGS, a0, t0
	restore_sc_msareg	3, SC32_MSAREGS, a0, t0
	restore_sc_msareg	4, SC32_MSAREGS, a0, t0
	restore_sc_msareg	5, SC32_MSAREGS, a0, t0
	restore_sc_msareg	6, SC32_MSAREGS, a0, t0
	restore_sc_msareg	7, SC32_MSAREGS, a0, t0
	restore_sc_msareg	8, SC32_MSAREGS, a0, t0
	restore_sc_msareg	9, SC32_MSAREGS, a0, t0
	restore_sc_msareg	10, SC32_MSAREGS, a0, t0
	restore_sc_msareg	11, SC32_MSAREGS, a0, t0
	restore_sc_msareg	12, SC32_MSAREGS, a0, t0
	restore_sc_msareg	13, SC32_MSAREGS, a0, t0
	restore_sc_msareg	14, SC32_MSAREGS, a0, t0
	restore_sc_msareg	15, SC32_MSAREGS, a0, t0
	restore_sc_msareg	16, SC32_MSAREGS, a0, t0
	restore_sc_msareg	17, SC32_MSAREGS, a0, t0
	restore_sc_msareg	18, SC32_MSAREGS, a0, t0
	restore_sc_msareg	19, SC32_MSAREGS, a0, t0
	restore_sc_msareg	20, SC32_MSAREGS, a0, t0
	restore_sc_msareg	21, SC32_MSAREGS, a0, t0
	restore_sc_msareg	22, SC32_MSAREGS, a0, t0
	restore_sc_msareg	23, SC32_MSAREGS, a0, t0
	restore_sc_msareg	24, SC32_MSAREGS, a0, t0
	restore_sc_msareg	25, SC32_MSAREGS, a0, t0
	restore_sc_msareg	26, SC32_MSAREGS, a0, t0
	restore_sc_msareg	27, SC32_MSAREGS, a0, t0
	restore_sc_msareg	28, SC32_MSAREGS, a0, t0
	restore_sc_msareg	29, SC32_MSAREGS, a0, t0
	restore_sc_msareg	30, SC32_MSAREGS, a0, t0
	restore_sc_msareg	31, SC32_MSAREGS, a0, t0
	jr	ra
	 li	v0, 0
	END(_restore_msa_context32)

#endif /* CONFIG_MIPS32_COMPAT */

#endif /* CONFIG_CPU_HAS_MSA */

	.set	reorder

	.type	fault@function
	.ent	fault
fault:	li	v0, -EFAULT				# failure
	jr	ra
	.end	fault
