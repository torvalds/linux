/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/coproc.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Rusty Russell <rusty@rustcorp.com.au>
 *          Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/mm.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <trace/events/kvm.h>

#include "sys_regs.h"

/*
 * All of this file is extremly similar to the ARM coproc.c, but the
 * types are different. My gut feeling is that it should be pretty
 * easy to merge, but that would be an ABI breakage -- again. VFP
 * would also need to be abstracted.
 *
 * For AArch32, we only take care of what is being trapped. Anything
 * that has to do with init and userspace access has to go via the
 * 64bit interface.
 */

/* 3 bits per cache level, as per CLIDR, but non-existent caches always 0 */
static u32 cache_levels;

/* CSSELR values; used to index KVM_REG_ARM_DEMUX_ID_CCSIDR */
#define CSSELR_MAX 12

/* Which cache CCSIDR represents depends on CSSELR value. */
static u32 get_ccsidr(u32 csselr)
{
	u32 ccsidr;

	/* Make sure noone else changes CSSELR during this! */
	local_irq_disable();
	/* Put value into CSSELR */
	asm volatile("msr csselr_el1, %x0" : : "r" (csselr));
	isb();
	/* Read result out of CCSIDR */
	asm volatile("mrs %0, ccsidr_el1" : "=r" (ccsidr));
	local_irq_enable();

	return ccsidr;
}

static void do_dc_cisw(u32 val)
{
	asm volatile("dc cisw, %x0" : : "r" (val));
	dsb();
}

static void do_dc_csw(u32 val)
{
	asm volatile("dc csw, %x0" : : "r" (val));
	dsb();
}

/* See note at ARM ARM B1.14.4 */
static bool access_dcsw(struct kvm_vcpu *vcpu,
			const struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	unsigned long val;
	int cpu;

	if (!p->is_write)
		return read_from_write_only(vcpu, p);

	cpu = get_cpu();

	cpumask_setall(&vcpu->arch.require_dcache_flush);
	cpumask_clear_cpu(cpu, &vcpu->arch.require_dcache_flush);

	/* If we were already preempted, take the long way around */
	if (cpu != vcpu->arch.last_pcpu) {
		flush_cache_all();
		goto done;
	}

	val = *vcpu_reg(vcpu, p->Rt);

	switch (p->CRm) {
	case 6:			/* Upgrade DCISW to DCCISW, as per HCR.SWIO */
	case 14:		/* DCCISW */
		do_dc_cisw(val);
		break;

	case 10:		/* DCCSW */
		do_dc_csw(val);
		break;
	}

done:
	put_cpu();

	return true;
}

/*
 * We could trap ID_DFR0 and tell the guest we don't support performance
 * monitoring.  Unfortunately the patch to make the kernel check ID_DFR0 was
 * NAKed, so it will read the PMCR anyway.
 *
 * Therefore we tell the guest we have 0 counters.  Unfortunately, we
 * must always support PMCCNTR (the cycle counter): we just RAZ/WI for
 * all PM registers, which doesn't crash the guest kernel at least.
 */
static bool pm_fake(struct kvm_vcpu *vcpu,
		    const struct sys_reg_params *p,
		    const struct sys_reg_desc *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);
	else
		return read_zero(vcpu, p);
}

static void reset_amair_el1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 amair;

	asm volatile("mrs %0, amair_el1\n" : "=r" (amair));
	vcpu_sys_reg(vcpu, AMAIR_EL1) = amair;
}

static void reset_mpidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	/*
	 * Simply map the vcpu_id into the Aff0 field of the MPIDR.
	 */
	vcpu_sys_reg(vcpu, MPIDR_EL1) = (1UL << 31) | (vcpu->vcpu_id & 0xff);
}

/*
 * Architected system registers.
 * Important: Must be sorted ascending by Op0, Op1, CRn, CRm, Op2
 */
