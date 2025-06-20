// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/bitops.h>
#include <linux/kvm_host.h>

#include <asm/cpufeature.h>
#include <asm/insn.h>

struct insn_func {
	unsigned long mask;
	unsigned long match;
	/*
	 * Possible return values are as follows:
	 * 1) Returns < 0 for error case
	 * 2) Returns 0 for exit to user-space
	 * 3) Returns 1 to continue with next sepc
	 * 4) Returns 2 to continue with same sepc
	 * 5) Returns 3 to inject illegal instruction trap and continue
	 * 6) Returns 4 to inject virtual instruction trap and continue
	 *
	 * Use enum kvm_insn_return for return values
	 */
	int (*func)(struct kvm_vcpu *vcpu, struct kvm_run *run, ulong insn);
};

static int truly_illegal_insn(struct kvm_vcpu *vcpu, struct kvm_run *run,
			      ulong insn)
{
	struct kvm_cpu_trap utrap = { 0 };

	/* Redirect trap to Guest VCPU */
	utrap.sepc = vcpu->arch.guest_context.sepc;
	utrap.scause = EXC_INST_ILLEGAL;
	utrap.stval = insn;
	utrap.htval = 0;
	utrap.htinst = 0;
	kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);

	return 1;
}

static int truly_virtual_insn(struct kvm_vcpu *vcpu, struct kvm_run *run,
			      ulong insn)
{
	struct kvm_cpu_trap utrap = { 0 };

	/* Redirect trap to Guest VCPU */
	utrap.sepc = vcpu->arch.guest_context.sepc;
	utrap.scause = EXC_VIRTUAL_INST_FAULT;
	utrap.stval = insn;
	utrap.htval = 0;
	utrap.htinst = 0;
	kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);

	return 1;
}

/**
 * kvm_riscv_vcpu_wfi -- Emulate wait for interrupt (WFI) behaviour
 *
 * @vcpu: The VCPU pointer
 */
void kvm_riscv_vcpu_wfi(struct kvm_vcpu *vcpu)
{
	if (!kvm_arch_vcpu_runnable(vcpu)) {
		kvm_vcpu_srcu_read_unlock(vcpu);
		kvm_vcpu_halt(vcpu);
		kvm_vcpu_srcu_read_lock(vcpu);
	}
}

static int wfi_insn(struct kvm_vcpu *vcpu, struct kvm_run *run, ulong insn)
{
	vcpu->stat.wfi_exit_stat++;
	kvm_riscv_vcpu_wfi(vcpu);
	return KVM_INSN_CONTINUE_NEXT_SEPC;
}

static int wrs_insn(struct kvm_vcpu *vcpu, struct kvm_run *run, ulong insn)
{
	vcpu->stat.wrs_exit_stat++;
	kvm_vcpu_on_spin(vcpu, vcpu->arch.guest_context.sstatus & SR_SPP);
	return KVM_INSN_CONTINUE_NEXT_SEPC;
}

struct csr_func {
	unsigned int base;
	unsigned int count;
	/*
	 * Possible return values are as same as "func" callback in
	 * "struct insn_func".
	 */
	int (*func)(struct kvm_vcpu *vcpu, unsigned int csr_num,
		    unsigned long *val, unsigned long new_val,
		    unsigned long wr_mask);
};

static int seed_csr_rmw(struct kvm_vcpu *vcpu, unsigned int csr_num,
			unsigned long *val, unsigned long new_val,
			unsigned long wr_mask)
{
	if (!riscv_isa_extension_available(vcpu->arch.isa, ZKR))
		return KVM_INSN_ILLEGAL_TRAP;

	return KVM_INSN_EXIT_TO_USER_SPACE;
}

static const struct csr_func csr_funcs[] = {
	KVM_RISCV_VCPU_AIA_CSR_FUNCS
	KVM_RISCV_VCPU_HPMCOUNTER_CSR_FUNCS
	{ .base = CSR_SEED, .count = 1, .func = seed_csr_rmw },
};

