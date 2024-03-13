/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/csr.h>

	.text
	.altmacro
	.option norelax

SYM_FUNC_START(__kvm_riscv_switch_to)
	/* Save Host GPRs (except A0 and T0-T6) */
	REG_S	ra, (KVM_ARCH_HOST_RA)(a0)
	REG_S	sp, (KVM_ARCH_HOST_SP)(a0)
	REG_S	gp, (KVM_ARCH_HOST_GP)(a0)
	REG_S	tp, (KVM_ARCH_HOST_TP)(a0)
	REG_S	s0, (KVM_ARCH_HOST_S0)(a0)
	REG_S	s1, (KVM_ARCH_HOST_S1)(a0)
	REG_S	a1, (KVM_ARCH_HOST_A1)(a0)
	REG_S	a2, (KVM_ARCH_HOST_A2)(a0)
	REG_S	a3, (KVM_ARCH_HOST_A3)(a0)
	REG_S	a4, (KVM_ARCH_HOST_A4)(a0)
	REG_S	a5, (KVM_ARCH_HOST_A5)(a0)
	REG_S	a6, (KVM_ARCH_HOST_A6)(a0)
	REG_S	a7, (KVM_ARCH_HOST_A7)(a0)
	REG_S	s2, (KVM_ARCH_HOST_S2)(a0)
	REG_S	s3, (KVM_ARCH_HOST_S3)(a0)
	REG_S	s4, (KVM_ARCH_HOST_S4)(a0)
	REG_S	s5, (KVM_ARCH_HOST_S5)(a0)
	REG_S	s6, (KVM_ARCH_HOST_S6)(a0)
	REG_S	s7, (KVM_ARCH_HOST_S7)(a0)
	REG_S	s8, (KVM_ARCH_HOST_S8)(a0)
	REG_S	s9, (KVM_ARCH_HOST_S9)(a0)
	REG_S	s10, (KVM_ARCH_HOST_S10)(a0)
	REG_S	s11, (KVM_ARCH_HOST_S11)(a0)

	/* Load Guest CSR values */
	REG_L	t0, (KVM_ARCH_GUEST_SSTATUS)(a0)
	REG_L	t1, (KVM_ARCH_GUEST_HSTATUS)(a0)
	REG_L	t2, (KVM_ARCH_GUEST_SCOUNTEREN)(a0)
	la	t4, .Lkvm_switch_return
	REG_L	t5, (KVM_ARCH_GUEST_SEPC)(a0)

	/* Save Host and Restore Guest SSTATUS */
	csrrw	t0, CSR_SSTATUS, t0

	/* Save Host and Restore Guest HSTATUS */
	csrrw	t1, CSR_HSTATUS, t1

	/* Save Host and Restore Guest SCOUNTEREN */
	csrrw	t2, CSR_SCOUNTEREN, t2

	/* Save Host STVEC and change it to return path */
	csrrw	t4, CSR_STVEC, t4

	/* Save Host SSCRATCH and change it to struct kvm_vcpu_arch pointer */
	csrrw	t3, CSR_SSCRATCH, a0

	/* Restore Guest SEPC */
	csrw	CSR_SEPC, t5

	/* Store Host CSR values */
	REG_S	t0, (KVM_ARCH_HOST_SSTATUS)(a0)
	REG_S	t1, (KVM_ARCH_HOST_HSTATUS)(a0)
	REG_S	t2, (KVM_ARCH_HOST_SCOUNTEREN)(a0)
	REG_S	t3, (KVM_ARCH_HOST_SSCRATCH)(a0)
	REG_S	t4, (KVM_ARCH_HOST_STVEC)(a0)

	/* Restore Guest GPRs (except A0) */
	REG_L	ra, (KVM_ARCH_GUEST_RA)(a0)
	REG_L	sp, (KVM_ARCH_GUEST_SP)(a0)
	REG_L	gp, (KVM_ARCH_GUEST_GP)(a0)
	REG_L	tp, (KVM_ARCH_GUEST_TP)(a0)
	REG_L	t0, (KVM_ARCH_GUEST_T0)(a0)
	REG_L	t1, (KVM_ARCH_GUEST_T1)(a0)
	REG_L	t2, (KVM_ARCH_GUEST_T2)(a0)
	REG_L	s0, (KVM_ARCH_GUEST_S0)(a0)
	REG_L	s1, (KVM_ARCH_GUEST_S1)(a0)
	REG_L	a1, (KVM_ARCH_GUEST_A1)(a0)
	REG_L	a2, (KVM_ARCH_GUEST_A2)(a0)
	REG_L	a3, (KVM_ARCH_GUEST_A3)(a0)
	REG_L	a4, (KVM_ARCH_GUEST_A4)(a0)
	REG_L	a5, (KVM_ARCH_GUEST_A5)(a0)
	REG_L	a6, (KVM_ARCH_GUEST_A6)(a0)
	REG_L	a7, (KVM_ARCH_GUEST_A7)(a0)
	REG_L	s2, (KVM_ARCH_GUEST_S2)(a0)
	REG_L	s3, (KVM_ARCH_GUEST_S3)(a0)
	REG_L	s4, (KVM_ARCH_GUEST_S4)(a0)
	REG_L	s5, (KVM_ARCH_GUEST_S5)(a0)
	REG_L	s6, (KVM_ARCH_GUEST_S6)(a0)
	REG_L	s7, (KVM_ARCH_GUEST_S7)(a0)
	REG_L	s8, (KVM_ARCH_GUEST_S8)(a0)
	REG_L	s9, (KVM_ARCH_GUEST_S9)(a0)
	REG_L	s10, (KVM_ARCH_GUEST_S10)(a0)
	REG_L	s11, (KVM_ARCH_GUEST_S11)(a0)
	REG_L	t3, (KVM_ARCH_GUEST_T3)(a0)
	REG_L	t4, (KVM_ARCH_GUEST_T4)(a0)
	REG_L	t5, (KVM_ARCH_GUEST_T5)(a0)
	REG_L	t6, (KVM_ARCH_GUEST_T6)(a0)

	/* Restore Guest A0 */
	REG_L	a0, (KVM_ARCH_GUEST_A0)(a0)

	/* Resume Guest */
	sret

	/* Back to Host */
	.align 2
