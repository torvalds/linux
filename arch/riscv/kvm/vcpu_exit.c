// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>

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

static int virtual_inst_fault(struct kvm_vcpu *vcpu, struct kvm_run *run,
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

static int emulate_load(struct kvm_vcpu *vcpu, struct kvm_run *run,
			unsigned long fault_addr, unsigned long htinst)
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

static int emulate_store(struct kvm_vcpu *vcpu, struct kvm_run *run,
			 unsigned long fault_addr, unsigned long htinst)
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

static int stage2_page_fault(struct kvm_vcpu *vcpu, struct kvm_run *run,
			     struct kvm_cpu_trap *trap)
{
	struct kvm_memory_slot *memslot;
	unsigned long hva, fault_addr;
	bool writeable;
	gfn_t gfn;
	int ret;

	fault_addr = (trap->htval << 2) | (trap->stval & 0x3);
	gfn = fault_addr >> PAGE_SHIFT;
	memslot = gfn_to_memslot(vcpu->kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writeable);

	if (kvm_is_error_hva(hva) ||
	    (trap->scause == EXC_STORE_GUEST_PAGE_FAULT && !writeable)) {
		switch (trap->scause) {
		case EXC_LOAD_GUEST_PAGE_FAULT:
			return emulate_load(vcpu, run, fault_addr,
					    trap->htinst);
		case EXC_STORE_GUEST_PAGE_FAULT:
			return emulate_store(vcpu, run, fault_addr,
					     trap->htinst);
		default:
			return -EOPNOTSUPP;
		};
	}

	ret = kvm_riscv_stage2_map(vcpu, memslot, fault_addr, hva,
		(trap->scause == EXC_STORE_GUEST_PAGE_FAULT) ? true : false);
	if (ret < 0)
		return ret;

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
		srcu_read_unlock(&vcpu->kvm->srcu, vcpu->arch.srcu_idx);
		kvm_vcpu_halt(vcpu);
		vcpu->arch.srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		kvm_clear_request(KVM_REQ_UNHALT, vcpu);
	}
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
	register unsigned long val asm("t0");
	register unsigned long tmp asm("t1");
	register unsigned long addr asm("t2") = guest_addr;
	unsigned long flags;
	unsigned long old_stvec, old_hstatus;

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
			/*
			 * HLVX.HU %[val], (%[addr])
			 * HLVX.HU t0, (t2)
			 * 0110010 00011 00111 100 00101 1110011
			 */
			".word 0x6433c2f3\n"
			"andi %[tmp], %[val], 3\n"
			"addi %[tmp], %[tmp], -3\n"
			"bne %[tmp], zero, 2f\n"
			"addi %[addr], %[addr], 2\n"
			/*
			 * HLVX.HU %[tmp], (%[addr])
			 * HLVX.HU t1, (t2)
			 * 0110010 00011 00111 100 00110 1110011
			 */
			".word 0x6433c373\n"
			"sll %[tmp], %[tmp], 16\n"
			"add %[val], %[val], %[tmp]\n"
			"2:\n"
			".option pop"
		: [val] "=&r" (val), [tmp] "=&r" (tmp),
		  [taddr] "+&r" (taddr), [ttmp] "+&r" (ttmp),
		  [addr] "+&r" (addr) : : "memory");

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
			/*
			 * HLV.D %[val], (%[addr])
			 * HLV.D t0, (t2)
			 * 0110110 00000 00111 100 00101 1110011
			 */
			".word 0x6c03c2f3\n"
#else
			/*
			 * HLV.W %[val], (%[addr])
			 * HLV.W t0, (t2)
			 * 0110100 00000 00111 100 00101 1110011
			 */
			".word 0x6803c2f3\n"
#endif
			".option pop"
		: [val] "=&r" (val),
		  [taddr] "+&r" (taddr), [ttmp] "+&r" (ttmp)
		: [addr] "r" (addr) : "memory");
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
			ret = virtual_inst_fault(vcpu, run, trap);
		break;
	case EXC_INST_GUEST_PAGE_FAULT:
	case EXC_LOAD_GUEST_PAGE_FAULT:
	case EXC_STORE_GUEST_PAGE_FAULT:
		if (vcpu->arch.guest_context.hstatus & HSTATUS_SPV)
			ret = stage2_page_fault(vcpu, run, trap);
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