/**
 * kvm_riscv_vcpu_csr_return -- Handle CSR read/write after user space
 *				emulation or in-kernel emulation
 *
 * @vcpu: The VCPU pointer
 * @run:  The VCPU run struct containing the CSR data
 *
 * Returns > 0 upon failure and 0 upon success
 */
int kvm_riscv_vcpu_csr_return(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	ulong insn;

	if (vcpu->arch.csr_decode.return_handled)
		return 0;
	vcpu->arch.csr_decode.return_handled = 1;

	/* Update destination register for CSR reads */
	insn = vcpu->arch.csr_decode.insn;
	if ((insn >> SH_RD) & MASK_RX)
		SET_RD(insn, &vcpu->arch.guest_context,
		       run->riscv_csr.ret_value);

	/* Move to next instruction */
	vcpu->arch.guest_context.sepc += INSN_LEN(insn);

	return 0;
}

static int csr_insn(struct kvm_vcpu *vcpu, struct kvm_run *run, ulong insn)
{
	int i, rc = KVM_INSN_ILLEGAL_TRAP;
	unsigned int csr_num = insn >> SH_RS2;
	unsigned int rs1_num = (insn >> SH_RS1) & MASK_RX;
	ulong rs1_val = GET_RS1(insn, &vcpu->arch.guest_context);
	const struct csr_func *tcfn, *cfn = NULL;
	ulong val = 0, wr_mask = 0, new_val = 0;

	/* Decode the CSR instruction */
	switch (GET_FUNCT3(insn)) {
	case GET_FUNCT3(INSN_MATCH_CSRRW):
		wr_mask = -1UL;
		new_val = rs1_val;
		break;
	case GET_FUNCT3(INSN_MATCH_CSRRS):
		wr_mask = rs1_val;
		new_val = -1UL;
		break;
	case GET_FUNCT3(INSN_MATCH_CSRRC):
		wr_mask = rs1_val;
		new_val = 0;
		break;
	case GET_FUNCT3(INSN_MATCH_CSRRWI):
		wr_mask = -1UL;
		new_val = rs1_num;
		break;
	case GET_FUNCT3(INSN_MATCH_CSRRSI):
		wr_mask = rs1_num;
		new_val = -1UL;
		break;
	case GET_FUNCT3(INSN_MATCH_CSRRCI):
		wr_mask = rs1_num;
		new_val = 0;
		break;
	default:
		return rc;
	}

	/* Save instruction decode info */
	vcpu->arch.csr_decode.insn = insn;
	vcpu->arch.csr_decode.return_handled = 0;

	/* Update CSR details in kvm_run struct */
	run->riscv_csr.csr_num = csr_num;
	run->riscv_csr.new_value = new_val;
	run->riscv_csr.write_mask = wr_mask;
	run->riscv_csr.ret_value = 0;

	/* Find in-kernel CSR function */
	for (i = 0; i < ARRAY_SIZE(csr_funcs); i++) {
		tcfn = &csr_funcs[i];
		if ((tcfn->base <= csr_num) &&
		    (csr_num < (tcfn->base + tcfn->count))) {
			cfn = tcfn;
			break;
		}
	}

	/* First try in-kernel CSR emulation */
	if (cfn && cfn->func) {
		rc = cfn->func(vcpu, csr_num, &val, new_val, wr_mask);
		if (rc > KVM_INSN_EXIT_TO_USER_SPACE) {
			if (rc == KVM_INSN_CONTINUE_NEXT_SEPC) {
				run->riscv_csr.ret_value = val;
				vcpu->stat.csr_exit_kernel++;
				kvm_riscv_vcpu_csr_return(vcpu, run);
				rc = KVM_INSN_CONTINUE_SAME_SEPC;
			}
			return rc;
		}
	}

	/* Exit to user-space for CSR emulation */
	if (rc <= KVM_INSN_EXIT_TO_USER_SPACE) {
		vcpu->stat.csr_exit_user++;
		run->exit_reason = KVM_EXIT_RISCV_CSR;
	}

	return rc;
}