.Lkvm_switch_return:
	/* Swap Guest A0 with SSCRATCH */
	csrrw	a0, CSR_SSCRATCH, a0

	/* Save Guest GPRs (except A0) */
	REG_S	ra, (KVM_ARCH_GUEST_RA)(a0)
	REG_S	sp, (KVM_ARCH_GUEST_SP)(a0)
	REG_S	gp, (KVM_ARCH_GUEST_GP)(a0)
	REG_S	tp, (KVM_ARCH_GUEST_TP)(a0)
	REG_S	t0, (KVM_ARCH_GUEST_T0)(a0)
	REG_S	t1, (KVM_ARCH_GUEST_T1)(a0)
	REG_S	t2, (KVM_ARCH_GUEST_T2)(a0)
	REG_S	s0, (KVM_ARCH_GUEST_S0)(a0)
	REG_S	s1, (KVM_ARCH_GUEST_S1)(a0)
	REG_S	a1, (KVM_ARCH_GUEST_A1)(a0)
	REG_S	a2, (KVM_ARCH_GUEST_A2)(a0)
	REG_S	a3, (KVM_ARCH_GUEST_A3)(a0)
	REG_S	a4, (KVM_ARCH_GUEST_A4)(a0)
	REG_S	a5, (KVM_ARCH_GUEST_A5)(a0)
	REG_S	a6, (KVM_ARCH_GUEST_A6)(a0)
	REG_S	a7, (KVM_ARCH_GUEST_A7)(a0)
	REG_S	s2, (KVM_ARCH_GUEST_S2)(a0)
	REG_S	s3, (KVM_ARCH_GUEST_S3)(a0)
	REG_S	s4, (KVM_ARCH_GUEST_S4)(a0)
	REG_S	s5, (KVM_ARCH_GUEST_S5)(a0)
	REG_S	s6, (KVM_ARCH_GUEST_S6)(a0)
	REG_S	s7, (KVM_ARCH_GUEST_S7)(a0)
	REG_S	s8, (KVM_ARCH_GUEST_S8)(a0)
	REG_S	s9, (KVM_ARCH_GUEST_S9)(a0)
	REG_S	s10, (KVM_ARCH_GUEST_S10)(a0)
	REG_S	s11, (KVM_ARCH_GUEST_S11)(a0)
	REG_S	t3, (KVM_ARCH_GUEST_T3)(a0)
	REG_S	t4, (KVM_ARCH_GUEST_T4)(a0)
	REG_S	t5, (KVM_ARCH_GUEST_T5)(a0)
	REG_S	t6, (KVM_ARCH_GUEST_T6)(a0)

	/* Load Host CSR values */
	REG_L	t1, (KVM_ARCH_HOST_STVEC)(a0)
	REG_L	t2, (KVM_ARCH_HOST_SSCRATCH)(a0)
	REG_L	t3, (KVM_ARCH_HOST_SCOUNTEREN)(a0)
	REG_L	t4, (KVM_ARCH_HOST_HSTATUS)(a0)
	REG_L	t5, (KVM_ARCH_HOST_SSTATUS)(a0)

	/* Save Guest SEPC */
	csrr	t0, CSR_SEPC

	/* Save Guest A0 and Restore Host SSCRATCH */
	csrrw	t2, CSR_SSCRATCH, t2

	/* Restore Host STVEC */
	csrw	CSR_STVEC, t1

	/* Save Guest and Restore Host SCOUNTEREN */
	csrrw	t3, CSR_SCOUNTEREN, t3

	/* Save Guest and Restore Host HSTATUS */
	csrrw	t4, CSR_HSTATUS, t4

	/* Save Guest and Restore Host SSTATUS */
	csrrw	t5, CSR_SSTATUS, t5

	/* Store Guest CSR values */
	REG_S	t0, (KVM_ARCH_GUEST_SEPC)(a0)
	REG_S	t2, (KVM_ARCH_GUEST_A0)(a0)
	REG_S	t3, (KVM_ARCH_GUEST_SCOUNTEREN)(a0)
	REG_S	t4, (KVM_ARCH_GUEST_HSTATUS)(a0)
	REG_S	t5, (KVM_ARCH_GUEST_SSTATUS)(a0)

	/* Restore Host GPRs (except A0 and T0-T6) */
	REG_L	ra, (KVM_ARCH_HOST_RA)(a0)
	REG_L	sp, (KVM_ARCH_HOST_SP)(a0)
	REG_L	gp, (KVM_ARCH_HOST_GP)(a0)
	REG_L	tp, (KVM_ARCH_HOST_TP)(a0)
	REG_L	s0, (KVM_ARCH_HOST_S0)(a0)
	REG_L	s1, (KVM_ARCH_HOST_S1)(a0)
	REG_L	a1, (KVM_ARCH_HOST_A1)(a0)
	REG_L	a2, (KVM_ARCH_HOST_A2)(a0)
	REG_L	a3, (KVM_ARCH_HOST_A3)(a0)
	REG_L	a4, (KVM_ARCH_HOST_A4)(a0)
	REG_L	a5, (KVM_ARCH_HOST_A5)(a0)
	REG_L	a6, (KVM_ARCH_HOST_A6)(a0)
	REG_L	a7, (KVM_ARCH_HOST_A7)(a0)
	REG_L	s2, (KVM_ARCH_HOST_S2)(a0)
	REG_L	s3, (KVM_ARCH_HOST_S3)(a0)
	REG_L	s4, (KVM_ARCH_HOST_S4)(a0)
	REG_L	s5, (KVM_ARCH_HOST_S5)(a0)
	REG_L	s6, (KVM_ARCH_HOST_S6)(a0)
	REG_L	s7, (KVM_ARCH_HOST_S7)(a0)
	REG_L	s8, (KVM_ARCH_HOST_S8)(a0)
	REG_L	s9, (KVM_ARCH_HOST_S9)(a0)
	REG_L	s10, (KVM_ARCH_HOST_S10)(a0)
	REG_L	s11, (KVM_ARCH_HOST_S11)(a0)

	/* Return to C code */
	ret
