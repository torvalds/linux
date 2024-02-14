// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/insn-def.h>

static int gstage_page_fault(struct kvm_vcpu *vcpu, struct kvm_run *run,
			     struct kvm_cpu_trap *trap)
{
	struct kvm_memory_slot *memslot;
	unsigned long hva, fault_addr;
	bool writable;
	gfn_t gfn;
	int ret;

	fault_addr = (trap->htval << 2) | (trap->stval & 0x3);
	gfn = fault_addr >> PAGE_SHIFT;
	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writable);

	if (kvm_is_error_hva(hva) ||
	    (trap->scause == EXC_STORE_GUEST_PAGE_FAULT && !writable)) {
		switch (trap->scause) {
		case EXC_LOAD_GUEST_PAGE_FAULT:
			return kvm_riscv_vcpu_mmio_load(vcpu, run,
							fault_addr,
							trap->htinst);
		case EXC_STORE_GUEST_PAGE_FAULT:
			return kvm_riscv_vcpu_mmio_store(vcpu, run,
							 fault_addr,
							 trap->htinst);
		default:
			return -EOPNOTSUPP;
		};
	}

	ret = kvm_riscv_gstage_map(vcpu, memslot, fault_addr, hva,
		(trap->scause == EXC_STORE_GUEST_PAGE_FAULT) ? true : false);
	if (ret < 0)
		return ret;

	return 1;
}

/**
 * kvm_riscv_vcpu_unpriv_read -- Read machine word from Guest memory
 *
 * @vcpu: The VCPU pointer
 * @read_insn: Flag representing whether we are reading instruction
 * @guest_addr: Guest address to read
 * @trap: Output pointer to trap details
 */
unsigned long kvm_riscv_vcpu_unpriv_read(struct kvm_vcpu *vcpu,
					 bool read_insn,
					 unsigned long guest_addr,
					 struct kvm_cpu_trap *trap)
{
	register unsigned long taddr asm("a0") = (unsigned long)trap;
	register unsigned long ttmp asm("a1");
	unsigned long flags, val, tmp, old_stvec, old_hstatus;

	local_irq_save(flags);

	old_hstatus = csr_swap(CSR_HSTATUS, vcpu->arch.guest_context.hstatus);
	old_stvec = csr_swap(CSR_STVEC, (ulong)&__kvm_riscv_unpriv_trap);

	if (read_insn) {
		/*
		 * HLVX.HU instruction
		 * 0110010 00011 rs1 100 rd 1110011
		 */
		asm volatile ("\n"
			".option push\n"
			".option norvc\n"
			"add %[ttmp], %[taddr], 0\n"
			HLVX_HU(%[val], %[addr])
			"andi %[tmp], %[val], 3\n"
			"addi %[tmp], %[tmp], -3\n"
			"bne %[tmp], zero, 2f\n"
			"addi %[addr], %[addr], 2\n"
			HLVX_HU(%[tmp], %[addr])
			"sll %[tmp], %[tmp], 16\n"
			"add %[val], %[val], %[tmp]\n"
			"2:\n"
			".option pop"
		: [val] "=&r" (val), [tmp] "=&r" (tmp),
		  [taddr] "+&r" (taddr), [ttmp] "+&r" (ttmp),
		  [addr] "+&r" (guest_addr) : : "memory");

		if (trap->scause == EXC_LOAD_PAGE_FAULT)
			trap->scause = EXC_INST_PAGE_FAULT;
	} else {
		/*
		 * HLV.D instruction
		 * 0110110 00000 rs1 100 rd 1110011
		 *
		 * HLV.W instruction
		 * 0110100 00000 rs1 100 rd 1110011
		 */
		asm volatile ("\n"
			".option push\n"
			".option norvc\n"
			"add %[ttmp], %[taddr], 0\n"
#ifdef CONFIG_64BIT
			HLV_D(%[val], %[addr])
#else
			HLV_W(%[val], %[addr])
#endif
			".option pop"
		: [val] "=&r" (val),
		  [taddr] "+&r" (taddr), [ttmp] "+&r" (ttmp)
		: [addr] "r" (guest_addr) : "memory");
	}

	csr_write(CSR_STVEC, old_stvec);
	csr_write(CSR_HSTATUS, old_hstatus);

	local_irq_restore(flags);

	return val;
}

/**
 * kvm_riscv_vcpu_trap_redirect -- Redirect trap to Guest
 *
 * @vcpu: The VCPU pointer
 * @trap: Trap details
 */
void kvm_riscv_vcpu_trap_redirect(struct kvm_vcpu *vcpu,
				  struct kvm_cpu_trap *trap)
{
	unsigned long vsstatus = csr_read(CSR_VSSTATUS);

	/* Change Guest SSTATUS.SPP bit */
	vsstatus &= ~SR_SPP;
	if (vcpu->arch.guest_context.sstatus & SR_SPP)
		vsstatus |= SR_SPP;

	/* Change Guest SSTATUS.SPIE bit */
	vsstatus &= ~SR_SPIE;
	if (vsstatus & SR_SIE)
		vsstatus |= SR_SPIE;

	/* Clear Guest SSTATUS.SIE bit */
	vsstatus &= ~SR_SIE;

	/* Update Guest SSTATUS */
	csr_write(CSR_VSSTATUS, vsstatus);

	/* Update Guest SCAUSE, STVAL, and SEPC */
	csr_write(CSR_VSCAUSE, trap->scause);
	csr_write(CSR_VSTVAL, trap->stval);
	csr_write(CSR_VSEPC, trap->sepc);

	/* Set Guest PC to Guest exception vector */
	vcpu->arch.guest_context.sepc = csr_read(CSR_VSTVEC);
}

/*
 * Return > 0 to return to guest, < 0 on error, 0 (and set exit_reason) on
 * proper exit to userspace.
 */
int kvm_riscv_vcpu_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
			struct kvm_cpu_trap *trap)
{
	int ret;

	/* If we got host interrupt then do nothing */
	if (trap->scause & CAUSE_IRQ_FLAG)
		return 1;

	/* Handle guest traps */
	ret = -EFAULT;
	run->exit_reason = KVM_EXIT_UNKNOWN;
	switch (trap->scause) {
	case EXC_VIRTUAL_INST_FAULT:
		if (vcpu->arch.guest_context.hstatus & HSTATUS_SPV)
			ret = kvm_riscv_vcpu_virtual_insn(vcpu, run, trap);
		break;
	case EXC_INST_GUEST_PAGE_FAULT:
	case EXC_LOAD_GUEST_PAGE_FAULT:
	case EXC_STORE_GUEST_PAGE_FAULT:
		if (vcpu->arch.guest_context.hstatus & HSTATUS_SPV)
			ret = gstage_page_fault(vcpu, run, trap);
		break;
	case EXC_SUPERVISOR_SYSCALL:
		if (vcpu->arch.guest_context.hstatus & HSTATUS_SPV)
			ret = kvm_riscv_vcpu_sbi_ecall(vcpu, run);
		break;
	default:
		break;
	}

	/* Print details in-case of error */
	if (ret < 0) {
		kvm_err("VCPU exit error %d\n", ret);
		kvm_err("SEPC=0x%lx SSTATUS=0x%lx HSTATUS=0x%lx\n",
			vcpu->arch.guest_context.sepc,
			vcpu->arch.guest_context.sstatus,
			vcpu->arch.guest_context.hstatus);
		kvm_err("SCAUSE=0x%lx STVAL=0x%lx HTVAL=0x%lx HTINST=0x%lx\n",
			trap->scause, trap->stval, trap->htval, trap->htinst);
	}

	return ret;
}
