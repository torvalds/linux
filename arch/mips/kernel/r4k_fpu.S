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
	cfc1	t1, fcr31
	.set	pop

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2) || \
		defined(CONFIG_CPU_MIPS32_R6)
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
	EX	sdc1 $f1, 8(a0)
	EX	sdc1 $f3, 24(a0)
	EX	sdc1 $f5, 40(a0)
	EX	sdc1 $f7, 56(a0)
	EX	sdc1 $f9, 72(a0)
	EX	sdc1 $f11, 88(a0)
	EX	sdc1 $f13, 104(a0)
	EX	sdc1 $f15, 120(a0)
	EX	sdc1 $f17, 136(a0)
	EX	sdc1 $f19, 152(a0)
	EX	sdc1 $f21, 168(a0)
	EX	sdc1 $f23, 184(a0)
	EX	sdc1 $f25, 200(a0)
	EX	sdc1 $f27, 216(a0)
	EX	sdc1 $f29, 232(a0)
	EX	sdc1 $f31, 248(a0)
1:	.set	pop
#endif

	.set push
	SET_HARDFLOAT
	/* Store the 16 even double precision registers */
	EX	sdc1 $f0, 0(a0)
	EX	sdc1 $f2, 16(a0)
	EX	sdc1 $f4, 32(a0)
	EX	sdc1 $f6, 48(a0)
	EX	sdc1 $f8, 64(a0)
	EX	sdc1 $f10, 80(a0)
	EX	sdc1 $f12, 96(a0)
	EX	sdc1 $f14, 112(a0)
	EX	sdc1 $f16, 128(a0)
	EX	sdc1 $f18, 144(a0)
	EX	sdc1 $f20, 160(a0)
	EX	sdc1 $f22, 176(a0)
	EX	sdc1 $f24, 192(a0)
	EX	sdc1 $f26, 208(a0)
	EX	sdc1 $f28, 224(a0)
	EX	sdc1 $f30, 240(a0)
	EX	sw t1, 0(a1)
	jr	ra
	 li	v0, 0					# success
	.set pop
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
	EX	lw t1, 0(a1)

#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)  || \
		defined(CONFIG_CPU_MIPS32_R6)
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
	EX	ldc1 $f1, 8(a0)
	EX	ldc1 $f3, 24(a0)
	EX	ldc1 $f5, 40(a0)
	EX	ldc1 $f7, 56(a0)
	EX	ldc1 $f9, 72(a0)
	EX	ldc1 $f11, 88(a0)
	EX	ldc1 $f13, 104(a0)
	EX	ldc1 $f15, 120(a0)
	EX	ldc1 $f17, 136(a0)
	EX	ldc1 $f19, 152(a0)
	EX	ldc1 $f21, 168(a0)
	EX	ldc1 $f23, 184(a0)
	EX	ldc1 $f25, 200(a0)
	EX	ldc1 $f27, 216(a0)
	EX	ldc1 $f29, 232(a0)
	EX	ldc1 $f31, 248(a0)
1:	.set pop
#endif
	.set push
	SET_HARDFLOAT
	EX	ldc1 $f0, 0(a0)
	EX	ldc1 $f2, 16(a0)
	EX	ldc1 $f4, 32(a0)
	EX	ldc1 $f6, 48(a0)
	EX	ldc1 $f8, 64(a0)
	EX	ldc1 $f10, 80(a0)
	EX	ldc1 $f12, 96(a0)
	EX	ldc1 $f14, 112(a0)
	EX	ldc1 $f16, 128(a0)
	EX	ldc1 $f18, 144(a0)
	EX	ldc1 $f20, 160(a0)
	EX	ldc1 $f22, 176(a0)
	EX	ldc1 $f24, 192(a0)
	EX	ldc1 $f26, 208(a0)
	EX	ldc1 $f28, 224(a0)
	EX	ldc1 $f30, 240(a0)
	ctc1	t1, fcr31
	.set pop
	jr	ra
	 li	v0, 0					# success
	END(_restore_fp_context)

#ifdef CONFIG_CPU_HAS_MSA

	.macro	op_one_wr	op, idx, base
	.align	4
\idx:	\op	\idx, 0, \base
	jr	ra
	 nop
	.endm

	.macro	op_msa_wr	name, op
LEAF(\name)
	.set		push
	.set		noreorder
	sll		t0, a0, 4
	PTR_LA		t1, 0f
	PTR_ADDU	t0, t0, t1
	jr		t0
	  nop
	op_one_wr	\op, 0, a1
	op_one_wr	\op, 1, a1
	op_one_wr	\op, 2, a1
	op_one_wr	\op, 3, a1
	op_one_wr	\op, 4, a1
	op_one_wr	\op, 5, a1
	op_one_wr	\op, 6, a1
	op_one_wr	\op, 7, a1
	op_one_wr	\op, 8, a1
	op_one_wr	\op, 9, a1
	op_one_wr	\op, 10, a1
	op_one_wr	\op, 11, a1
	op_one_wr	\op, 12, a1
	op_one_wr	\op, 13, a1
	op_one_wr	\op, 14, a1
	op_one_wr	\op, 15, a1
	op_one_wr	\op, 16, a1
	op_one_wr	\op, 17, a1
	op_one_wr	\op, 18, a1
	op_one_wr	\op, 19, a1
	op_one_wr	\op, 20, a1
	op_one_wr	\op, 21, a1
	op_one_wr	\op, 22, a1
	op_one_wr	\op, 23, a1
	op_one_wr	\op, 24, a1
	op_one_wr	\op, 25, a1
	op_one_wr	\op, 26, a1
	op_one_wr	\op, 27, a1
	op_one_wr	\op, 28, a1
	op_one_wr	\op, 29, a1
	op_one_wr	\op, 30, a1
	op_one_wr	\op, 31, a1
	.set		pop
	END(\name)
	.endm

	op_msa_wr	read_msa_wr_b, st_b
	op_msa_wr	read_msa_wr_h, st_h
	op_msa_wr	read_msa_wr_w, st_w
	op_msa_wr	read_msa_wr_d, st_d

	op_msa_wr	write_msa_wr_b, ld_b
	op_msa_wr	write_msa_wr_h, ld_h
	op_msa_wr	write_msa_wr_w, ld_w
	op_msa_wr	write_msa_wr_d, ld_d