SYM_FUNC_END(__kvm_riscv_switch_to)

SYM_CODE_START(__kvm_riscv_unpriv_trap)
	/*
	 * We assume that faulting unpriv load/store instruction is
	 * 4-byte long and blindly increment SEPC by 4.
	 *
	 * The trap details will be saved at address pointed by 'A0'
	 * register and we use 'A1' register as temporary.
	 */
	csrr	a1, CSR_SEPC
	REG_S	a1, (KVM_ARCH_TRAP_SEPC)(a0)
	addi	a1, a1, 4
	csrw	CSR_SEPC, a1
	csrr	a1, CSR_SCAUSE
	REG_S	a1, (KVM_ARCH_TRAP_SCAUSE)(a0)
	csrr	a1, CSR_STVAL
	REG_S	a1, (KVM_ARCH_TRAP_STVAL)(a0)
	csrr	a1, CSR_HTVAL
	REG_S	a1, (KVM_ARCH_TRAP_HTVAL)(a0)
	csrr	a1, CSR_HTINST
	REG_S	a1, (KVM_ARCH_TRAP_HTINST)(a0)
	sret
SYM_CODE_END(__kvm_riscv_unpriv_trap)

#ifdef	CONFIG_FPU
SYM_FUNC_START(__kvm_riscv_fp_f_save)
	csrr t2, CSR_SSTATUS
	li t1, SR_FS
	csrs CSR_SSTATUS, t1
	frcsr t0
	fsw f0,  KVM_ARCH_FP_F_F0(a0)
	fsw f1,  KVM_ARCH_FP_F_F1(a0)
	fsw f2,  KVM_ARCH_FP_F_F2(a0)
	fsw f3,  KVM_ARCH_FP_F_F3(a0)
	fsw f4,  KVM_ARCH_FP_F_F4(a0)
	fsw f5,  KVM_ARCH_FP_F_F5(a0)
	fsw f6,  KVM_ARCH_FP_F_F6(a0)
	fsw f7,  KVM_ARCH_FP_F_F7(a0)
	fsw f8,  KVM_ARCH_FP_F_F8(a0)
	fsw f9,  KVM_ARCH_FP_F_F9(a0)
	fsw f10, KVM_ARCH_FP_F_F10(a0)
	fsw f11, KVM_ARCH_FP_F_F11(a0)
	fsw f12, KVM_ARCH_FP_F_F12(a0)
	fsw f13, KVM_ARCH_FP_F_F13(a0)
	fsw f14, KVM_ARCH_FP_F_F14(a0)
	fsw f15, KVM_ARCH_FP_F_F15(a0)
	fsw f16, KVM_ARCH_FP_F_F16(a0)
	fsw f17, KVM_ARCH_FP_F_F17(a0)
	fsw f18, KVM_ARCH_FP_F_F18(a0)
	fsw f19, KVM_ARCH_FP_F_F19(a0)
	fsw f20, KVM_ARCH_FP_F_F20(a0)
	fsw f21, KVM_ARCH_FP_F_F21(a0)
	fsw f22, KVM_ARCH_FP_F_F22(a0)
	fsw f23, KVM_ARCH_FP_F_F23(a0)
	fsw f24, KVM_ARCH_FP_F_F24(a0)
	fsw f25, KVM_ARCH_FP_F_F25(a0)
	fsw f26, KVM_ARCH_FP_F_F26(a0)
	fsw f27, KVM_ARCH_FP_F_F27(a0)
	fsw f28, KVM_ARCH_FP_F_F28(a0)
	fsw f29, KVM_ARCH_FP_F_F29(a0)
	fsw f30, KVM_ARCH_FP_F_F30(a0)
	fsw f31, KVM_ARCH_FP_F_F31(a0)
	sw t0, KVM_ARCH_FP_F_FCSR(a0)
	csrw CSR_SSTATUS, t2
	ret