static const struct sys_reg_desc sys_reg_descs[] = {
	/* DC ISW */
	{ Op0(0b01), Op1(0b000), CRn(0b0111), CRm(0b0110), Op2(0b010),
	  access_dcsw },
	/* DC CSW */
	{ Op0(0b01), Op1(0b000), CRn(0b0111), CRm(0b1010), Op2(0b010),
	  access_dcsw },
	/* DC CISW */
	{ Op0(0b01), Op1(0b000), CRn(0b0111), CRm(0b1110), Op2(0b010),
	  access_dcsw },

	/* TEECR32_EL1 */
	{ Op0(0b10), Op1(0b010), CRn(0b0000), CRm(0b0000), Op2(0b000),
	  NULL, reset_val, TEECR32_EL1, 0 },
	/* TEEHBR32_EL1 */
	{ Op0(0b10), Op1(0b010), CRn(0b0001), CRm(0b0000), Op2(0b000),
	  NULL, reset_val, TEEHBR32_EL1, 0 },
	/* DBGVCR32_EL2 */
	{ Op0(0b10), Op1(0b100), CRn(0b0000), CRm(0b0111), Op2(0b000),
	  NULL, reset_val, DBGVCR32_EL2, 0 },

	/* MPIDR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0000), Op2(0b101),
	  NULL, reset_mpidr, MPIDR_EL1 },
	/* SCTLR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b000),
	  NULL, reset_val, SCTLR_EL1, 0x00C50078 },
	/* CPACR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b010),
	  NULL, reset_val, CPACR_EL1, 0 },
	/* TTBR0_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0010), CRm(0b0000), Op2(0b000),
	  NULL, reset_unknown, TTBR0_EL1 },
	/* TTBR1_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0010), CRm(0b0000), Op2(0b001),
	  NULL, reset_unknown, TTBR1_EL1 },
	/* TCR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0010), CRm(0b0000), Op2(0b010),
	  NULL, reset_val, TCR_EL1, 0 },

	/* AFSR0_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0101), CRm(0b0001), Op2(0b000),
	  NULL, reset_unknown, AFSR0_EL1 },
	/* AFSR1_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0101), CRm(0b0001), Op2(0b001),
	  NULL, reset_unknown, AFSR1_EL1 },
	/* ESR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0101), CRm(0b0010), Op2(0b000),
	  NULL, reset_unknown, ESR_EL1 },
	/* FAR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0110), CRm(0b0000), Op2(0b000),
	  NULL, reset_unknown, FAR_EL1 },

	/* PMINTENSET_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1001), CRm(0b1110), Op2(0b001),
	  pm_fake },
	/* PMINTENCLR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1001), CRm(0b1110), Op2(0b010),
	  pm_fake },

	/* MAIR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1010), CRm(0b0010), Op2(0b000),
	  NULL, reset_unknown, MAIR_EL1 },
	/* AMAIR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1010), CRm(0b0011), Op2(0b000),
	  NULL, reset_amair_el1, AMAIR_EL1 },

	/* VBAR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1100), CRm(0b0000), Op2(0b000),
	  NULL, reset_val, VBAR_EL1, 0 },
	/* CONTEXTIDR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1101), CRm(0b0000), Op2(0b001),
	  NULL, reset_val, CONTEXTIDR_EL1, 0 },
	/* TPIDR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1101), CRm(0b0000), Op2(0b100),
	  NULL, reset_unknown, TPIDR_EL1 },

	/* CNTKCTL_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1110), CRm(0b0001), Op2(0b000),
	  NULL, reset_val, CNTKCTL_EL1, 0},

	/* CSSELR_EL1 */
	{ Op0(0b11), Op1(0b010), CRn(0b0000), CRm(0b0000), Op2(0b000),
	  NULL, reset_unknown, CSSELR_EL1 },

	/* PMCR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b000),
	  pm_fake },
	/* PMCNTENSET_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b001),
	  pm_fake },
	/* PMCNTENCLR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b010),
	  pm_fake },
	/* PMOVSCLR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b011),
	  pm_fake },
	/* PMSWINC_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b100),
	  pm_fake },
	/* PMSELR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b101),
	  pm_fake },
	/* PMCEID0_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b110),
	  pm_fake },
	/* PMCEID1_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b111),
	  pm_fake },
	/* PMCCNTR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1101), Op2(0b000),
	  pm_fake },
	/* PMXEVTYPER_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1101), Op2(0b001),
	  pm_fake },
	/* PMXEVCNTR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1101), Op2(0b010),
	  pm_fake },
	/* PMUSERENR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1110), Op2(0b000),
	  pm_fake },
	/* PMOVSSET_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1110), Op2(0b011),
	  pm_fake },

	/* TPIDR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1101), CRm(0b0000), Op2(0b010),
	  NULL, reset_unknown, TPIDR_EL0 },
	/* TPIDRRO_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1101), CRm(0b0000), Op2(0b011),
	  NULL, reset_unknown, TPIDRRO_EL0 },

	/* DACR32_EL2 */
	{ Op0(0b11), Op1(0b100), CRn(0b0011), CRm(0b0000), Op2(0b000),
	  NULL, reset_unknown, DACR32_EL2 },
	/* IFSR32_EL2 */
	{ Op0(0b11), Op1(0b100), CRn(0b0101), CRm(0b0000), Op2(0b001),
	  NULL, reset_unknown, IFSR32_EL2 },
	/* FPEXC32_EL2 */
	{ Op0(0b11), Op1(0b100), CRn(0b0101), CRm(0b0011), Op2(0b000),
	  NULL, reset_val, FPEXC32_EL2, 0x70 },
};