static const struct insn_func system_opcode_funcs[] = {
	{
		.mask  = INSN_MASK_CSRRW,
		.match = INSN_MATCH_CSRRW,
		.func  = csr_insn,
	},
	{
		.mask  = INSN_MASK_CSRRS,
		.match = INSN_MATCH_CSRRS,
		.func  = csr_insn,
	},
	{
		.mask  = INSN_MASK_CSRRC,
		.match = INSN_MATCH_CSRRC,
		.func  = csr_insn,
	},
	{
		.mask  = INSN_MASK_CSRRWI,
		.match = INSN_MATCH_CSRRWI,
		.func  = csr_insn,
	},
	{
		.mask  = INSN_MASK_CSRRSI,
		.match = INSN_MATCH_CSRRSI,
		.func  = csr_insn,
	},
	{
		.mask  = INSN_MASK_CSRRCI,
		.match = INSN_MATCH_CSRRCI,
		.func  = csr_insn,
	},
	{
		.mask  = INSN_MASK_WFI,
		.match = INSN_MATCH_WFI,
		.func  = wfi_insn,
	},
	{
		.mask  = INSN_MASK_WRS,
		.match = INSN_MATCH_WRS,
		.func  = wrs_insn,
	},
};

static int system_opcode_insn(struct kvm_vcpu *vcpu, struct kvm_run *run,
			      ulong insn)
{
	int i, rc = KVM_INSN_ILLEGAL_TRAP;
	const struct insn_func *ifn;

	for (i = 0; i < ARRAY_SIZE(system_opcode_funcs); i++) {
		ifn = &system_opcode_funcs[i];
		if ((insn & ifn->mask) == ifn->match) {
			rc = ifn->func(vcpu, run, insn);
			break;
		}
	}

	switch (rc) {
	case KVM_INSN_ILLEGAL_TRAP:
		return truly_illegal_insn(vcpu, run, insn);
	case KVM_INSN_VIRTUAL_TRAP:
		return truly_virtual_insn(vcpu, run, insn);
	case KVM_INSN_CONTINUE_NEXT_SEPC:
		vcpu->arch.guest_context.sepc += INSN_LEN(insn);
		break;
	default:
		break;
	}

	return (rc <= 0) ? rc : 1;
}

/**
 * kvm_riscv_vcpu_virtual_insn -- Handle virtual instruction trap
 *
 * @vcpu: The VCPU pointer
 * @run:  The VCPU run struct containing the mmio data
 * @trap: Trap details
 *
 * Returns > 0 to continue run-loop
 * Returns   0 to exit run-loop and handle in user-space.
 * Returns < 0 to report failure and exit run-loop
 */
int kvm_riscv_vcpu_virtual_insn(struct kvm_vcpu *vcpu, struct kvm_run *run,
				struct kvm_cpu_trap *trap)
{
	unsigned long insn = trap->stval;
	struct kvm_cpu_trap utrap = { 0 };
	struct kvm_cpu_context *ct;

	if (unlikely(INSN_IS_16BIT(insn))) {
		if (insn == 0) {
			ct = &vcpu->arch.guest_context;
			insn = kvm_riscv_vcpu_unpriv_read(vcpu, true,
							  ct->sepc,
							  &utrap);
			if (utrap.scause) {
				utrap.sepc = ct->sepc;
				kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);
				return 1;
			}
		}
		if (INSN_IS_16BIT(insn))
			return truly_illegal_insn(vcpu, run, insn);
	}

	switch ((insn & INSN_OPCODE_MASK) >> INSN_OPCODE_SHIFT) {
	case INSN_OPCODE_SYSTEM:
		return system_opcode_insn(vcpu, run, insn);
	default:
		return truly_illegal_insn(vcpu, run, insn);
	}
}

