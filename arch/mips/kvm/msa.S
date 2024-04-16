/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * MIPS SIMD Architecture (MSA) context handling code for KVM.
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 */

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>

	.set	noreorder
	.set	noat

LEAF(__kvm_save_msa)
	st_d	0,  VCPU_FPR0,  a0
	st_d	1,  VCPU_FPR1,  a0
	st_d	2,  VCPU_FPR2,  a0
	st_d	3,  VCPU_FPR3,  a0
	st_d	4,  VCPU_FPR4,  a0
	st_d	5,  VCPU_FPR5,  a0
	st_d	6,  VCPU_FPR6,  a0
	st_d	7,  VCPU_FPR7,  a0
	st_d	8,  VCPU_FPR8,  a0
	st_d	9,  VCPU_FPR9,  a0
	st_d	10, VCPU_FPR10, a0
	st_d	11, VCPU_FPR11, a0
	st_d	12, VCPU_FPR12, a0
	st_d	13, VCPU_FPR13, a0
	st_d	14, VCPU_FPR14, a0
	st_d	15, VCPU_FPR15, a0
	st_d	16, VCPU_FPR16, a0
	st_d	17, VCPU_FPR17, a0
	st_d	18, VCPU_FPR18, a0
	st_d	19, VCPU_FPR19, a0
	st_d	20, VCPU_FPR20, a0
	st_d	21, VCPU_FPR21, a0
	st_d	22, VCPU_FPR22, a0
	st_d	23, VCPU_FPR23, a0
	st_d	24, VCPU_FPR24, a0
	st_d	25, VCPU_FPR25, a0
	st_d	26, VCPU_FPR26, a0
	st_d	27, VCPU_FPR27, a0
	st_d	28, VCPU_FPR28, a0
	st_d	29, VCPU_FPR29, a0
	st_d	30, VCPU_FPR30, a0
	st_d	31, VCPU_FPR31, a0
	jr	ra
	 nop
	END(__kvm_save_msa)

LEAF(__kvm_restore_msa)
	ld_d	0,  VCPU_FPR0,  a0
	ld_d	1,  VCPU_FPR1,  a0
	ld_d	2,  VCPU_FPR2,  a0
	ld_d	3,  VCPU_FPR3,  a0
	ld_d	4,  VCPU_FPR4,  a0
	ld_d	5,  VCPU_FPR5,  a0
	ld_d	6,  VCPU_FPR6,  a0
	ld_d	7,  VCPU_FPR7,  a0
	ld_d	8,  VCPU_FPR8,  a0
	ld_d	9,  VCPU_FPR9,  a0
	ld_d	10, VCPU_FPR10, a0
	ld_d	11, VCPU_FPR11, a0
	ld_d	12, VCPU_FPR12, a0
	ld_d	13, VCPU_FPR13, a0
	ld_d	14, VCPU_FPR14, a0
	ld_d	15, VCPU_FPR15, a0
	ld_d	16, VCPU_FPR16, a0
	ld_d	17, VCPU_FPR17, a0
	ld_d	18, VCPU_FPR18, a0
	ld_d	19, VCPU_FPR19, a0
	ld_d	20, VCPU_FPR20, a0
	ld_d	21, VCPU_FPR21, a0
	ld_d	22, VCPU_FPR22, a0
	ld_d	23, VCPU_FPR23, a0
	ld_d	24, VCPU_FPR24, a0
	ld_d	25, VCPU_FPR25, a0
	ld_d	26, VCPU_FPR26, a0
	ld_d	27, VCPU_FPR27, a0
	ld_d	28, VCPU_FPR28, a0
	ld_d	29, VCPU_FPR29, a0
	ld_d	30, VCPU_FPR30, a0
	ld_d	31, VCPU_FPR31, a0
	jr	ra
	 nop
	END(__kvm_restore_msa)

	.macro	kvm_restore_msa_upper	wr, off, base
	.set	push
	.set	noat
#ifdef CONFIG_64BIT
	ld	$1, \off(\base)
	insert_d \wr, 1
#elif defined(CONFIG_CPU_LITTLE_ENDIAN)
	lw	$1, \off(\base)
	insert_w \wr, 2
	lw	$1, (\off+4)(\base)
	insert_w \wr, 3
#else /* CONFIG_CPU_BIG_ENDIAN */
	lw	$1, (\off+4)(\base)
	insert_w \wr, 2
	lw	$1, \off(\base)
	insert_w \wr, 3
#endif
	.set	pop
	.endm

LEAF(__kvm_restore_msa_upper)
	kvm_restore_msa_upper	0,  VCPU_FPR0 +8, a0
	kvm_restore_msa_upper	1,  VCPU_FPR1 +8, a0
	kvm_restore_msa_upper	2,  VCPU_FPR2 +8, a0
	kvm_restore_msa_upper	3,  VCPU_FPR3 +8, a0
	kvm_restore_msa_upper	4,  VCPU_FPR4 +8, a0
	kvm_restore_msa_upper	5,  VCPU_FPR5 +8, a0
	kvm_restore_msa_upper	6,  VCPU_FPR6 +8, a0
	kvm_restore_msa_upper	7,  VCPU_FPR7 +8, a0
	kvm_restore_msa_upper	8,  VCPU_FPR8 +8, a0
	kvm_restore_msa_upper	9,  VCPU_FPR9 +8, a0
	kvm_restore_msa_upper	10, VCPU_FPR10+8, a0
	kvm_restore_msa_upper	11, VCPU_FPR11+8, a0
	kvm_restore_msa_upper	12, VCPU_FPR12+8, a0
	kvm_restore_msa_upper	13, VCPU_FPR13+8, a0
	kvm_restore_msa_upper	14, VCPU_FPR14+8, a0
	kvm_restore_msa_upper	15, VCPU_FPR15+8, a0
	kvm_restore_msa_upper	16, VCPU_FPR16+8, a0
	kvm_restore_msa_upper	17, VCPU_FPR17+8, a0
	kvm_restore_msa_upper	18, VCPU_FPR18+8, a0
	kvm_restore_msa_upper	19, VCPU_FPR19+8, a0
	kvm_restore_msa_upper	20, VCPU_FPR20+8, a0
	kvm_restore_msa_upper	21, VCPU_FPR21+8, a0
	kvm_restore_msa_upper	22, VCPU_FPR22+8, a0
	kvm_restore_msa_upper	23, VCPU_FPR23+8, a0
	kvm_restore_msa_upper	24, VCPU_FPR24+8, a0
	kvm_restore_msa_upper	25, VCPU_FPR25+8, a0
	kvm_restore_msa_upper	26, VCPU_FPR26+8, a0
	kvm_restore_msa_upper	27, VCPU_FPR27+8, a0
	kvm_restore_msa_upper	28, VCPU_FPR28+8, a0
	kvm_restore_msa_upper	29, VCPU_FPR29+8, a0
	kvm_restore_msa_upper	30, VCPU_FPR30+8, a0
	kvm_restore_msa_upper	31, VCPU_FPR31+8, a0
	jr	ra
	 nop
	END(__kvm_restore_msa_upper)

LEAF(__kvm_restore_msacsr)
	lw	t0, VCPU_MSA_CSR(a0)
	/*
	 * The ctcmsa must stay at this offset in __kvm_restore_msacsr.
	 * See kvm_mips_csr_die_notify() which handles t0 containing a value
	 * which triggers an MSA FP Exception, which must be stepped over and
	 * ignored since the set cause bits must remain there for the guest.
	 */
	_ctcmsa	MSA_CSR, t0
	jr	ra
	 nop
	END(__kvm_restore_msacsr)