SYM_FUNC_END(__kvm_riscv_fp_f_save)

SYM_FUNC_START(__kvm_riscv_fp_d_save)
	csrr t2, CSR_SSTATUS
	li t1, SR_FS
	csrs CSR_SSTATUS, t1
	frcsr t0
	fsd f0,  KVM_ARCH_FP_D_F0(a0)
	fsd f1,  KVM_ARCH_FP_D_F1(a0)
	fsd f2,  KVM_ARCH_FP_D_F2(a0)
	fsd f3,  KVM_ARCH_FP_D_F3(a0)
	fsd f4,  KVM_ARCH_FP_D_F4(a0)
	fsd f5,  KVM_ARCH_FP_D_F5(a0)
	fsd f6,  KVM_ARCH_FP_D_F6(a0)
	fsd f7,  KVM_ARCH_FP_D_F7(a0)
	fsd f8,  KVM_ARCH_FP_D_F8(a0)
	fsd f9,  KVM_ARCH_FP_D_F9(a0)
	fsd f10, KVM_ARCH_FP_D_F10(a0)
	fsd f11, KVM_ARCH_FP_D_F11(a0)
	fsd f12, KVM_ARCH_FP_D_F12(a0)
	fsd f13, KVM_ARCH_FP_D_F13(a0)
	fsd f14, KVM_ARCH_FP_D_F14(a0)
	fsd f15, KVM_ARCH_FP_D_F15(a0)
	fsd f16, KVM_ARCH_FP_D_F16(a0)
	fsd f17, KVM_ARCH_FP_D_F17(a0)
	fsd f18, KVM_ARCH_FP_D_F18(a0)
	fsd f19, KVM_ARCH_FP_D_F19(a0)
	fsd f20, KVM_ARCH_FP_D_F20(a0)
	fsd f21, KVM_ARCH_FP_D_F21(a0)
	fsd f22, KVM_ARCH_FP_D_F22(a0)
	fsd f23, KVM_ARCH_FP_D_F23(a0)
	fsd f24, KVM_ARCH_FP_D_F24(a0)
	fsd f25, KVM_ARCH_FP_D_F25(a0)
	fsd f26, KVM_ARCH_FP_D_F26(a0)
	fsd f27, KVM_ARCH_FP_D_F27(a0)
	fsd f28, KVM_ARCH_FP_D_F28(a0)
	fsd f29, KVM_ARCH_FP_D_F29(a0)
	fsd f30, KVM_ARCH_FP_D_F30(a0)
	fsd f31, KVM_ARCH_FP_D_F31(a0)
	sw t0, KVM_ARCH_FP_D_FCSR(a0)
	csrw CSR_SSTATUS, t2
	ret