/**
 * kvm_riscv_vcpu_mmio_load -- Emulate MMIO load instruction
 *
 * @vcpu: The VCPU pointer
 * @run:  The VCPU run struct containing the mmio data
 * @fault_addr: Guest physical address to load
 * @htinst: Transformed encoding of the load instruction
 *
 * Returns > 0 to continue run-loop
 * Returns   0 to exit run-loop and handle in user-space.
 * Returns < 0 to report failure and exit run-loop
 */
int kvm_riscv_vcpu_mmio_load(struct kvm_vcpu *vcpu, struct kvm_run *run,
			     unsigned long fault_addr,
			     unsigned long htinst)
{
	u8 data_buf[8];
	unsigned long insn;
	int shift = 0, len = 0, insn_len = 0;
	struct kvm_cpu_trap utrap = { 0 };
	struct kvm_cpu_context *ct = &vcpu->arch.guest_context;

	/* Determine trapped instruction */
	if (htinst & 0x1) {
		/*
		 * Bit[0] == 1 implies trapped instruction value is
		 * transformed instruction or custom instruction.
		 */
		insn = htinst | INSN_16BIT_MASK;
		insn_len = (htinst & BIT(1)) ? INSN_LEN(insn) : 2;
	} else {
		/*
		 * Bit[0] == 0 implies trapped instruction value is
		 * zero or special value.
		 */
		insn = kvm_riscv_vcpu_unpriv_read(vcpu, true, ct->sepc,
						  &utrap);
		if (utrap.scause) {
			/* Redirect trap if we failed to read instruction */
			utrap.sepc = ct->sepc;
			kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);
			return 1;
		}
		insn_len = INSN_LEN(insn);
	}

	/* Decode length of MMIO and shift */
	if ((insn & INSN_MASK_LW) == INSN_MATCH_LW) {
		len = 4;
		shift = 8 * (sizeof(ulong) - len);
	} else if ((insn & INSN_MASK_LB) == INSN_MATCH_LB) {
		len = 1;
		shift = 8 * (sizeof(ulong) - len);
	} else if ((insn & INSN_MASK_LBU) == INSN_MATCH_LBU) {
		len = 1;
		shift = 8 * (sizeof(ulong) - len);
#ifdef CONFIG_64BIT
	} else if ((insn & INSN_MASK_LD) == INSN_MATCH_LD) {
		len = 8;
		shift = 8 * (sizeof(ulong) - len);
	} else if ((insn & INSN_MASK_LWU) == INSN_MATCH_LWU) {
		len = 4;
#endif
	} else if ((insn & INSN_MASK_LH) == INSN_MATCH_LH) {
		len = 2;
		shift = 8 * (sizeof(ulong) - len);
	} else if ((insn & INSN_MASK_LHU) == INSN_MATCH_LHU) {
		len = 2;
#ifdef CONFIG_64BIT
	} else if ((insn & INSN_MASK_C_LD) == INSN_MATCH_C_LD) {
		len = 8;
		shift = 8 * (sizeof(ulong) - len);
		insn = RVC_RS2S(insn) << SH_RD;
	} else if ((insn & INSN_MASK_C_LDSP) == INSN_MATCH_C_LDSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		len = 8;
		shift = 8 * (sizeof(ulong) - len);
#endif
	} else if ((insn & INSN_MASK_C_LW) == INSN_MATCH_C_LW) {
		len = 4;
		shift = 8 * (sizeof(ulong) - len);
		insn = RVC_RS2S(insn) << SH_RD;
	} else if ((insn & INSN_MASK_C_LWSP) == INSN_MATCH_C_LWSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		len = 4;
		shift = 8 * (sizeof(ulong) - len);
	} else {
		return -EOPNOTSUPP;
	}

	/* Fault address should be aligned to length of MMIO */
	if (fault_addr & (len - 1))
		return -EIO;

	/* Save instruction decode info */
	vcpu->arch.mmio_decode.insn = insn;
	vcpu->arch.mmio_decode.insn_len = insn_len;
	vcpu->arch.mmio_decode.shift = shift;
	vcpu->arch.mmio_decode.len = len;
	vcpu->arch.mmio_decode.return_handled = 0;

	/* Update MMIO details in kvm_run struct */
	run->mmio.is_write = false;
	run->mmio.phys_addr = fault_addr;
	run->mmio.len = len;

	/* Try to handle MMIO access in the kernel */
	if (!kvm_io_bus_read(vcpu, KVM_MMIO_BUS, fault_addr, len, data_buf)) {
		/* Successfully handled MMIO access in the kernel so resume */
		memcpy(run->mmio.data, data_buf, len);
		vcpu->stat.mmio_exit_kernel++;
		kvm_riscv_vcpu_mmio_return(vcpu, run);
		return 1;
	}

	/* Exit to userspace for MMIO emulation */
	vcpu->stat.mmio_exit_user++;
	run->exit_reason = KVM_EXIT_MMIO;

	return 0;
}