/* Trapped cp15 registers */
static const struct sys_reg_desc cp15_regs[] = {
	/*
	 * DC{C,I,CI}SW operations:
	 */
	{ Op1( 0), CRn( 7), CRm( 6), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 7), CRm(10), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 7), CRm(14), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 0), pm_fake },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 1), pm_fake },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 2), pm_fake },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 3), pm_fake },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 5), pm_fake },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 6), pm_fake },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 7), pm_fake },
	{ Op1( 0), CRn( 9), CRm(13), Op2( 0), pm_fake },
	{ Op1( 0), CRn( 9), CRm(13), Op2( 1), pm_fake },
	{ Op1( 0), CRn( 9), CRm(13), Op2( 2), pm_fake },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 0), pm_fake },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 1), pm_fake },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 2), pm_fake },
};

/* Target specific emulation tables */
static struct kvm_sys_reg_target_table *target_tables[KVM_ARM_NUM_TARGETS];

void kvm_register_target_sys_reg_table(unsigned int target,
				       struct kvm_sys_reg_target_table *table)
{
	target_tables[target] = table;
}

/* Get specific register table for this target. */
static const struct sys_reg_desc *get_target_table(unsigned target,
						   bool mode_is_64,
						   size_t *num)
{
	struct kvm_sys_reg_target_table *table;

	table = target_tables[target];
	if (mode_is_64) {
		*num = table->table64.num;
		return table->table64.table;
	} else {
		*num = table->table32.num;
		return table->table32.table;
	}
}

static const struct sys_reg_desc *find_reg(const struct sys_reg_params *params,
					 const struct sys_reg_desc table[],
					 unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		const struct sys_reg_desc *r = &table[i];

		if (params->Op0 != r->Op0)
			continue;
		if (params->Op1 != r->Op1)
			continue;
		if (params->CRn != r->CRn)
			continue;
		if (params->CRm != r->CRm)
			continue;
		if (params->Op2 != r->Op2)
			continue;

		return r;
	}
	return NULL;
}

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

int kvm_handle_cp14_access(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

static void emulate_cp15(struct kvm_vcpu *vcpu,
			 const struct sys_reg_params *params)
{
	size_t num;
	const struct sys_reg_desc *table, *r;

	table = get_target_table(vcpu->arch.target, false, &num);

	/* Search target-specific then generic table. */
	r = find_reg(params, table, num);
	if (!r)
		r = find_reg(params, cp15_regs, ARRAY_SIZE(cp15_regs));

	if (likely(r)) {
		/*
		 * Not having an accessor means that we have
		 * configured a trap that we don't know how to
		 * handle. This certainly qualifies as a gross bug
		 * that should be fixed right away.
		 */
		BUG_ON(!r->access);

		if (likely(r->access(vcpu, params, r))) {
			/* Skip instruction, since it was emulated */
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			return;
		}
		/* If access function fails, it should complain. */
	}

	kvm_err("Unsupported guest CP15 access at: %08lx\n", *vcpu_pc(vcpu));
	print_sys_reg_instr(params);
	kvm_inject_undefined(vcpu);
}

/**
 * kvm_handle_cp15_64 -- handles a mrrc/mcrr trap on a guest CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_cp15_64(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct sys_reg_params params;
	u32 hsr = kvm_vcpu_get_hsr(vcpu);
	int Rt2 = (hsr >> 10) & 0xf;

	params.CRm = (hsr >> 1) & 0xf;
	params.Rt = (hsr >> 5) & 0xf;
	params.is_write = ((hsr & 1) == 0);

	params.Op0 = 0;
	params.Op1 = (hsr >> 16) & 0xf;
	params.Op2 = 0;
	params.CRn = 0;

	/*
	 * Massive hack here. Store Rt2 in the top 32bits so we only
	 * have one register to deal with. As we use the same trap
	 * backends between AArch32 and AArch64, we get away with it.
	 */
	if (params.is_write) {
		u64 val = *vcpu_reg(vcpu, params.Rt);
		val &= 0xffffffff;
		val |= *vcpu_reg(vcpu, Rt2) << 32;
		*vcpu_reg(vcpu, params.Rt) = val;
	}

	emulate_cp15(vcpu, &params);

	/* Do the opposite hack for the read side */
	if (!params.is_write) {
		u64 val = *vcpu_reg(vcpu, params.Rt);
		val >>= 32;
		*vcpu_reg(vcpu, Rt2) = val;
	}

	return 1;
}

