/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/linkage.h>
#include <asm/asm.h>

	.text
	.altmacro
	.option norelax

	/*
	 * Instruction encoding of hfence.gvma is:
	 * HFENCE.GVMA rs1, rs2
	 * HFENCE.GVMA zero, rs2
	 * HFENCE.GVMA rs1
	 * HFENCE.GVMA
	 *
	 * rs1!=zero and rs2!=zero ==> HFENCE.GVMA rs1, rs2
	 * rs1==zero and rs2!=zero ==> HFENCE.GVMA zero, rs2
	 * rs1!=zero and rs2==zero ==> HFENCE.GVMA rs1
	 * rs1==zero and rs2==zero ==> HFENCE.GVMA
	 *
	 * Instruction encoding of HFENCE.GVMA is:
	 * 0110001 rs2(5) rs1(5) 000 00000 1110011
	 */

ENTRY(__kvm_riscv_hfence_gvma_vmid_gpa)
	/*
	 * rs1 = a0 (GPA >> 2)
	 * rs2 = a1 (VMID)
	 * HFENCE.GVMA a0, a1
	 * 0110001 01011 01010 000 00000 1110011
	 */
	.word 0x62b50073
	ret
ENDPROC(__kvm_riscv_hfence_gvma_vmid_gpa)

ENTRY(__kvm_riscv_hfence_gvma_vmid)
	/*
	 * rs1 = zero
	 * rs2 = a0 (VMID)
	 * HFENCE.GVMA zero, a0
	 * 0110001 01010 00000 000 00000 1110011
	 */
	.word 0x62a00073
	ret
ENDPROC(__kvm_riscv_hfence_gvma_vmid)

ENTRY(__kvm_riscv_hfence_gvma_gpa)
	/*
	 * rs1 = a0 (GPA >> 2)
	 * rs2 = zero
	 * HFENCE.GVMA a0
	 * 0110001 00000 01010 000 00000 1110011
	 */
	.word 0x62050073
	ret
ENDPROC(__kvm_riscv_hfence_gvma_gpa)

ENTRY(__kvm_riscv_hfence_gvma_all)
	/*
	 * rs1 = zero
	 * rs2 = zero
	 * HFENCE.GVMA
	 * 0110001 00000 00000 000 00000 1110011
	 */
	.word 0x62000073
	ret
ENDPROC(__kvm_riscv_hfence_gvma_all)