/**
 * kvm_riscv_vcpu_mmio_store -- Emulate MMIO store instruction
 *
 * @vcpu: The VCPU pointer
 * @run:  The VCPU run struct containing the mmio data
 * @fault_addr: Guest physical address to store
 * @htinst: Transformed encoding of the store instruction
 *
 * Returns > 0 to continue run-loop
 * Returns   0 to exit run-loop and handle in user-space.
 * Returns < 0 to report failure and exit run-loop
 */
int kvm_riscv_vcpu_mmio_store(struct kvm_vcpu *vcpu, struct kvm_run *run,
			      unsigned long fault_addr,
			      unsigned long htinst)
{
	u8 data8;
	u16 data16;
	u32 data32;
	u64 data64;
	ulong data;
	unsigned long insn;
	int len = 0, insn_len = 0;
	struct kvm_cpu_trap utrap = { 0 };
	struct kvm_cpu_context *ct = &vcpu->arch.guest_context;

	/* Determine trapped instruction */
	if (htinst & 0x1) {
		/*
		 * Bit[0] == 1 implies trapped instruction value is
		 * transformed instruction or custom instruction.
		 */
		insn = htinst | INSN_16BIT_MASK;
		insn_len = (htinst & BIT(1)) ? INSN_LEN(insn) : 2;
	} else {
		/*
		 * Bit[0] == 0 implies trapped instruction value is
		 * zero or special value.
		 */
		insn = kvm_riscv_vcpu_unpriv_read(vcpu, true, ct->sepc,
						  &utrap);
		if (utrap.scause) {
			/* Redirect trap if we failed to read instruction */
			utrap.sepc = ct->sepc;
			kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);
			return 1;
		}
		insn_len = INSN_LEN(insn);
	}

	data = GET_RS2(insn, &vcpu->arch.guest_context);
	data8 = data16 = data32 = data64 = data;

	if ((insn & INSN_MASK_SW) == INSN_MATCH_SW) {
		len = 4;
	} else if ((insn & INSN_MASK_SB) == INSN_MATCH_SB) {
		len = 1;
#ifdef CONFIG_64BIT
	} else if ((insn & INSN_MASK_SD) == INSN_MATCH_SD) {
		len = 8;
#endif
	} else if ((insn & INSN_MASK_SH) == INSN_MATCH_SH) {
		len = 2;
#ifdef CONFIG_64BIT
	} else if ((insn & INSN_MASK_C_SD) == INSN_MATCH_C_SD) {
		len = 8;
		data64 = GET_RS2S(insn, &vcpu->arch.guest_context);
	} else if ((insn & INSN_MASK_C_SDSP) == INSN_MATCH_C_SDSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		len = 8;
		data64 = GET_RS2C(insn, &vcpu->arch.guest_context);
#endif
	} else if ((insn & INSN_MASK_C_SW) == INSN_MATCH_C_SW) {
		len = 4;
		data32 = GET_RS2S(insn, &vcpu->arch.guest_context);
	} else if ((insn & INSN_MASK_C_SWSP) == INSN_MATCH_C_SWSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		len = 4;
		data32 = GET_RS2C(insn, &vcpu->arch.guest_context);
	} else {
		return -EOPNOTSUPP;
	}

	/* Fault address should be aligned to length of MMIO */
	if (fault_addr & (len - 1))
		return -EIO;

	/* Save instruction decode info */
	vcpu->arch.mmio_decode.insn = insn;
	vcpu->arch.mmio_decode.insn_len = insn_len;
	vcpu->arch.mmio_decode.shift = 0;
	vcpu->arch.mmio_decode.len = len;
	vcpu->arch.mmio_decode.return_handled = 0;

	/* Copy data to kvm_run instance */
	switch (len) {
	case 1:
		*((u8 *)run->mmio.data) = data8;
		break;
	case 2:
		*((u16 *)run->mmio.data) = data16;
		break;
	case 4:
		*((u32 *)run->mmio.data) = data32;
		break;
	case 8:
		*((u64 *)run->mmio.data) = data64;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Update MMIO details in kvm_run struct */
	run->mmio.is_write = true;
	run->mmio.phys_addr = fault_addr;
	run->mmio.len = len;

	/* Try to handle MMIO access in the kernel */
	if (!kvm_io_bus_write(vcpu, KVM_MMIO_BUS,
			      fault_addr, len, run->mmio.data)) {
		/* Successfully handled MMIO access in the kernel so resume */
		vcpu->stat.mmio_exit_kernel++;
		kvm_riscv_vcpu_mmio_return(vcpu, run);
		return 1;
	}

	/* Exit to userspace for MMIO emulation */
	vcpu->stat.mmio_exit_user++;
	run->exit_reason = KVM_EXIT_MMIO;

	return 0;
}