/**
 * kvm_handle_cp15_32 -- handles a mrc/mcr trap on a guest CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_cp15_32(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct sys_reg_params params;
	u32 hsr = kvm_vcpu_get_hsr(vcpu);

	params.CRm = (hsr >> 1) & 0xf;
	params.Rt  = (hsr >> 5) & 0xf;
	params.is_write = ((hsr & 1) == 0);
	params.CRn = (hsr >> 10) & 0xf;
	params.Op0 = 0;
	params.Op1 = (hsr >> 14) & 0x7;
	params.Op2 = (hsr >> 17) & 0x7;

	emulate_cp15(vcpu, &params);
	return 1;
}

static int emulate_sys_reg(struct kvm_vcpu *vcpu,
			   const struct sys_reg_params *params)
{
	size_t num;
	const struct sys_reg_desc *table, *r;

	table = get_target_table(vcpu->arch.target, true, &num);

	/* Search target-specific then generic table. */
	r = find_reg(params, table, num);
	if (!r)
		r = find_reg(params, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	if (likely(r)) {
		/*
		 * Not having an accessor means that we have
		 * configured a trap that we don't know how to
		 * handle. This certainly qualifies as a gross bug
		 * that should be fixed right away.
		 */
		BUG_ON(!r->access);

		if (likely(r->access(vcpu, params, r))) {
			/* Skip instruction, since it was emulated */
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			return 1;
		}
		/* If access function fails, it should complain. */
	} else {
		kvm_err("Unsupported guest sys_reg access at: %lx\n",
			*vcpu_pc(vcpu));
		print_sys_reg_instr(params);
	}
	kvm_inject_undefined(vcpu);
	return 1;
}

static void reset_sys_reg_descs(struct kvm_vcpu *vcpu,
			      const struct sys_reg_desc *table, size_t num)
{
	unsigned long i;

	for (i = 0; i < num; i++)
		if (table[i].reset)
			table[i].reset(vcpu, &table[i]);
}

/**
 * kvm_handle_sys_reg -- handles a mrs/msr trap on a guest sys_reg access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct sys_reg_params params;
	unsigned long esr = kvm_vcpu_get_hsr(vcpu);

	params.Op0 = (esr >> 20) & 3;
	params.Op1 = (esr >> 14) & 0x7;
	params.CRn = (esr >> 10) & 0xf;
	params.CRm = (esr >> 1) & 0xf;
	params.Op2 = (esr >> 17) & 0x7;
	params.Rt = (esr >> 5) & 0x1f;
	params.is_write = !(esr & 1);

	return emulate_sys_reg(vcpu, &params);
}

/******************************************************************************
 * Userspace API
 *****************************************************************************/

