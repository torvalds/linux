/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Ralf Baechle
 */
#ifndef _ASM_ASMMACRO_H
#define _ASM_ASMMACRO_H

#include <asm/hazards.h>
#include <asm/asm-offsets.h>
#include <asm/msa.h>

#ifdef CONFIG_32BIT
#include <asm/asmmacro-32.h>
#endif
#ifdef CONFIG_64BIT
#include <asm/asmmacro-64.h>
#endif

#if defined(CONFIG_CPU_MIPSR2) || defined(CONFIG_CPU_MIPSR6)
	.macro	local_irq_enable reg=t0
	ei
	irq_enable_hazard
	.endm

	.macro	local_irq_disable reg=t0
	di
	irq_disable_hazard
	.endm
#else
	.macro	local_irq_enable reg=t0
	mfc0	\reg, CP0_STATUS
	ori	\reg, \reg, 1
	mtc0	\reg, CP0_STATUS
	irq_enable_hazard
	.endm

	.macro	local_irq_disable reg=t0
#ifdef CONFIG_PREEMPT
	lw      \reg, TI_PRE_COUNT($28)
	addi    \reg, \reg, 1
	sw      \reg, TI_PRE_COUNT($28)
#endif
	mfc0	\reg, CP0_STATUS
	ori	\reg, \reg, 1
	xori	\reg, \reg, 1
	mtc0	\reg, CP0_STATUS
	irq_disable_hazard
#ifdef CONFIG_PREEMPT
	lw      \reg, TI_PRE_COUNT($28)
	addi    \reg, \reg, -1
	sw      \reg, TI_PRE_COUNT($28)
#endif
	.endm
#endif /* CONFIG_CPU_MIPSR2 */

	.macro	fpu_save_16even thread tmp=t0
	.set	push
	SET_HARDFLOAT
	cfc1	\tmp, fcr31
	sdc1	$f0,  THREAD_FPR0(\thread)
	sdc1	$f2,  THREAD_FPR2(\thread)
	sdc1	$f4,  THREAD_FPR4(\thread)
	sdc1	$f6,  THREAD_FPR6(\thread)
	sdc1	$f8,  THREAD_FPR8(\thread)
	sdc1	$f10, THREAD_FPR10(\thread)
	sdc1	$f12, THREAD_FPR12(\thread)
	sdc1	$f14, THREAD_FPR14(\thread)
	sdc1	$f16, THREAD_FPR16(\thread)
	sdc1	$f18, THREAD_FPR18(\thread)
	sdc1	$f20, THREAD_FPR20(\thread)
	sdc1	$f22, THREAD_FPR22(\thread)
	sdc1	$f24, THREAD_FPR24(\thread)
	sdc1	$f26, THREAD_FPR26(\thread)
	sdc1	$f28, THREAD_FPR28(\thread)
	sdc1	$f30, THREAD_FPR30(\thread)
	sw	\tmp, THREAD_FCR31(\thread)
	.set	pop
	.endm

	.macro	fpu_save_16odd thread
	.set	push
	.set	mips64r2
	SET_HARDFLOAT
	sdc1	$f1,  THREAD_FPR1(\thread)
	sdc1	$f3,  THREAD_FPR3(\thread)
	sdc1	$f5,  THREAD_FPR5(\thread)
	sdc1	$f7,  THREAD_FPR7(\thread)
	sdc1	$f9,  THREAD_FPR9(\thread)
	sdc1	$f11, THREAD_FPR11(\thread)
	sdc1	$f13, THREAD_FPR13(\thread)
	sdc1	$f15, THREAD_FPR15(\thread)
	sdc1	$f17, THREAD_FPR17(\thread)
	sdc1	$f19, THREAD_FPR19(\thread)
	sdc1	$f21, THREAD_FPR21(\thread)
	sdc1	$f23, THREAD_FPR23(\thread)
	sdc1	$f25, THREAD_FPR25(\thread)
	sdc1	$f27, THREAD_FPR27(\thread)
	sdc1	$f29, THREAD_FPR29(\thread)
	sdc1	$f31, THREAD_FPR31(\thread)
	.set	pop
	.endm

	.macro	fpu_save_double thread status tmp
#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2) || \
		defined(CONFIG_CPU_MIPS32_R6)
	sll	\tmp, \status, 5
	bgez	\tmp, 10f
	fpu_save_16odd \thread