SYM_FUNC_END(__kvm_riscv_fp_d_save)

SYM_FUNC_START(__kvm_riscv_fp_f_restore)
	csrr t2, CSR_SSTATUS
	li t1, SR_FS
	lw t0, KVM_ARCH_FP_F_FCSR(a0)
	csrs CSR_SSTATUS, t1
	flw f0,  KVM_ARCH_FP_F_F0(a0)
	flw f1,  KVM_ARCH_FP_F_F1(a0)
	flw f2,  KVM_ARCH_FP_F_F2(a0)
	flw f3,  KVM_ARCH_FP_F_F3(a0)
	flw f4,  KVM_ARCH_FP_F_F4(a0)
	flw f5,  KVM_ARCH_FP_F_F5(a0)
	flw f6,  KVM_ARCH_FP_F_F6(a0)
	flw f7,  KVM_ARCH_FP_F_F7(a0)
	flw f8,  KVM_ARCH_FP_F_F8(a0)
	flw f9,  KVM_ARCH_FP_F_F9(a0)
	flw f10, KVM_ARCH_FP_F_F10(a0)
	flw f11, KVM_ARCH_FP_F_F11(a0)
	flw f12, KVM_ARCH_FP_F_F12(a0)
	flw f13, KVM_ARCH_FP_F_F13(a0)
	flw f14, KVM_ARCH_FP_F_F14(a0)
	flw f15, KVM_ARCH_FP_F_F15(a0)
	flw f16, KVM_ARCH_FP_F_F16(a0)
	flw f17, KVM_ARCH_FP_F_F17(a0)
	flw f18, KVM_ARCH_FP_F_F18(a0)
	flw f19, KVM_ARCH_FP_F_F19(a0)
	flw f20, KVM_ARCH_FP_F_F20(a0)
	flw f21, KVM_ARCH_FP_F_F21(a0)
	flw f22, KVM_ARCH_FP_F_F22(a0)
	flw f23, KVM_ARCH_FP_F_F23(a0)
	flw f24, KVM_ARCH_FP_F_F24(a0)
	flw f25, KVM_ARCH_FP_F_F25(a0)
	flw f26, KVM_ARCH_FP_F_F26(a0)
	flw f27, KVM_ARCH_FP_F_F27(a0)
	flw f28, KVM_ARCH_FP_F_F28(a0)
	flw f29, KVM_ARCH_FP_F_F29(a0)
	flw f30, KVM_ARCH_FP_F_F30(a0)
	flw f31, KVM_ARCH_FP_F_F31(a0)
	fscsr t0
	csrw CSR_SSTATUS, t2
	ret