static bool index_to_params(u64 id, struct sys_reg_params *params)
{
	switch (id & KVM_REG_SIZE_MASK) {
	case KVM_REG_SIZE_U64:
		/* Any unused index bits means it's not valid. */
		if (id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK
			      | KVM_REG_ARM_COPROC_MASK
			      | KVM_REG_ARM64_SYSREG_OP0_MASK
			      | KVM_REG_ARM64_SYSREG_OP1_MASK
			      | KVM_REG_ARM64_SYSREG_CRN_MASK
			      | KVM_REG_ARM64_SYSREG_CRM_MASK
			      | KVM_REG_ARM64_SYSREG_OP2_MASK))
			return false;
		params->Op0 = ((id & KVM_REG_ARM64_SYSREG_OP0_MASK)
			       >> KVM_REG_ARM64_SYSREG_OP0_SHIFT);
		params->Op1 = ((id & KVM_REG_ARM64_SYSREG_OP1_MASK)
			       >> KVM_REG_ARM64_SYSREG_OP1_SHIFT);
		params->CRn = ((id & KVM_REG_ARM64_SYSREG_CRN_MASK)
			       >> KVM_REG_ARM64_SYSREG_CRN_SHIFT);
		params->CRm = ((id & KVM_REG_ARM64_SYSREG_CRM_MASK)
			       >> KVM_REG_ARM64_SYSREG_CRM_SHIFT);
		params->Op2 = ((id & KVM_REG_ARM64_SYSREG_OP2_MASK)
			       >> KVM_REG_ARM64_SYSREG_OP2_SHIFT);
		return true;
	default:
		return false;
	}
}

/* Decode an index value, and find the sys_reg_desc entry. */
static const struct sys_reg_desc *index_to_sys_reg_desc(struct kvm_vcpu *vcpu,
						    u64 id)
{
	size_t num;
	const struct sys_reg_desc *table, *r;
	struct sys_reg_params params;

	/* We only do sys_reg for now. */
	if ((id & KVM_REG_ARM_COPROC_MASK) != KVM_REG_ARM64_SYSREG)
		return NULL;

	if (!index_to_params(id, &params))
		return NULL;

	table = get_target_table(vcpu->arch.target, true, &num);
	r = find_reg(&params, table, num);
	if (!r)
		r = find_reg(&params, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	/* Not saved in the sys_reg array? */
	if (r && !r->reg)
		r = NULL;

	return r;
}

/*
 * These are the invariant sys_reg registers: we let the guest see the
 * host versions of these, so they're part of the guest state.
 *
 * A future CPU may provide a mechanism to present different values to
 * the guest, or a future kvm may trap them.
 */

#define FUNCTION_INVARIANT(reg)						\
	static void get_##reg(struct kvm_vcpu *v,			\
			      const struct sys_reg_desc *r)		\
	{								\
		u64 val;						\
									\
		asm volatile("mrs %0, " __stringify(reg) "\n"		\
			     : "=r" (val));				\
		((struct sys_reg_desc *)r)->val = val;			\
	}

FUNCTION_INVARIANT(midr_el1)
FUNCTION_INVARIANT(ctr_el0)
FUNCTION_INVARIANT(revidr_el1)
FUNCTION_INVARIANT(id_pfr0_el1)
FUNCTION_INVARIANT(id_pfr1_el1)
FUNCTION_INVARIANT(id_dfr0_el1)
FUNCTION_INVARIANT(id_afr0_el1)
FUNCTION_INVARIANT(id_mmfr0_el1)
FUNCTION_INVARIANT(id_mmfr1_el1)
FUNCTION_INVARIANT(id_mmfr2_el1)
FUNCTION_INVARIANT(id_mmfr3_el1)
FUNCTION_INVARIANT(id_isar0_el1)
FUNCTION_INVARIANT(id_isar1_el1)
FUNCTION_INVARIANT(id_isar2_el1)
FUNCTION_INVARIANT(id_isar3_el1)
FUNCTION_INVARIANT(id_isar4_el1)
FUNCTION_INVARIANT(id_isar5_el1)
FUNCTION_INVARIANT(clidr_el1)
FUNCTION_INVARIANT(aidr_el1)