10:
#endif
	fpu_save_16even \thread \tmp
	.endm

	.macro	fpu_restore_16even thread tmp=t0
	.set	push
	SET_HARDFLOAT
	lw	\tmp, THREAD_FCR31(\thread)
	ldc1	$f0,  THREAD_FPR0(\thread)
	ldc1	$f2,  THREAD_FPR2(\thread)
	ldc1	$f4,  THREAD_FPR4(\thread)
	ldc1	$f6,  THREAD_FPR6(\thread)
	ldc1	$f8,  THREAD_FPR8(\thread)
	ldc1	$f10, THREAD_FPR10(\thread)
	ldc1	$f12, THREAD_FPR12(\thread)
	ldc1	$f14, THREAD_FPR14(\thread)
	ldc1	$f16, THREAD_FPR16(\thread)
	ldc1	$f18, THREAD_FPR18(\thread)
	ldc1	$f20, THREAD_FPR20(\thread)
	ldc1	$f22, THREAD_FPR22(\thread)
	ldc1	$f24, THREAD_FPR24(\thread)
	ldc1	$f26, THREAD_FPR26(\thread)
	ldc1	$f28, THREAD_FPR28(\thread)
	ldc1	$f30, THREAD_FPR30(\thread)
	ctc1	\tmp, fcr31
	.endm

	.macro	fpu_restore_16odd thread
	.set	push
	.set	mips64r2
	SET_HARDFLOAT
	ldc1	$f1,  THREAD_FPR1(\thread)
	ldc1	$f3,  THREAD_FPR3(\thread)
	ldc1	$f5,  THREAD_FPR5(\thread)
	ldc1	$f7,  THREAD_FPR7(\thread)
	ldc1	$f9,  THREAD_FPR9(\thread)
	ldc1	$f11, THREAD_FPR11(\thread)
	ldc1	$f13, THREAD_FPR13(\thread)
	ldc1	$f15, THREAD_FPR15(\thread)
	ldc1	$f17, THREAD_FPR17(\thread)
	ldc1	$f19, THREAD_FPR19(\thread)
	ldc1	$f21, THREAD_FPR21(\thread)
	ldc1	$f23, THREAD_FPR23(\thread)
	ldc1	$f25, THREAD_FPR25(\thread)
	ldc1	$f27, THREAD_FPR27(\thread)
	ldc1	$f29, THREAD_FPR29(\thread)
	ldc1	$f31, THREAD_FPR31(\thread)
	.set	pop
	.endm

	.macro	fpu_restore_double thread status tmp
#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2) || \
		defined(CONFIG_CPU_MIPS32_R6)
	sll	\tmp, \status, 5
	bgez	\tmp, 10f				# 16 register mode?

	fpu_restore_16odd \thread
10:
#endif
	fpu_restore_16even \thread \tmp
	.endm

#if defined(CONFIG_CPU_MIPSR2) || defined(CONFIG_CPU_MIPSR6)
	.macro	_EXT	rd, rs, p, s
	ext	\rd, \rs, \p, \s
	.endm
#else /* !CONFIG_CPU_MIPSR2 || !CONFIG_CPU_MIPSR6 */
	.macro	_EXT	rd, rs, p, s
	srl	\rd, \rs, \p
	andi	\rd, \rd, (1 << \s) - 1
	.endm
#endif /* !CONFIG_CPU_MIPSR2 || !CONFIG_CPU_MIPSR6 */

/*
 * Temporary until all gas have MT ASE support
 */
	.macro	DMT	reg=0
	.word	0x41600bc1 | (\reg << 16)
	.endm

	.macro	EMT	reg=0
	.word	0x41600be1 | (\reg << 16)
	.endm

	.macro	DVPE	reg=0
	.word	0x41600001 | (\reg << 16)
	.endm

	.macro	EVPE	reg=0
	.word	0x41600021 | (\reg << 16)
	.endm

	.macro	MFTR	rt=0, rd=0, u=0, sel=0
	 .word	0x41000000 | (\rt << 16) | (\rd << 11) | (\u << 5) | (\sel)
	.endm

	.macro	MTTR	rt=0, rd=0, u=0, sel=0
	 .word	0x41800000 | (\rt << 16) | (\rd << 11) | (\u << 5) | (\sel)
	.endm