/**
 * kvm_riscv_vcpu_mmio_return -- Handle MMIO loads after user space emulation
 *			     or in-kernel IO emulation
 *
 * @vcpu: The VCPU pointer
 * @run:  The VCPU run struct containing the mmio data
 */
int kvm_riscv_vcpu_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	u8 data8;
	u16 data16;
	u32 data32;
	u64 data64;
	ulong insn;
	int len, shift;

	if (vcpu->arch.mmio_decode.return_handled)
		return 0;

	vcpu->arch.mmio_decode.return_handled = 1;
	insn = vcpu->arch.mmio_decode.insn;

	if (run->mmio.is_write)
		goto done;

	len = vcpu->arch.mmio_decode.len;
	shift = vcpu->arch.mmio_decode.shift;

	switch (len) {
	case 1:
		data8 = *((u8 *)run->mmio.data);
		SET_RD(insn, &vcpu->arch.guest_context,
			(ulong)data8 << shift >> shift);
		break;
	case 2:
		data16 = *((u16 *)run->mmio.data);
		SET_RD(insn, &vcpu->arch.guest_context,
			(ulong)data16 << shift >> shift);
		break;
	case 4:
		data32 = *((u32 *)run->mmio.data);
		SET_RD(insn, &vcpu->arch.guest_context,
			(ulong)data32 << shift >> shift);
		break;
	case 8:
		data64 = *((u64 *)run->mmio.data);
		SET_RD(insn, &vcpu->arch.guest_context,
			(ulong)data64 << shift >> shift);
		break;
	default:
		return -EOPNOTSUPP;
	}

done:
	/* Move to next instruction */
	vcpu->arch.guest_context.sepc += vcpu->arch.mmio_decode.insn_len;

	return 0;
}