/* ->val is filled in by kvm_sys_reg_table_init() */
static struct sys_reg_desc invariant_sys_regs[] = {
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0000), Op2(0b000),
	  NULL, get_midr_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0000), Op2(0b110),
	  NULL, get_revidr_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b000),
	  NULL, get_id_pfr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b001),
	  NULL, get_id_pfr1_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b010),
	  NULL, get_id_dfr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b011),
	  NULL, get_id_afr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b100),
	  NULL, get_id_mmfr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b101),
	  NULL, get_id_mmfr1_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b110),
	  NULL, get_id_mmfr2_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b111),
	  NULL, get_id_mmfr3_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b000),
	  NULL, get_id_isar0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b001),
	  NULL, get_id_isar1_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b010),
	  NULL, get_id_isar2_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b011),
	  NULL, get_id_isar3_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b100),
	  NULL, get_id_isar4_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b101),
	  NULL, get_id_isar5_el1 },
	{ Op0(0b11), Op1(0b001), CRn(0b0000), CRm(0b0000), Op2(0b001),
	  NULL, get_clidr_el1 },
	{ Op0(0b11), Op1(0b001), CRn(0b0000), CRm(0b0000), Op2(0b111),
	  NULL, get_aidr_el1 },
	{ Op0(0b11), Op1(0b011), CRn(0b0000), CRm(0b0000), Op2(0b001),
	  NULL, get_ctr_el0 },
};

static int reg_from_user(void *val, const void __user *uaddr, u64 id)
{
	/* This Just Works because we are little endian. */
	if (copy_from_user(val, uaddr, KVM_REG_SIZE(id)) != 0)
		return -EFAULT;
	return 0;
}

static int reg_to_user(void __user *uaddr, const void *val, u64 id)
{
	/* This Just Works because we are little endian. */
	if (copy_to_user(uaddr, val, KVM_REG_SIZE(id)) != 0)
		return -EFAULT;
	return 0;
}

static int get_invariant_sys_reg(u64 id, void __user *uaddr)
{
	struct sys_reg_params params;
	const struct sys_reg_desc *r;

	if (!index_to_params(id, &params))
		return -ENOENT;

	r = find_reg(&params, invariant_sys_regs, ARRAY_SIZE(invariant_sys_regs));
	if (!r)
		return -ENOENT;

	return reg_to_user(uaddr, &r->val, id);
}

static int set_invariant_sys_reg(u64 id, void __user *uaddr)
{
	struct sys_reg_params params;
	const struct sys_reg_desc *r;
	int err;
	u64 val = 0; /* Make sure high bits are 0 for 32-bit regs */

	if (!index_to_params(id, &params))
		return -ENOENT;
	r = find_reg(&params, invariant_sys_regs, ARRAY_SIZE(invariant_sys_regs));
	if (!r)
		return -ENOENT;

	err = reg_from_user(&val, uaddr, id);
	if (err)
		return err;

	/* This is what we mean by invariant: you can't change it. */
	if (r->val != val)
		return -EINVAL;

	return 0;
}

static bool is_valid_cache(u32 val)
{
	u32 level, ctype;

	if (val >= CSSELR_MAX)
		return -ENOENT;

	/* Bottom bit is Instruction or Data bit.  Next 3 bits are level. */
	level = (val >> 1);
	ctype = (cache_levels >> (level * 3)) & 7;

	switch (ctype) {
	case 0: /* No cache */
		return false;
	case 1: /* Instruction cache only */
		return (val & 1);
	case 2: /* Data cache only */
	case 4: /* Unified cache */
		return !(val & 1);
	case 3: /* Separate instruction and data caches */
		return true;
	default: /* Reserved: we can't know instruction or data. */
		return false;
	}
}

static int demux_c15_get(u64 id, void __user *uaddr)
{
	u32 val;
	u32 __user *uval = uaddr;

	/* Fail if we have unknown bits set. */
	if (id & ~(KVM_REG_ARCH_MASK|KVM_REG_SIZE_MASK|KVM_REG_ARM_COPROC_MASK
		   | ((1 << KVM_REG_ARM_COPROC_SHIFT)-1)))
		return -ENOENT;

	switch (id & KVM_REG_ARM_DEMUX_ID_MASK) {
	case KVM_REG_ARM_DEMUX_ID_CCSIDR:
		if (KVM_REG_SIZE(id) != 4)
			return -ENOENT;
		val = (id & KVM_REG_ARM_DEMUX_VAL_MASK)
			>> KVM_REG_ARM_DEMUX_VAL_SHIFT;
		if (!is_valid_cache(val))
			return -ENOENT;

		return put_user(get_ccsidr(val), uval);
	default:
		return -ENOENT;
	}
}