#ifdef TOOLCHAIN_SUPPORTS_MSA
	.macro	_cfcmsa	rd, cs
	.set	push
	.set	mips32r2
	.set	msa
	cfcmsa	\rd, $\cs
	.set	pop
	.endm

	.macro	_ctcmsa	cd, rs
	.set	push
	.set	mips32r2
	.set	msa
	ctcmsa	$\cd, \rs
	.set	pop
	.endm

	.macro	ld_d	wd, off, base
	.set	push
	.set	mips32r2
	.set	msa
	ld.d	$w\wd, \off(\base)
	.set	pop
	.endm

	.macro	st_d	wd, off, base
	.set	push
	.set	mips32r2
	.set	msa
	st.d	$w\wd, \off(\base)
	.set	pop
	.endm

	.macro	copy_u_w	ws, n
	.set	push
	.set	mips32r2
	.set	msa
	copy_u.w $1, $w\ws[\n]
	.set	pop
	.endm

	.macro	copy_u_d	ws, n
	.set	push
	.set	mips64r2
	.set	msa
	copy_u.d $1, $w\ws[\n]
	.set	pop
	.endm

	.macro	insert_w	wd, n
	.set	push
	.set	mips32r2
	.set	msa
	insert.w $w\wd[\n], $1
	.set	pop
	.endm

	.macro	insert_d	wd, n
	.set	push
	.set	mips64r2
	.set	msa
	insert.d $w\wd[\n], $1
	.set	pop
	.endm
#else

#ifdef CONFIG_CPU_MICROMIPS
#define CFC_MSA_INSN		0x587e0056
#define CTC_MSA_INSN		0x583e0816
#define LDD_MSA_INSN		0x58000837
#define STD_MSA_INSN		0x5800083f
#define COPY_UW_MSA_INSN	0x58f00056
#define COPY_UD_MSA_INSN	0x58f80056
#define INSERT_W_MSA_INSN	0x59300816
#define INSERT_D_MSA_INSN	0x59380816
#else
#define CFC_MSA_INSN		0x787e0059
#define CTC_MSA_INSN		0x783e0819
#define LDD_MSA_INSN		0x78000823
#define STD_MSA_INSN		0x78000827
#define COPY_UW_MSA_INSN	0x78f00059
#define COPY_UD_MSA_INSN	0x78f80059
#define INSERT_W_MSA_INSN	0x79300819
#define INSERT_D_MSA_INSN	0x79380819
#endif

	/*
	 * Temporary until all toolchains in use include MSA support.
	 */
	.macro	_cfcmsa	rd, cs
	.set	push
	.set	noat
	SET_HARDFLOAT
	.insn
	.word	CFC_MSA_INSN | (\cs << 11)
	move	\rd, $1
	.set	pop
	.endm

	.macro	_ctcmsa	cd, rs
	.set	push
	.set	noat
	SET_HARDFLOAT
	move	$1, \rs
	.word	CTC_MSA_INSN | (\cd << 6)
	.set	pop
	.endm

	.macro	ld_d	wd, off, base
	.set	push
	.set	noat
	SET_HARDFLOAT
	addu	$1, \base, \off
	.word	LDD_MSA_INSN | (\wd << 6)
	.set	pop
	.endm

	.macro	st_d	wd, off, base
	.set	push
	.set	noat
	SET_HARDFLOAT
	addu	$1, \base, \off
	.word	STD_MSA_INSN | (\wd << 6)
	.set	pop
	.endm

	.macro	copy_u_w	ws, n
	.set	push
	.set	noat
	SET_HARDFLOAT
	.insn
	.word	COPY_UW_MSA_INSN | (\n << 16) | (\ws << 11)
	.set	pop
	.endm

	.macro	copy_u_d	ws, n
	.set	push
	.set	noat
	SET_HARDFLOAT
	.insn
	.word	COPY_UD_MSA_INSN | (\n << 16) | (\ws << 11)
	.set	pop
	.endm

	.macro	insert_w	wd, n
	.set	push
	.set	noat
	SET_HARDFLOAT
	.word	INSERT_W_MSA_INSN | (\n << 16) | (\wd << 6)
	.set	pop
	.endm

	.macro	insert_d	wd, n
	.set	push
	.set	noat
	SET_HARDFLOAT
	.word	INSERT_D_MSA_INSN | (\n << 16) | (\wd << 6)
	.set	pop
	.endm
