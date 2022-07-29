// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/bitops.h>
#include <linux/kvm_host.h>

#define INSN_OPCODE_MASK	0x007c
#define INSN_OPCODE_SHIFT	2
#define INSN_OPCODE_SYSTEM	28

#define INSN_MASK_WFI		0xffffffff
#define INSN_MATCH_WFI		0x10500073

#define INSN_MATCH_LB		0x3
#define INSN_MASK_LB		0x707f
#define INSN_MATCH_LH		0x1003
#define INSN_MASK_LH		0x707f
#define INSN_MATCH_LW		0x2003
#define INSN_MASK_LW		0x707f
#define INSN_MATCH_LD		0x3003
#define INSN_MASK_LD		0x707f
#define INSN_MATCH_LBU		0x4003
#define INSN_MASK_LBU		0x707f
#define INSN_MATCH_LHU		0x5003
#define INSN_MASK_LHU		0x707f
#define INSN_MATCH_LWU		0x6003
#define INSN_MASK_LWU		0x707f
#define INSN_MATCH_SB		0x23
#define INSN_MASK_SB		0x707f
#define INSN_MATCH_SH		0x1023
#define INSN_MASK_SH		0x707f
#define INSN_MATCH_SW		0x2023
#define INSN_MASK_SW		0x707f
#define INSN_MATCH_SD		0x3023
#define INSN_MASK_SD		0x707f

#define INSN_MATCH_C_LD		0x6000
#define INSN_MASK_C_LD		0xe003
#define INSN_MATCH_C_SD		0xe000
#define INSN_MASK_C_SD		0xe003
#define INSN_MATCH_C_LW		0x4000
#define INSN_MASK_C_LW		0xe003
#define INSN_MATCH_C_SW		0xc000
#define INSN_MASK_C_SW		0xe003
#define INSN_MATCH_C_LDSP	0x6002
#define INSN_MASK_C_LDSP	0xe003
#define INSN_MATCH_C_SDSP	0xe002
#define INSN_MASK_C_SDSP	0xe003
#define INSN_MATCH_C_LWSP	0x4002
#define INSN_MASK_C_LWSP	0xe003
#define INSN_MATCH_C_SWSP	0xc002
#define INSN_MASK_C_SWSP	0xe003

#define INSN_16BIT_MASK		0x3

#define INSN_IS_16BIT(insn)	(((insn) & INSN_16BIT_MASK) != INSN_16BIT_MASK)

#define INSN_LEN(insn)		(INSN_IS_16BIT(insn) ? 2 : 4)

#ifdef CONFIG_64BIT
#define LOG_REGBYTES		3
#else
#define LOG_REGBYTES		2
#endif
#define REGBYTES		(1 << LOG_REGBYTES)

#define SH_RD			7
#define SH_RS1			15
#define SH_RS2			20
#define SH_RS2C			2

#define RV_X(x, s, n)		(((x) >> (s)) & ((1 << (n)) - 1))
#define RVC_LW_IMM(x)		((RV_X(x, 6, 1) << 2) | \
				 (RV_X(x, 10, 3) << 3) | \
				 (RV_X(x, 5, 1) << 6))
#define RVC_LD_IMM(x)		((RV_X(x, 10, 3) << 3) | \
				 (RV_X(x, 5, 2) << 6))
#define RVC_LWSP_IMM(x)		((RV_X(x, 4, 3) << 2) | \
				 (RV_X(x, 12, 1) << 5) | \
				 (RV_X(x, 2, 2) << 6))
#define RVC_LDSP_IMM(x)		((RV_X(x, 5, 2) << 3) | \
				 (RV_X(x, 12, 1) << 5) | \
				 (RV_X(x, 2, 3) << 6))
#define RVC_SWSP_IMM(x)		((RV_X(x, 9, 4) << 2) | \
				 (RV_X(x, 7, 2) << 6))
#define RVC_SDSP_IMM(x)		((RV_X(x, 10, 3) << 3) | \
				 (RV_X(x, 7, 3) << 6))
#define RVC_RS1S(insn)		(8 + RV_X(insn, SH_RD, 3))
#define RVC_RS2S(insn)		(8 + RV_X(insn, SH_RS2C, 3))
#define RVC_RS2(insn)		RV_X(insn, SH_RS2C, 5)

#define SHIFT_RIGHT(x, y)		\
	((y) < 0 ? ((x) << -(y)) : ((x) >> (y)))

#define REG_MASK			\
	((1 << (5 + LOG_REGBYTES)) - (1 << LOG_REGBYTES))

#define REG_OFFSET(insn, pos)		\
	(SHIFT_RIGHT((insn), (pos) - LOG_REGBYTES) & REG_MASK)

#define REG_PTR(insn, pos, regs)	\
	((ulong *)((ulong)(regs) + REG_OFFSET(insn, pos)))

#define GET_RM(insn)		(((insn) >> 12) & 7)

#define GET_RS1(insn, regs)	(*REG_PTR(insn, SH_RS1, regs))
#define GET_RS2(insn, regs)	(*REG_PTR(insn, SH_RS2, regs))
#define GET_RS1S(insn, regs)	(*REG_PTR(RVC_RS1S(insn), 0, regs))
#define GET_RS2S(insn, regs)	(*REG_PTR(RVC_RS2S(insn), 0, regs))
#define GET_RS2C(insn, regs)	(*REG_PTR(insn, SH_RS2C, regs))
#define GET_SP(regs)		(*REG_PTR(2, 0, regs))
#define SET_RD(insn, regs, val)	(*REG_PTR(insn, SH_RD, regs) = (val))
#define IMM_I(insn)		((s32)(insn) >> 20)
#define IMM_S(insn)		(((s32)(insn) >> 25 << 5) | \
				 (s32)(((insn) >> 7) & 0x1f))
#define MASK_FUNCT3		0x7000

static int truly_illegal_insn(struct kvm_vcpu *vcpu,
			      struct kvm_run *run,
			      ulong insn)
{
	struct kvm_cpu_trap utrap = { 0 };

	/* Redirect trap to Guest VCPU */
	utrap.sepc = vcpu->arch.guest_context.sepc;
	utrap.scause = EXC_INST_ILLEGAL;
	utrap.stval = insn;
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
		kvm_clear_request(KVM_REQ_UNHALT, vcpu);
	}
}

static int system_opcode_insn(struct kvm_vcpu *vcpu,
			      struct kvm_run *run,
			      ulong insn)
{
	if ((insn & INSN_MASK_WFI) == INSN_MATCH_WFI) {
		vcpu->stat.wfi_exit_stat++;
		kvm_riscv_vcpu_wfi(vcpu);
		vcpu->arch.guest_context.sepc += INSN_LEN(insn);
		return 1;
	}

	return truly_illegal_insn(vcpu, run, insn);
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
