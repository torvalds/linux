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

ENTRY(__kvm_riscv_switch_to)
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

	/* Save Host and Restore Guest SSTATUS */
	REG_L	t0, (KVM_ARCH_GUEST_SSTATUS)(a0)
	csrrw	t0, CSR_SSTATUS, t0
	REG_S	t0, (KVM_ARCH_HOST_SSTATUS)(a0)

	/* Save Host and Restore Guest HSTATUS */
	REG_L	t1, (KVM_ARCH_GUEST_HSTATUS)(a0)
	csrrw	t1, CSR_HSTATUS, t1
	REG_S	t1, (KVM_ARCH_HOST_HSTATUS)(a0)

	/* Save Host and Restore Guest SCOUNTEREN */
	REG_L	t2, (KVM_ARCH_GUEST_SCOUNTEREN)(a0)
	csrrw	t2, CSR_SCOUNTEREN, t2
	REG_S	t2, (KVM_ARCH_HOST_SCOUNTEREN)(a0)

	/* Save Host SSCRATCH and change it to struct kvm_vcpu_arch pointer */
	csrrw	t3, CSR_SSCRATCH, a0
	REG_S	t3, (KVM_ARCH_HOST_SSCRATCH)(a0)

	/* Save Host STVEC and change it to return path */
	la	t4, __kvm_switch_return
	csrrw	t4, CSR_STVEC, t4
	REG_S	t4, (KVM_ARCH_HOST_STVEC)(a0)

	/* Restore Guest SEPC */
	REG_L	t0, (KVM_ARCH_GUEST_SEPC)(a0)
	csrw	CSR_SEPC, t0

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
__kvm_switch_return:
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

	/* Save Guest SEPC */
	csrr	t0, CSR_SEPC
	REG_S	t0, (KVM_ARCH_GUEST_SEPC)(a0)

	/* Restore Host STVEC */
	REG_L	t1, (KVM_ARCH_HOST_STVEC)(a0)
	csrw	CSR_STVEC, t1

	/* Save Guest A0 and Restore Host SSCRATCH */
	REG_L	t2, (KVM_ARCH_HOST_SSCRATCH)(a0)
	csrrw	t2, CSR_SSCRATCH, t2
	REG_S	t2, (KVM_ARCH_GUEST_A0)(a0)

	/* Save Guest and Restore Host SCOUNTEREN */
	REG_L	t3, (KVM_ARCH_HOST_SCOUNTEREN)(a0)
	csrrw	t3, CSR_SCOUNTEREN, t3
	REG_S	t3, (KVM_ARCH_GUEST_SCOUNTEREN)(a0)

	/* Save Guest and Restore Host HSTATUS */
	REG_L	t4, (KVM_ARCH_HOST_HSTATUS)(a0)
	csrrw	t4, CSR_HSTATUS, t4
	REG_S	t4, (KVM_ARCH_GUEST_HSTATUS)(a0)

	/* Save Guest and Restore Host SSTATUS */
	REG_L	t5, (KVM_ARCH_HOST_SSTATUS)(a0)
	csrrw	t5, CSR_SSTATUS, t5
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
ENDPROC(__kvm_riscv_switch_to)