#endif

	.macro	msa_save_all	thread
	st_d	0, THREAD_FPR0, \thread
	st_d	1, THREAD_FPR1, \thread
	st_d	2, THREAD_FPR2, \thread
	st_d	3, THREAD_FPR3, \thread
	st_d	4, THREAD_FPR4, \thread
	st_d	5, THREAD_FPR5, \thread
	st_d	6, THREAD_FPR6, \thread
	st_d	7, THREAD_FPR7, \thread
	st_d	8, THREAD_FPR8, \thread
	st_d	9, THREAD_FPR9, \thread
	st_d	10, THREAD_FPR10, \thread
	st_d	11, THREAD_FPR11, \thread
	st_d	12, THREAD_FPR12, \thread
	st_d	13, THREAD_FPR13, \thread
	st_d	14, THREAD_FPR14, \thread
	st_d	15, THREAD_FPR15, \thread
	st_d	16, THREAD_FPR16, \thread
	st_d	17, THREAD_FPR17, \thread
	st_d	18, THREAD_FPR18, \thread
	st_d	19, THREAD_FPR19, \thread
	st_d	20, THREAD_FPR20, \thread
	st_d	21, THREAD_FPR21, \thread
	st_d	22, THREAD_FPR22, \thread
	st_d	23, THREAD_FPR23, \thread
	st_d	24, THREAD_FPR24, \thread
	st_d	25, THREAD_FPR25, \thread
	st_d	26, THREAD_FPR26, \thread
	st_d	27, THREAD_FPR27, \thread
	st_d	28, THREAD_FPR28, \thread
	st_d	29, THREAD_FPR29, \thread
	st_d	30, THREAD_FPR30, \thread
	st_d	31, THREAD_FPR31, \thread
	.set	push
	.set	noat
	SET_HARDFLOAT
	_cfcmsa	$1, MSA_CSR
	sw	$1, THREAD_MSA_CSR(\thread)
	.set	pop
	.endm

	.macro	msa_restore_all	thread
	.set	push
	.set	noat
	SET_HARDFLOAT
	lw	$1, THREAD_MSA_CSR(\thread)
	_ctcmsa	MSA_CSR, $1
	.set	pop
	ld_d	0, THREAD_FPR0, \thread
	ld_d	1, THREAD_FPR1, \thread
	ld_d	2, THREAD_FPR2, \thread
	ld_d	3, THREAD_FPR3, \thread
	ld_d	4, THREAD_FPR4, \thread
	ld_d	5, THREAD_FPR5, \thread
	ld_d	6, THREAD_FPR6, \thread
	ld_d	7, THREAD_FPR7, \thread
	ld_d	8, THREAD_FPR8, \thread
	ld_d	9, THREAD_FPR9, \thread
	ld_d	10, THREAD_FPR10, \thread
	ld_d	11, THREAD_FPR11, \thread
	ld_d	12, THREAD_FPR12, \thread
	ld_d	13, THREAD_FPR13, \thread
	ld_d	14, THREAD_FPR14, \thread
	ld_d	15, THREAD_FPR15, \thread
	ld_d	16, THREAD_FPR16, \thread
	ld_d	17, THREAD_FPR17, \thread
	ld_d	18, THREAD_FPR18, \thread
	ld_d	19, THREAD_FPR19, \thread
	ld_d	20, THREAD_FPR20, \thread
	ld_d	21, THREAD_FPR21, \thread
	ld_d	22, THREAD_FPR22, \thread
	ld_d	23, THREAD_FPR23, \thread
	ld_d	24, THREAD_FPR24, \thread
	ld_d	25, THREAD_FPR25, \thread
	ld_d	26, THREAD_FPR26, \thread
	ld_d	27, THREAD_FPR27, \thread
	ld_d	28, THREAD_FPR28, \thread
	ld_d	29, THREAD_FPR29, \thread
	ld_d	30, THREAD_FPR30, \thread
	ld_d	31, THREAD_FPR31, \thread
	.endm

	.macro	msa_init_upper wd
#ifdef CONFIG_64BIT
	insert_d \wd, 1
#else
	insert_w \wd, 2
	insert_w \wd, 3
#endif
	.endm

	.macro	msa_init_all_upper
	.set	push
	.set	noat
	SET_HARDFLOAT
	not	$1, zero
	msa_init_upper	0
	msa_init_upper	1
	msa_init_upper	2
	msa_init_upper	3
	msa_init_upper	4
	msa_init_upper	5
	msa_init_upper	6
	msa_init_upper	7
	msa_init_upper	8
	msa_init_upper	9
	msa_init_upper	10
	msa_init_upper	11
	msa_init_upper	12
	msa_init_upper	13
	msa_init_upper	14
	msa_init_upper	15
	msa_init_upper	16
	msa_init_upper	17
	msa_init_upper	18
	msa_init_upper	19
	msa_init_upper	20
	msa_init_upper	21
	msa_init_upper	22
	msa_init_upper	23
	msa_init_upper	24
	msa_init_upper	25
	msa_init_upper	26
	msa_init_upper	27
	msa_init_upper	28
	msa_init_upper	29
	msa_init_upper	30
	msa_init_upper	31
	.set	pop
	.endm

#endif /* _ASM_ASMMACRO_H */