static int demux_c15_set(u64 id, void __user *uaddr)
{
	u32 val, newval;
	u32 __user *uval = uaddr;

	/* Fail if we have unknown bits set. */
	if (id & ~(KVM_REG_ARCH_MASK|KVM_REG_SIZE_MASK|KVM_REG_ARM_COPROC_MASK
		   | ((1 << KVM_REG_ARM_COPROC_SHIFT)-1)))
		return -ENOENT;

	switch (id & KVM_REG_ARM_DEMUX_ID_MASK) {
	case KVM_REG_ARM_DEMUX_ID_CCSIDR:
		if (KVM_REG_SIZE(id) != 4)
			return -ENOENT;
		val = (id & KVM_REG_ARM_DEMUX_VAL_MASK)
			>> KVM_REG_ARM_DEMUX_VAL_SHIFT;
		if (!is_valid_cache(val))
			return -ENOENT;

		if (get_user(newval, uval))
			return -EFAULT;

		/* This is also invariant: you can't change it. */
		if (newval != get_ccsidr(val))
			return -EINVAL;
		return 0;
	default:
		return -ENOENT;
	}
}

int kvm_arm_sys_reg_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	const struct sys_reg_desc *r;
	void __user *uaddr = (void __user *)(unsigned long)reg->addr;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_get(reg->id, uaddr);

	if (KVM_REG_SIZE(reg->id) != sizeof(__u64))
		return -ENOENT;

	r = index_to_sys_reg_desc(vcpu, reg->id);
	if (!r)
		return get_invariant_sys_reg(reg->id, uaddr);

	return reg_to_user(uaddr, &vcpu_sys_reg(vcpu, r->reg), reg->id);
}

int kvm_arm_sys_reg_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	const struct sys_reg_desc *r;
	void __user *uaddr = (void __user *)(unsigned long)reg->addr;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_set(reg->id, uaddr);

	if (KVM_REG_SIZE(reg->id) != sizeof(__u64))
		return -ENOENT;

	r = index_to_sys_reg_desc(vcpu, reg->id);
	if (!r)
		return set_invariant_sys_reg(reg->id, uaddr);

	return reg_from_user(&vcpu_sys_reg(vcpu, r->reg), uaddr, reg->id);
}

static unsigned int num_demux_regs(void)
{
	unsigned int i, count = 0;

	for (i = 0; i < CSSELR_MAX; i++)
		if (is_valid_cache(i))
			count++;

	return count;
}

static int write_demux_regids(u64 __user *uindices)
{
	u64 val = KVM_REG_ARM | KVM_REG_SIZE_U32 | KVM_REG_ARM_DEMUX;
	unsigned int i;

	val |= KVM_REG_ARM_DEMUX_ID_CCSIDR;
	for (i = 0; i < CSSELR_MAX; i++) {
		if (!is_valid_cache(i))
			continue;
		if (put_user(val | i, uindices))
			return -EFAULT;
		uindices++;
	}
	return 0;
}

static u64 sys_reg_to_index(const struct sys_reg_desc *reg)
{
	return (KVM_REG_ARM64 | KVM_REG_SIZE_U64 |
		KVM_REG_ARM64_SYSREG |
		(reg->Op0 << KVM_REG_ARM64_SYSREG_OP0_SHIFT) |
		(reg->Op1 << KVM_REG_ARM64_SYSREG_OP1_SHIFT) |
		(reg->CRn << KVM_REG_ARM64_SYSREG_CRN_SHIFT) |
		(reg->CRm << KVM_REG_ARM64_SYSREG_CRM_SHIFT) |
		(reg->Op2 << KVM_REG_ARM64_SYSREG_OP2_SHIFT));
}

static bool copy_reg_to_user(const struct sys_reg_desc *reg, u64 __user **uind)
{
	if (!*uind)
		return true;

	if (put_user(sys_reg_to_index(reg), *uind))
		return false;

	(*uind)++;
	return true;
}

