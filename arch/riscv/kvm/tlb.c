// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>

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

void kvm_riscv_local_hfence_gvma_vmid_gpa(unsigned long vmid,
					  gpa_t gpa, gpa_t gpsz,
					  unsigned long order)
{
	gpa_t pos;

	if (PTRS_PER_PTE < (gpsz >> order)) {
		kvm_riscv_local_hfence_gvma_vmid_all(vmid);
		return;
	}

	for (pos = gpa; pos < (gpa + gpsz); pos += BIT(order)) {
		/*
		 * rs1 = a0 (GPA >> 2)
		 * rs2 = a1 (VMID)
		 * HFENCE.GVMA a0, a1
		 * 0110001 01011 01010 000 00000 1110011
		 */
		asm volatile ("srli a0, %0, 2\n"
			      "add a1, %1, zero\n"
			      ".word 0x62b50073\n"
			      :: "r" (pos), "r" (vmid)
			      : "a0", "a1", "memory");
	}
}

void kvm_riscv_local_hfence_gvma_vmid_all(unsigned long vmid)
{
	/*
	 * rs1 = zero
	 * rs2 = a0 (VMID)
	 * HFENCE.GVMA zero, a0
	 * 0110001 01010 00000 000 00000 1110011
	 */
	asm volatile ("add a0, %0, zero\n"
		      ".word 0x62a00073\n"
		      :: "r" (vmid) : "a0", "memory");
}

void kvm_riscv_local_hfence_gvma_gpa(gpa_t gpa, gpa_t gpsz,
				     unsigned long order)
{
	gpa_t pos;

	if (PTRS_PER_PTE < (gpsz >> order)) {
		kvm_riscv_local_hfence_gvma_all();
		return;
	}

	for (pos = gpa; pos < (gpa + gpsz); pos += BIT(order)) {
		/*
		 * rs1 = a0 (GPA >> 2)
		 * rs2 = zero
		 * HFENCE.GVMA a0
		 * 0110001 00000 01010 000 00000 1110011
		 */
		asm volatile ("srli a0, %0, 2\n"
			      ".word 0x62050073\n"
			      :: "r" (pos) : "a0", "memory");
	}
}

void kvm_riscv_local_hfence_gvma_all(void)
{
	/*
	 * rs1 = zero
	 * rs2 = zero
	 * HFENCE.GVMA
	 * 0110001 00000 00000 000 00000 1110011
	 */
	asm volatile (".word 0x62000073" ::: "memory");
}

/*
 * Instruction encoding of hfence.gvma is:
 * HFENCE.VVMA rs1, rs2
 * HFENCE.VVMA zero, rs2
 * HFENCE.VVMA rs1
 * HFENCE.VVMA
 *
 * rs1!=zero and rs2!=zero ==> HFENCE.VVMA rs1, rs2
 * rs1==zero and rs2!=zero ==> HFENCE.VVMA zero, rs2
 * rs1!=zero and rs2==zero ==> HFENCE.VVMA rs1
 * rs1==zero and rs2==zero ==> HFENCE.VVMA
 *
 * Instruction encoding of HFENCE.VVMA is:
 * 0010001 rs2(5) rs1(5) 000 00000 1110011
 */

void kvm_riscv_local_hfence_vvma_asid_gva(unsigned long vmid,
					  unsigned long asid,
					  unsigned long gva,
					  unsigned long gvsz,
					  unsigned long order)
{
	unsigned long pos, hgatp;

	if (PTRS_PER_PTE < (gvsz >> order)) {
		kvm_riscv_local_hfence_vvma_asid_all(vmid, asid);
		return;
	}

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	for (pos = gva; pos < (gva + gvsz); pos += BIT(order)) {
		/*
		 * rs1 = a0 (GVA)
		 * rs2 = a1 (ASID)
		 * HFENCE.VVMA a0, a1
		 * 0010001 01011 01010 000 00000 1110011
		 */
		asm volatile ("add a0, %0, zero\n"
			      "add a1, %1, zero\n"
			      ".word 0x22b50073\n"
			      :: "r" (pos), "r" (asid)
			      : "a0", "a1", "memory");
	}

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_hfence_vvma_asid_all(unsigned long vmid,
					  unsigned long asid)
{
	unsigned long hgatp;

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	/*
	 * rs1 = zero
	 * rs2 = a0 (ASID)
	 * HFENCE.VVMA zero, a0
	 * 0010001 01010 00000 000 00000 1110011
	 */
	asm volatile ("add a0, %0, zero\n"
		      ".word 0x22a00073\n"
		      :: "r" (asid) : "a0", "memory");

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_hfence_vvma_gva(unsigned long vmid,
				     unsigned long gva, unsigned long gvsz,
				     unsigned long order)
{
	unsigned long pos, hgatp;

	if (PTRS_PER_PTE < (gvsz >> order)) {
		kvm_riscv_local_hfence_vvma_all(vmid);
		return;
	}

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	for (pos = gva; pos < (gva + gvsz); pos += BIT(order)) {
		/*
		 * rs1 = a0 (GVA)
		 * rs2 = zero
		 * HFENCE.VVMA a0
		 * 0010001 00000 01010 000 00000 1110011
		 */
		asm volatile ("add a0, %0, zero\n"
			      ".word 0x22050073\n"
			      :: "r" (pos) : "a0", "memory");
	}

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_hfence_vvma_all(unsigned long vmid)
{
	unsigned long hgatp;

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	/*
	 * rs1 = zero
	 * rs2 = zero
	 * HFENCE.VVMA
	 * 0010001 00000 00000 000 00000 1110011
	 */
	asm volatile (".word 0x22000073" ::: "memory");

	csr_write(CSR_HGATP, hgatp);
}