SYM_FUNC_END(__kvm_riscv_fp_f_restore)

SYM_FUNC_START(__kvm_riscv_fp_d_restore)
	csrr t2, CSR_SSTATUS
	li t1, SR_FS
	lw t0, KVM_ARCH_FP_D_FCSR(a0)
	csrs CSR_SSTATUS, t1
	fld f0,  KVM_ARCH_FP_D_F0(a0)
	fld f1,  KVM_ARCH_FP_D_F1(a0)
	fld f2,  KVM_ARCH_FP_D_F2(a0)
	fld f3,  KVM_ARCH_FP_D_F3(a0)
	fld f4,  KVM_ARCH_FP_D_F4(a0)
	fld f5,  KVM_ARCH_FP_D_F5(a0)
	fld f6,  KVM_ARCH_FP_D_F6(a0)
	fld f7,  KVM_ARCH_FP_D_F7(a0)
	fld f8,  KVM_ARCH_FP_D_F8(a0)
	fld f9,  KVM_ARCH_FP_D_F9(a0)
	fld f10, KVM_ARCH_FP_D_F10(a0)
	fld f11, KVM_ARCH_FP_D_F11(a0)
	fld f12, KVM_ARCH_FP_D_F12(a0)
	fld f13, KVM_ARCH_FP_D_F13(a0)
	fld f14, KVM_ARCH_FP_D_F14(a0)
	fld f15, KVM_ARCH_FP_D_F15(a0)
	fld f16, KVM_ARCH_FP_D_F16(a0)
	fld f17, KVM_ARCH_FP_D_F17(a0)
	fld f18, KVM_ARCH_FP_D_F18(a0)
	fld f19, KVM_ARCH_FP_D_F19(a0)
	fld f20, KVM_ARCH_FP_D_F20(a0)
	fld f21, KVM_ARCH_FP_D_F21(a0)
	fld f22, KVM_ARCH_FP_D_F22(a0)
	fld f23, KVM_ARCH_FP_D_F23(a0)
	fld f24, KVM_ARCH_FP_D_F24(a0)
	fld f25, KVM_ARCH_FP_D_F25(a0)
	fld f26, KVM_ARCH_FP_D_F26(a0)
	fld f27, KVM_ARCH_FP_D_F27(a0)
	fld f28, KVM_ARCH_FP_D_F28(a0)
	fld f29, KVM_ARCH_FP_D_F29(a0)
	fld f30, KVM_ARCH_FP_D_F30(a0)
	fld f31, KVM_ARCH_FP_D_F31(a0)
	fscsr t0
	csrw CSR_SSTATUS, t2
	ret
SYM_FUNC_END(__kvm_riscv_fp_d_restore)
#endif