/* Assumed ordered tables, see kvm_sys_reg_table_init. */
static int walk_sys_regs(struct kvm_vcpu *vcpu, u64 __user *uind)
{
	const struct sys_reg_desc *i1, *i2, *end1, *end2;
	unsigned int total = 0;
	size_t num;

	/* We check for duplicates here, to allow arch-specific overrides. */
	i1 = get_target_table(vcpu->arch.target, true, &num);
	end1 = i1 + num;
	i2 = sys_reg_descs;
	end2 = sys_reg_descs + ARRAY_SIZE(sys_reg_descs);

	BUG_ON(i1 == end1 || i2 == end2);

	/* Walk carefully, as both tables may refer to the same register. */
	while (i1 || i2) {
		int cmp = cmp_sys_reg(i1, i2);
		/* target-specific overrides generic entry. */
		if (cmp <= 0) {
			/* Ignore registers we trap but don't save. */
			if (i1->reg) {
				if (!copy_reg_to_user(i1, &uind))
					return -EFAULT;
				total++;
			}
		} else {
			/* Ignore registers we trap but don't save. */
			if (i2->reg) {
				if (!copy_reg_to_user(i2, &uind))
					return -EFAULT;
				total++;
			}
		}

		if (cmp <= 0 && ++i1 == end1)
			i1 = NULL;
		if (cmp >= 0 && ++i2 == end2)
			i2 = NULL;
	}
	return total;
}

unsigned long kvm_arm_num_sys_reg_descs(struct kvm_vcpu *vcpu)
{
	return ARRAY_SIZE(invariant_sys_regs)
		+ num_demux_regs()
		+ walk_sys_regs(vcpu, (u64 __user *)NULL);
}

int kvm_arm_copy_sys_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	unsigned int i;
	int err;

	/* Then give them all the invariant registers' indices. */
	for (i = 0; i < ARRAY_SIZE(invariant_sys_regs); i++) {
		if (put_user(sys_reg_to_index(&invariant_sys_regs[i]), uindices))
			return -EFAULT;
		uindices++;
	}

	err = walk_sys_regs(vcpu, uindices);
	if (err < 0)
		return err;
	uindices += err;

	return write_demux_regids(uindices);
}

void kvm_sys_reg_table_init(void)
{
	unsigned int i;
	struct sys_reg_desc clidr;

	/* Make sure tables are unique and in order. */
	for (i = 1; i < ARRAY_SIZE(sys_reg_descs); i++)
		BUG_ON(cmp_sys_reg(&sys_reg_descs[i-1], &sys_reg_descs[i]) >= 0);

	/* We abuse the reset function to overwrite the table itself. */
	for (i = 0; i < ARRAY_SIZE(invariant_sys_regs); i++)
		invariant_sys_regs[i].reset(NULL, &invariant_sys_regs[i]);

	/*
	 * CLIDR format is awkward, so clean it up.  See ARM B4.1.20:
	 *
	 *   If software reads the Cache Type fields from Ctype1
	 *   upwards, once it has seen a value of 0b000, no caches
	 *   exist at further-out levels of the hierarchy. So, for
	 *   example, if Ctype3 is the first Cache Type field with a
	 *   value of 0b000, the values of Ctype4 to Ctype7 must be
	 *   ignored.
	 */
	get_clidr_el1(NULL, &clidr); /* Ugly... */
	cache_levels = clidr.val;
	for (i = 0; i < 7; i++)
		if (((cache_levels >> (i*3)) & 7) == 0)
			break;
	/* Clear all higher bits. */
	cache_levels &= (1 << (i*3))-1;
}

/**
 * kvm_reset_sys_regs - sets system registers to reset value
 * @vcpu: The VCPU pointer
 *
 * This function finds the right table above and sets the registers on the
 * virtual CPU struct to their architecturally defined reset values.
 */
void kvm_reset_sys_regs(struct kvm_vcpu *vcpu)
{
	size_t num;
	const struct sys_reg_desc *table;

	/* Catch someone adding a register without putting in reset entry. */
	memset(&vcpu->arch.ctxt.sys_regs, 0x42, sizeof(vcpu->arch.ctxt.sys_regs));

	/* Generic chip reset first (so target could override). */
	reset_sys_reg_descs(vcpu, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	table = get_target_table(vcpu->arch.target, true, &num);
	reset_sys_reg_descs(vcpu, table, num);

	for (num = 1; num < NR_SYS_REGS; num++)
		if (vcpu_sys_reg(vcpu, num) == 0x4242424242424242)
			panic("Didn't reset vcpu_sys_reg(%zi)", num);
}
