/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * FPU context handling code for KVM.
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 */

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>

/* preprocessor replaces the fp in ".set fp=64" with $30 otherwise */
#undef fp

	.set	noreorder
	.set	noat

LEAF(__kvm_save_fpu)
	.set	push
	.set	hardfloat
	.set	fp=64
	mfc0	t0, CP0_STATUS
	sll     t0, t0, 5			# is Status.FR set?
	bgez    t0, 1f				# no: skip odd doubles
	 nop
	sdc1	$f1,  VCPU_FPR1(a0)
	sdc1	$f3,  VCPU_FPR3(a0)
	sdc1	$f5,  VCPU_FPR5(a0)
	sdc1	$f7,  VCPU_FPR7(a0)
	sdc1	$f9,  VCPU_FPR9(a0)
	sdc1	$f11, VCPU_FPR11(a0)
	sdc1	$f13, VCPU_FPR13(a0)
	sdc1	$f15, VCPU_FPR15(a0)
	sdc1	$f17, VCPU_FPR17(a0)
	sdc1	$f19, VCPU_FPR19(a0)
	sdc1	$f21, VCPU_FPR21(a0)
	sdc1	$f23, VCPU_FPR23(a0)
	sdc1	$f25, VCPU_FPR25(a0)
	sdc1	$f27, VCPU_FPR27(a0)
	sdc1	$f29, VCPU_FPR29(a0)
	sdc1	$f31, VCPU_FPR31(a0)
1:	sdc1	$f0,  VCPU_FPR0(a0)
	sdc1	$f2,  VCPU_FPR2(a0)
	sdc1	$f4,  VCPU_FPR4(a0)
	sdc1	$f6,  VCPU_FPR6(a0)
	sdc1	$f8,  VCPU_FPR8(a0)
	sdc1	$f10, VCPU_FPR10(a0)
	sdc1	$f12, VCPU_FPR12(a0)
	sdc1	$f14, VCPU_FPR14(a0)
	sdc1	$f16, VCPU_FPR16(a0)
	sdc1	$f18, VCPU_FPR18(a0)
	sdc1	$f20, VCPU_FPR20(a0)
	sdc1	$f22, VCPU_FPR22(a0)
	sdc1	$f24, VCPU_FPR24(a0)
	sdc1	$f26, VCPU_FPR26(a0)
	sdc1	$f28, VCPU_FPR28(a0)
	jr	ra
	 sdc1	$f30, VCPU_FPR30(a0)
	.set	pop
	END(__kvm_save_fpu)

LEAF(__kvm_restore_fpu)
	.set	push
	.set	hardfloat
	.set	fp=64
	mfc0	t0, CP0_STATUS
	sll     t0, t0, 5			# is Status.FR set?
	bgez    t0, 1f				# no: skip odd doubles
	 nop
	ldc1	$f1,  VCPU_FPR1(a0)
	ldc1	$f3,  VCPU_FPR3(a0)
	ldc1	$f5,  VCPU_FPR5(a0)
	ldc1	$f7,  VCPU_FPR7(a0)
	ldc1	$f9,  VCPU_FPR9(a0)
	ldc1	$f11, VCPU_FPR11(a0)
	ldc1	$f13, VCPU_FPR13(a0)
	ldc1	$f15, VCPU_FPR15(a0)
	ldc1	$f17, VCPU_FPR17(a0)
	ldc1	$f19, VCPU_FPR19(a0)
	ldc1	$f21, VCPU_FPR21(a0)
	ldc1	$f23, VCPU_FPR23(a0)
	ldc1	$f25, VCPU_FPR25(a0)
	ldc1	$f27, VCPU_FPR27(a0)
	ldc1	$f29, VCPU_FPR29(a0)
	ldc1	$f31, VCPU_FPR31(a0)
1:	ldc1	$f0,  VCPU_FPR0(a0)
	ldc1	$f2,  VCPU_FPR2(a0)
	ldc1	$f4,  VCPU_FPR4(a0)
	ldc1	$f6,  VCPU_FPR6(a0)
	ldc1	$f8,  VCPU_FPR8(a0)
	ldc1	$f10, VCPU_FPR10(a0)
	ldc1	$f12, VCPU_FPR12(a0)
	ldc1	$f14, VCPU_FPR14(a0)
	ldc1	$f16, VCPU_FPR16(a0)
	ldc1	$f18, VCPU_FPR18(a0)
	ldc1	$f20, VCPU_FPR20(a0)
	ldc1	$f22, VCPU_FPR22(a0)
	ldc1	$f24, VCPU_FPR24(a0)
	ldc1	$f26, VCPU_FPR26(a0)
	ldc1	$f28, VCPU_FPR28(a0)
	jr	ra
	 ldc1	$f30, VCPU_FPR30(a0)
	.set	pop
	END(__kvm_restore_fpu)

LEAF(__kvm_restore_fcsr)
	.set	push
	.set	hardfloat
	lw	t0, VCPU_FCR31(a0)
	/*
	 * The ctc1 must stay at this offset in __kvm_restore_fcsr.
	 * See kvm_mips_csr_die_notify() which handles t0 containing a value
	 * which triggers an FP Exception, which must be stepped over and
	 * ignored since the set cause bits must remain there for the guest.
	 */
	ctc1	t0, fcr31
	jr	ra
	 nop
	.set	pop
	END(__kvm_restore_fcsr)