#endif /* CONFIG_CPU_HAS_MSA */

#ifdef CONFIG_CPU_HAS_MSA

	.macro	save_msa_upper	wr, off, base
	.set	push
	.set	noat
#ifdef CONFIG_64BIT
	copy_u_d \wr, 1
	EX sd	$1, \off(\base)
#elif defined(CONFIG_CPU_LITTLE_ENDIAN)
	copy_u_w \wr, 2
	EX sw	$1, \off(\base)
	copy_u_w \wr, 3
	EX sw	$1, (\off+4)(\base)
#else /* CONFIG_CPU_BIG_ENDIAN */
	copy_u_w \wr, 2
	EX sw	$1, (\off+4)(\base)
	copy_u_w \wr, 3
	EX sw	$1, \off(\base)
#endif
	.set	pop
	.endm

LEAF(_save_msa_all_upper)
	save_msa_upper	0, 0x00, a0
	save_msa_upper	1, 0x08, a0
	save_msa_upper	2, 0x10, a0
	save_msa_upper	3, 0x18, a0
	save_msa_upper	4, 0x20, a0
	save_msa_upper	5, 0x28, a0
	save_msa_upper	6, 0x30, a0
	save_msa_upper	7, 0x38, a0
	save_msa_upper	8, 0x40, a0
	save_msa_upper	9, 0x48, a0
	save_msa_upper	10, 0x50, a0
	save_msa_upper	11, 0x58, a0
	save_msa_upper	12, 0x60, a0
	save_msa_upper	13, 0x68, a0
	save_msa_upper	14, 0x70, a0
	save_msa_upper	15, 0x78, a0
	save_msa_upper	16, 0x80, a0
	save_msa_upper	17, 0x88, a0
	save_msa_upper	18, 0x90, a0
	save_msa_upper	19, 0x98, a0
	save_msa_upper	20, 0xa0, a0
	save_msa_upper	21, 0xa8, a0
	save_msa_upper	22, 0xb0, a0
	save_msa_upper	23, 0xb8, a0
	save_msa_upper	24, 0xc0, a0
	save_msa_upper	25, 0xc8, a0
	save_msa_upper	26, 0xd0, a0
	save_msa_upper	27, 0xd8, a0
	save_msa_upper	28, 0xe0, a0
	save_msa_upper	29, 0xe8, a0
	save_msa_upper	30, 0xf0, a0
	save_msa_upper	31, 0xf8, a0
	jr	ra
	 li	v0, 0
	END(_save_msa_all_upper)

	.macro	restore_msa_upper	wr, off, base
	.set	push
	.set	noat
#ifdef CONFIG_64BIT
	EX ld	$1, \off(\base)
	insert_d \wr, 1
#elif defined(CONFIG_CPU_LITTLE_ENDIAN)
	EX lw	$1, \off(\base)
	insert_w \wr, 2
	EX lw	$1, (\off+4)(\base)
	insert_w \wr, 3
#else /* CONFIG_CPU_BIG_ENDIAN */
	EX lw	$1, (\off+4)(\base)
	insert_w \wr, 2
	EX lw	$1, \off(\base)
	insert_w \wr, 3
#endif
	.set	pop
	.endm

LEAF(_restore_msa_all_upper)
	restore_msa_upper	0, 0x00, a0
	restore_msa_upper	1, 0x08, a0
	restore_msa_upper	2, 0x10, a0
	restore_msa_upper	3, 0x18, a0
	restore_msa_upper	4, 0x20, a0
	restore_msa_upper	5, 0x28, a0
	restore_msa_upper	6, 0x30, a0
	restore_msa_upper	7, 0x38, a0
	restore_msa_upper	8, 0x40, a0
	restore_msa_upper	9, 0x48, a0
	restore_msa_upper	10, 0x50, a0
	restore_msa_upper	11, 0x58, a0
	restore_msa_upper	12, 0x60, a0
	restore_msa_upper	13, 0x68, a0
	restore_msa_upper	14, 0x70, a0
	restore_msa_upper	15, 0x78, a0
	restore_msa_upper	16, 0x80, a0
	restore_msa_upper	17, 0x88, a0
	restore_msa_upper	18, 0x90, a0
	restore_msa_upper	19, 0x98, a0
	restore_msa_upper	20, 0xa0, a0
	restore_msa_upper	21, 0xa8, a0
	restore_msa_upper	22, 0xb0, a0
	restore_msa_upper	23, 0xb8, a0
	restore_msa_upper	24, 0xc0, a0
	restore_msa_upper	25, 0xc8, a0
	restore_msa_upper	26, 0xd0, a0
	restore_msa_upper	27, 0xd8, a0
	restore_msa_upper	28, 0xe0, a0
	restore_msa_upper	29, 0xe8, a0
	restore_msa_upper	30, 0xf0, a0
	restore_msa_upper	31, 0xf8, a0
	jr	ra
	 li	v0, 0
	END(_restore_msa_all_upper)

#endif /* CONFIG_CPU_HAS_MSA */

	.set	reorder

	.type	fault@function
	.ent	fault
fault:	li	v0, -EFAULT				# failure
	jr	ra
	.end	fault
