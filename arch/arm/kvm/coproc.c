/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/bsearch.h>
#include <linux/mm.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_mmu.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <trace/events/kvm.h>
#include <asm/vfp.h>
#include "../vfp/vfpinstr.h"

#define CREATE_TRACE_POINTS
#include "trace.h"
#include "coproc.h"


/******************************************************************************
 * Co-processor emulation
 *****************************************************************************/

static bool write_to_read_only(struct kvm_vcpu *vcpu,
			       const struct coproc_params *params)
{
	WARN_ONCE(1, "CP15 write to read-only register\n");
	print_cp_instr(params);
	kvm_inject_undefined(vcpu);
	return false;
}

static bool read_from_write_only(struct kvm_vcpu *vcpu,
				 const struct coproc_params *params)
{
	WARN_ONCE(1, "CP15 read to write-only register\n");
	print_cp_instr(params);
	kvm_inject_undefined(vcpu);
	return false;
}

/* 3 bits per cache level, as per CLIDR, but non-existent caches always 0 */
static u32 cache_levels;

/* CSSELR values; used to index KVM_REG_ARM_DEMUX_ID_CCSIDR */
#define CSSELR_MAX 12

/*
 * kvm_vcpu_arch.cp15 holds cp15 registers as an array of u32, but some
 * of cp15 registers can be viewed either as couple of two u32 registers
 * or one u64 register. Current u64 register encoding is that least
 * significant u32 word is followed by most significant u32 word.
 */
static inline void vcpu_cp15_reg64_set(struct kvm_vcpu *vcpu,
				       const struct coproc_reg *r,
				       u64 val)
{
	vcpu_cp15(vcpu, r->reg) = val & 0xffffffff;
	vcpu_cp15(vcpu, r->reg + 1) = val >> 32;
}

static inline u64 vcpu_cp15_reg64_get(struct kvm_vcpu *vcpu,
				      const struct coproc_reg *r)
{
	u64 val;

	val = vcpu_cp15(vcpu, r->reg + 1);
	val = val << 32;
	val = val | vcpu_cp15(vcpu, r->reg);
	return val;
}

int kvm_handle_cp10_id(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

int kvm_handle_cp_0_13_access(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	/*
	 * We can get here, if the host has been built without VFPv3 support,
	 * but the guest attempted a floating point operation.
	 */
	kvm_inject_undefined(vcpu);
	return 1;
}

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

static void reset_mpidr(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	/*
	 * Compute guest MPIDR. We build a virtual cluster out of the
	 * vcpu_id, but we read the 'U' bit from the underlying
	 * hardware directly.
	 */
	vcpu_cp15(vcpu, c0_MPIDR) = ((read_cpuid_mpidr() & MPIDR_SMP_BITMASK) |
				     ((vcpu->vcpu_id >> 2) << MPIDR_LEVEL_BITS) |
				     (vcpu->vcpu_id & 3));
}

/* TRM entries A7:4.3.31 A15:4.3.28 - RO WI */
static bool access_actlr(struct kvm_vcpu *vcpu,
			 const struct coproc_params *p,
			 const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = vcpu_cp15(vcpu, c1_ACTLR);
	return true;
}

/* TRM entries A7:4.3.56, A15:4.3.60 - R/O. */
static bool access_cbar(struct kvm_vcpu *vcpu,
			const struct coproc_params *p,
			const struct coproc_reg *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p);
	return read_zero(vcpu, p);
}

/* TRM entries A7:4.3.49, A15:4.3.48 - R/O WI */
static bool access_l2ctlr(struct kvm_vcpu *vcpu,
			  const struct coproc_params *p,
			  const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = vcpu_cp15(vcpu, c9_L2CTLR);
	return true;
}

static void reset_l2ctlr(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	u32 l2ctlr, ncores;

	asm volatile("mrc p15, 1, %0, c9, c0, 2\n" : "=r" (l2ctlr));
	l2ctlr &= ~(3 << 24);
	ncores = atomic_read(&vcpu->kvm->online_vcpus) - 1;
	/* How many cores in the current cluster and the next ones */
	ncores -= (vcpu->vcpu_id & ~3);
	/* Cap it to the maximum number of cores in a single cluster */
	ncores = min(ncores, 3U);
	l2ctlr |= (ncores & 3) << 24;

	vcpu_cp15(vcpu, c9_L2CTLR) = l2ctlr;
}

static void reset_actlr(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	u32 actlr;

	/* ACTLR contains SMP bit: make sure you create all cpus first! */
	asm volatile("mrc p15, 0, %0, c1, c0, 1\n" : "=r" (actlr));
	/* Make the SMP bit consistent with the guest configuration */
	if (atomic_read(&vcpu->kvm->online_vcpus) > 1)
		actlr |= 1U << 6;
	else
		actlr &= ~(1U << 6);

	vcpu_cp15(vcpu, c1_ACTLR) = actlr;
}

/*
 * TRM entries: A7:4.3.50, A15:4.3.49
 * R/O WI (even if NSACR.NS_L2ERR, a write of 1 is ignored).
 */
static bool access_l2ectlr(struct kvm_vcpu *vcpu,
			   const struct coproc_params *p,
			   const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = 0;
	return true;
}

/*
 * See note at ARMv7 ARM B1.14.4 (TL;DR: S/W ops are not easily virtualized).
 */
static bool access_dcsw(struct kvm_vcpu *vcpu,
			const struct coproc_params *p,
			const struct coproc_reg *r)
{
	if (!p->is_write)
		return read_from_write_only(vcpu, p);

	kvm_set_way_flush(vcpu);
	return true;
}

/*
 * Generic accessor for VM registers. Only called as long as HCR_TVM
 * is set.  If the guest enables the MMU, we stop trapping the VM
 * sys_regs and leave it in complete control of the caches.
 *
 * Used by the cpu-specific code.
 */
bool access_vm_reg(struct kvm_vcpu *vcpu,
		   const struct coproc_params *p,
		   const struct coproc_reg *r)
{
	bool was_enabled = vcpu_has_cache_enabled(vcpu);

	BUG_ON(!p->is_write);

	vcpu_cp15(vcpu, r->reg) = *vcpu_reg(vcpu, p->Rt1);
	if (p->is_64bit)
		vcpu_cp15(vcpu, r->reg + 1) = *vcpu_reg(vcpu, p->Rt2);

	kvm_toggle_cache(vcpu, was_enabled);
	return true;
}

static bool access_gic_sgi(struct kvm_vcpu *vcpu,
			   const struct coproc_params *p,
			   const struct coproc_reg *r)
{
	u64 reg;
	bool g1;

	if (!p->is_write)
		return read_from_write_only(vcpu, p);

	reg = (u64)*vcpu_reg(vcpu, p->Rt2) << 32;
	reg |= *vcpu_reg(vcpu, p->Rt1) ;

	/*
	 * In a system where GICD_CTLR.DS=1, a ICC_SGI0R access generates
	 * Group0 SGIs only, while ICC_SGI1R can generate either group,
	 * depending on the SGI configuration. ICC_ASGI1R is effectively
	 * equivalent to ICC_SGI0R, as there is no "alternative" secure
	 * group.
	 */
	switch (p->Op1) {
	default:		/* Keep GCC quiet */
	case 0:			/* ICC_SGI1R */
		g1 = true;
		break;
	case 1:			/* ICC_ASGI1R */
	case 2:			/* ICC_SGI0R */
		g1 = false;
		break;
	}

	vgic_v3_dispatch_sgi(vcpu, reg, g1);

	return true;
}

static bool access_gic_sre(struct kvm_vcpu *vcpu,
			   const struct coproc_params *p,
			   const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = vcpu->arch.vgic_cpu.vgic_v3.vgic_sre;

	return true;
}

static bool access_cntp_tval(struct kvm_vcpu *vcpu,
			     const struct coproc_params *p,
			     const struct coproc_reg *r)
{
	u64 now = kvm_phys_timer_read();
	u64 val;

	if (p->is_write) {
		val = *vcpu_reg(vcpu, p->Rt1);
		kvm_arm_timer_set_reg(vcpu, KVM_REG_ARM_PTIMER_CVAL, val + now);
	} else {
		val = kvm_arm_timer_get_reg(vcpu, KVM_REG_ARM_PTIMER_CVAL);
		*vcpu_reg(vcpu, p->Rt1) = val - now;
	}

	return true;
}

static bool access_cntp_ctl(struct kvm_vcpu *vcpu,
			    const struct coproc_params *p,
			    const struct coproc_reg *r)
{
	u32 val;

	if (p->is_write) {
		val = *vcpu_reg(vcpu, p->Rt1);
		kvm_arm_timer_set_reg(vcpu, KVM_REG_ARM_PTIMER_CTL, val);
	} else {
		val = kvm_arm_timer_get_reg(vcpu, KVM_REG_ARM_PTIMER_CTL);
		*vcpu_reg(vcpu, p->Rt1) = val;
	}

	return true;
}

static bool access_cntp_cval(struct kvm_vcpu *vcpu,
			     const struct coproc_params *p,
			     const struct coproc_reg *r)
{
	u64 val;

	if (p->is_write) {
		val = (u64)*vcpu_reg(vcpu, p->Rt2) << 32;
		val |= *vcpu_reg(vcpu, p->Rt1);
		kvm_arm_timer_set_reg(vcpu, KVM_REG_ARM_PTIMER_CVAL, val);
	} else {
		val = kvm_arm_timer_get_reg(vcpu, KVM_REG_ARM_PTIMER_CVAL);
		*vcpu_reg(vcpu, p->Rt1) = val;
		*vcpu_reg(vcpu, p->Rt2) = val >> 32;
	}

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
static bool trap_raz_wi(struct kvm_vcpu *vcpu,
		    const struct coproc_params *p,
		    const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);
	else
		return read_zero(vcpu, p);
}

#define access_pmcr trap_raz_wi
#define access_pmcntenset trap_raz_wi
#define access_pmcntenclr trap_raz_wi
#define access_pmovsr trap_raz_wi
#define access_pmselr trap_raz_wi
#define access_pmceid0 trap_raz_wi
#define access_pmceid1 trap_raz_wi
#define access_pmccntr trap_raz_wi
#define access_pmxevtyper trap_raz_wi
#define access_pmxevcntr trap_raz_wi
#define access_pmuserenr trap_raz_wi
#define access_pmintenset trap_raz_wi
#define access_pmintenclr trap_raz_wi

/* Architected CP15 registers.
 * CRn denotes the primary register number, but is copied to the CRm in the
 * user space API for 64-bit register access in line with the terminology used
 * in the ARM ARM.
 * Important: Must be sorted ascending by CRn, CRM, Op1, Op2 and with 64-bit
 *            registers preceding 32-bit ones.
 */
static const struct coproc_reg cp15_regs[] = {
	/* MPIDR: we use VMPIDR for guest access. */
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 5), is32,
			NULL, reset_mpidr, c0_MPIDR },

	/* CSSELR: swapped by interrupt.S. */
	{ CRn( 0), CRm( 0), Op1( 2), Op2( 0), is32,
			NULL, reset_unknown, c0_CSSELR },

	/* ACTLR: trapped by HCR.TAC bit. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 1), is32,
			access_actlr, reset_actlr, c1_ACTLR },

	/* CPACR: swapped by interrupt.S. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 2), is32,
			NULL, reset_val, c1_CPACR, 0x00000000 },

	/* TTBR0/TTBR1/TTBCR: swapped by interrupt.S. */
	{ CRm64( 2), Op1( 0), is64, access_vm_reg, reset_unknown64, c2_TTBR0 },
	{ CRn(2), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c2_TTBR0 },
	{ CRn(2), CRm( 0), Op1( 0), Op2( 1), is32,
			access_vm_reg, reset_unknown, c2_TTBR1 },
	{ CRn( 2), CRm( 0), Op1( 0), Op2( 2), is32,
			access_vm_reg, reset_val, c2_TTBCR, 0x00000000 },
	{ CRm64( 2), Op1( 1), is64, access_vm_reg, reset_unknown64, c2_TTBR1 },


	/* DACR: swapped by interrupt.S. */
	{ CRn( 3), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c3_DACR },

	/* DFSR/IFSR/ADFSR/AIFSR: swapped by interrupt.S. */
	{ CRn( 5), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c5_DFSR },
	{ CRn( 5), CRm( 0), Op1( 0), Op2( 1), is32,
			access_vm_reg, reset_unknown, c5_IFSR },
	{ CRn( 5), CRm( 1), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c5_ADFSR },
	{ CRn( 5), CRm( 1), Op1( 0), Op2( 1), is32,
			access_vm_reg, reset_unknown, c5_AIFSR },

	/* DFAR/IFAR: swapped by interrupt.S. */
	{ CRn( 6), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c6_DFAR },
	{ CRn( 6), CRm( 0), Op1( 0), Op2( 2), is32,
			access_vm_reg, reset_unknown, c6_IFAR },

	/* PAR swapped by interrupt.S */
	{ CRm64( 7), Op1( 0), is64, NULL, reset_unknown64, c7_PAR },

	/*
	 * DC{C,I,CI}SW operations:
	 */
	{ CRn( 7), CRm( 6), Op1( 0), Op2( 2), is32, access_dcsw},
	{ CRn( 7), CRm(10), Op1( 0), Op2( 2), is32, access_dcsw},
	{ CRn( 7), CRm(14), Op1( 0), Op2( 2), is32, access_dcsw},
	/*
	 * L2CTLR access (guest wants to know #CPUs).
	 */
	{ CRn( 9), CRm( 0), Op1( 1), Op2( 2), is32,
			access_l2ctlr, reset_l2ctlr, c9_L2CTLR },
	{ CRn( 9), CRm( 0), Op1( 1), Op2( 3), is32, access_l2ectlr},

	/*
	 * Dummy performance monitor implementation.
	 */
	{ CRn( 9), CRm(12), Op1( 0), Op2( 0), is32, access_pmcr},
	{ CRn( 9), CRm(12), Op1( 0), Op2( 1), is32, access_pmcntenset},
	{ CRn( 9), CRm(12), Op1( 0), Op2( 2), is32, access_pmcntenclr},
	{ CRn( 9), CRm(12), Op1( 0), Op2( 3), is32, access_pmovsr},
	{ CRn( 9), CRm(12), Op1( 0), Op2( 5), is32, access_pmselr},
	{ CRn( 9), CRm(12), Op1( 0), Op2( 6), is32, access_pmceid0},
	{ CRn( 9), CRm(12), Op1( 0), Op2( 7), is32, access_pmceid1},
	{ CRn( 9), CRm(13), Op1( 0), Op2( 0), is32, access_pmccntr},
	{ CRn( 9), CRm(13), Op1( 0), Op2( 1), is32, access_pmxevtyper},
	{ CRn( 9), CRm(13), Op1( 0), Op2( 2), is32, access_pmxevcntr},
	{ CRn( 9), CRm(14), Op1( 0), Op2( 0), is32, access_pmuserenr},
	{ CRn( 9), CRm(14), Op1( 0), Op2( 1), is32, access_pmintenset},
	{ CRn( 9), CRm(14), Op1( 0), Op2( 2), is32, access_pmintenclr},

	/* PRRR/NMRR (aka MAIR0/MAIR1): swapped by interrupt.S. */
	{ CRn(10), CRm( 2), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c10_PRRR},
	{ CRn(10), CRm( 2), Op1( 0), Op2( 1), is32,
			access_vm_reg, reset_unknown, c10_NMRR},

	/* AMAIR0/AMAIR1: swapped by interrupt.S. */
	{ CRn(10), CRm( 3), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_unknown, c10_AMAIR0},
	{ CRn(10), CRm( 3), Op1( 0), Op2( 1), is32,
			access_vm_reg, reset_unknown, c10_AMAIR1},

	/* ICC_SGI1R */
	{ CRm64(12), Op1( 0), is64, access_gic_sgi},

	/* VBAR: swapped by interrupt.S. */
	{ CRn(12), CRm( 0), Op1( 0), Op2( 0), is32,
			NULL, reset_val, c12_VBAR, 0x00000000 },

	/* ICC_ASGI1R */
	{ CRm64(12), Op1( 1), is64, access_gic_sgi},
	/* ICC_SGI0R */
	{ CRm64(12), Op1( 2), is64, access_gic_sgi},
	/* ICC_SRE */
	{ CRn(12), CRm(12), Op1( 0), Op2(5), is32, access_gic_sre },

	/* CONTEXTIDR/TPIDRURW/TPIDRURO/TPIDRPRW: swapped by interrupt.S. */
	{ CRn(13), CRm( 0), Op1( 0), Op2( 1), is32,
			access_vm_reg, reset_val, c13_CID, 0x00000000 },
	{ CRn(13), CRm( 0), Op1( 0), Op2( 2), is32,
			NULL, reset_unknown, c13_TID_URW },
	{ CRn(13), CRm( 0), Op1( 0), Op2( 3), is32,
			NULL, reset_unknown, c13_TID_URO },
	{ CRn(13), CRm( 0), Op1( 0), Op2( 4), is32,
			NULL, reset_unknown, c13_TID_PRIV },

	/* CNTP */
	{ CRm64(14), Op1( 2), is64, access_cntp_cval},

	/* CNTKCTL: swapped by interrupt.S. */
	{ CRn(14), CRm( 1), Op1( 0), Op2( 0), is32,
			NULL, reset_val, c14_CNTKCTL, 0x00000000 },

	/* CNTP */
	{ CRn(14), CRm( 2), Op1( 0), Op2( 0), is32, access_cntp_tval },
	{ CRn(14), CRm( 2), Op1( 0), Op2( 1), is32, access_cntp_ctl },

	/* The Configuration Base Address Register. */
	{ CRn(15), CRm( 0), Op1( 4), Op2( 0), is32, access_cbar},
};

static int check_reg_table(const struct coproc_reg *table, unsigned int n)
{
	unsigned int i;

	for (i = 1; i < n; i++) {
		if (cmp_reg(&table[i-1], &table[i]) >= 0) {
			kvm_err("reg table %p out of order (%d)\n", table, i - 1);
			return 1;
		}
	}

	return 0;
}

/* Target specific emulation tables */
static struct kvm_coproc_target_table *target_tables[KVM_ARM_NUM_TARGETS];

void kvm_register_target_coproc_table(struct kvm_coproc_target_table *table)
{
	BUG_ON(check_reg_table(table->table, table->num));
	target_tables[table->target] = table;
}

/* Get specific register table for this target. */
static const struct coproc_reg *get_target_table(unsigned target, size_t *num)
{
	struct kvm_coproc_target_table *table;

	table = target_tables[target];
	*num = table->num;
	return table->table;
}

#define reg_to_match_value(x)						\
	({								\
		unsigned long val;					\
		val  = (x)->CRn << 11;					\
		val |= (x)->CRm << 7;					\
		val |= (x)->Op1 << 4;					\
		val |= (x)->Op2 << 1;					\
		val |= !(x)->is_64bit;					\
		val;							\
	 })

static int match_reg(const void *key, const void *elt)
{
	const unsigned long pval = (unsigned long)key;
	const struct coproc_reg *r = elt;

	return pval - reg_to_match_value(r);
}

static const struct coproc_reg *find_reg(const struct coproc_params *params,
					 const struct coproc_reg table[],
					 unsigned int num)
{
	unsigned long pval = reg_to_match_value(params);

	return bsearch((void *)pval, table, num, sizeof(table[0]), match_reg);
}

static int emulate_cp15(struct kvm_vcpu *vcpu,
			const struct coproc_params *params)
{
	size_t num;
	const struct coproc_reg *table, *r;

	trace_kvm_emulate_cp15_imp(params->Op1, params->Rt1, params->CRn,
				   params->CRm, params->Op2, params->is_write);

	table = get_target_table(vcpu->arch.target, &num);

	/* Search target-specific then generic table. */
	r = find_reg(params, table, num);
	if (!r)
		r = find_reg(params, cp15_regs, ARRAY_SIZE(cp15_regs));

	if (likely(r)) {
		/* If we don't have an accessor, we should never get here! */
		BUG_ON(!r->access);

		if (likely(r->access(vcpu, params, r))) {
			/* Skip instruction, since it was emulated */
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
		}
	} else {
		/* If access function fails, it should complain. */
		kvm_err("Unsupported guest CP15 access at: %08lx\n",
			*vcpu_pc(vcpu));
		print_cp_instr(params);
		kvm_inject_undefined(vcpu);
	}

	return 1;
}

static struct coproc_params decode_64bit_hsr(struct kvm_vcpu *vcpu)
{
	struct coproc_params params;

	params.CRn = (kvm_vcpu_get_hsr(vcpu) >> 1) & 0xf;
	params.Rt1 = (kvm_vcpu_get_hsr(vcpu) >> 5) & 0xf;
	params.is_write = ((kvm_vcpu_get_hsr(vcpu) & 1) == 0);
	params.is_64bit = true;

	params.Op1 = (kvm_vcpu_get_hsr(vcpu) >> 16) & 0xf;
	params.Op2 = 0;
	params.Rt2 = (kvm_vcpu_get_hsr(vcpu) >> 10) & 0xf;
	params.CRm = 0;

	return params;
}

/**
 * kvm_handle_cp15_64 -- handles a mrrc/mcrr trap on a guest CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_cp15_64(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct coproc_params params = decode_64bit_hsr(vcpu);

	return emulate_cp15(vcpu, &params);
}

/**
 * kvm_handle_cp14_64 -- handles a mrrc/mcrr trap on a guest CP14 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_cp14_64(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct coproc_params params = decode_64bit_hsr(vcpu);

	/* raz_wi cp14 */
	trap_raz_wi(vcpu, &params, NULL);

	/* handled */
	kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
	return 1;
}

static void reset_coproc_regs(struct kvm_vcpu *vcpu,
			      const struct coproc_reg *table, size_t num)
{
	unsigned long i;

	for (i = 0; i < num; i++)
		if (table[i].reset)
			table[i].reset(vcpu, &table[i]);
}

static struct coproc_params decode_32bit_hsr(struct kvm_vcpu *vcpu)
{
	struct coproc_params params;

	params.CRm = (kvm_vcpu_get_hsr(vcpu) >> 1) & 0xf;
	params.Rt1 = (kvm_vcpu_get_hsr(vcpu) >> 5) & 0xf;
	params.is_write = ((kvm_vcpu_get_hsr(vcpu) & 1) == 0);
	params.is_64bit = false;

	params.CRn = (kvm_vcpu_get_hsr(vcpu) >> 10) & 0xf;
	params.Op1 = (kvm_vcpu_get_hsr(vcpu) >> 14) & 0x7;
	params.Op2 = (kvm_vcpu_get_hsr(vcpu) >> 17) & 0x7;
	params.Rt2 = 0;

	return params;
}

/**
 * kvm_handle_cp15_32 -- handles a mrc/mcr trap on a guest CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_cp15_32(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct coproc_params params = decode_32bit_hsr(vcpu);
	return emulate_cp15(vcpu, &params);
}

/**
 * kvm_handle_cp14_32 -- handles a mrc/mcr trap on a guest CP14 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_cp14_32(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct coproc_params params = decode_32bit_hsr(vcpu);

	/* raz_wi cp14 */
	trap_raz_wi(vcpu, &params, NULL);

	/* handled */
	kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
	return 1;
}

/******************************************************************************
 * Userspace API
 *****************************************************************************/

static bool index_to_params(u64 id, struct coproc_params *params)
{
	switch (id & KVM_REG_SIZE_MASK) {
	case KVM_REG_SIZE_U32:
		/* Any unused index bits means it's not valid. */
		if (id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK
			   | KVM_REG_ARM_COPROC_MASK
			   | KVM_REG_ARM_32_CRN_MASK
			   | KVM_REG_ARM_CRM_MASK
			   | KVM_REG_ARM_OPC1_MASK
			   | KVM_REG_ARM_32_OPC2_MASK))
			return false;

		params->is_64bit = false;
		params->CRn = ((id & KVM_REG_ARM_32_CRN_MASK)
			       >> KVM_REG_ARM_32_CRN_SHIFT);
		params->CRm = ((id & KVM_REG_ARM_CRM_MASK)
			       >> KVM_REG_ARM_CRM_SHIFT);
		params->Op1 = ((id & KVM_REG_ARM_OPC1_MASK)
			       >> KVM_REG_ARM_OPC1_SHIFT);
		params->Op2 = ((id & KVM_REG_ARM_32_OPC2_MASK)
			       >> KVM_REG_ARM_32_OPC2_SHIFT);
		return true;
	case KVM_REG_SIZE_U64:
		/* Any unused index bits means it's not valid. */
		if (id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK
			      | KVM_REG_ARM_COPROC_MASK
			      | KVM_REG_ARM_CRM_MASK
			      | KVM_REG_ARM_OPC1_MASK))
			return false;
		params->is_64bit = true;
		/* CRm to CRn: see cp15_to_index for details */
		params->CRn = ((id & KVM_REG_ARM_CRM_MASK)
			       >> KVM_REG_ARM_CRM_SHIFT);
		params->Op1 = ((id & KVM_REG_ARM_OPC1_MASK)
			       >> KVM_REG_ARM_OPC1_SHIFT);
		params->Op2 = 0;
		params->CRm = 0;
		return true;
	default:
		return false;
	}
}

/* Decode an index value, and find the cp15 coproc_reg entry. */
static const struct coproc_reg *index_to_coproc_reg(struct kvm_vcpu *vcpu,
						    u64 id)
{
	size_t num;
	const struct coproc_reg *table, *r;
	struct coproc_params params;

	/* We only do cp15 for now. */
	if ((id & KVM_REG_ARM_COPROC_MASK) >> KVM_REG_ARM_COPROC_SHIFT != 15)
		return NULL;

	if (!index_to_params(id, &params))
		return NULL;

	table = get_target_table(vcpu->arch.target, &num);
	r = find_reg(&params, table, num);
	if (!r)
		r = find_reg(&params, cp15_regs, ARRAY_SIZE(cp15_regs));

	/* Not saved in the cp15 array? */
	if (r && !r->reg)
		r = NULL;

	return r;
}

/*
 * These are the invariant cp15 registers: we let the guest see the host
 * versions of these, so they're part of the guest state.
 *
 * A future CPU may provide a mechanism to present different values to
 * the guest, or a future kvm may trap them.
 */
/* Unfortunately, there's no register-argument for mrc, so generate. */
#define FUNCTION_FOR32(crn, crm, op1, op2, name)			\
	static void get_##name(struct kvm_vcpu *v,			\
			       const struct coproc_reg *r)		\
	{								\
		u32 val;						\
									\
		asm volatile("mrc p15, " __stringify(op1)		\
			     ", %0, c" __stringify(crn)			\
			     ", c" __stringify(crm)			\
			     ", " __stringify(op2) "\n" : "=r" (val));	\
		((struct coproc_reg *)r)->val = val;			\
	}

FUNCTION_FOR32(0, 0, 0, 0, MIDR)
FUNCTION_FOR32(0, 0, 0, 1, CTR)
FUNCTION_FOR32(0, 0, 0, 2, TCMTR)
FUNCTION_FOR32(0, 0, 0, 3, TLBTR)
FUNCTION_FOR32(0, 0, 0, 6, REVIDR)
FUNCTION_FOR32(0, 1, 0, 0, ID_PFR0)
FUNCTION_FOR32(0, 1, 0, 1, ID_PFR1)
FUNCTION_FOR32(0, 1, 0, 2, ID_DFR0)
FUNCTION_FOR32(0, 1, 0, 3, ID_AFR0)
FUNCTION_FOR32(0, 1, 0, 4, ID_MMFR0)
FUNCTION_FOR32(0, 1, 0, 5, ID_MMFR1)
FUNCTION_FOR32(0, 1, 0, 6, ID_MMFR2)
FUNCTION_FOR32(0, 1, 0, 7, ID_MMFR3)
FUNCTION_FOR32(0, 2, 0, 0, ID_ISAR0)
FUNCTION_FOR32(0, 2, 0, 1, ID_ISAR1)
FUNCTION_FOR32(0, 2, 0, 2, ID_ISAR2)
FUNCTION_FOR32(0, 2, 0, 3, ID_ISAR3)
FUNCTION_FOR32(0, 2, 0, 4, ID_ISAR4)
FUNCTION_FOR32(0, 2, 0, 5, ID_ISAR5)
FUNCTION_FOR32(0, 0, 1, 1, CLIDR)
FUNCTION_FOR32(0, 0, 1, 7, AIDR)

/* ->val is filled in by kvm_invariant_coproc_table_init() */
static struct coproc_reg invariant_cp15[] = {
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 0), is32, NULL, get_MIDR },
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 1), is32, NULL, get_CTR },
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 2), is32, NULL, get_TCMTR },
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 3), is32, NULL, get_TLBTR },
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 6), is32, NULL, get_REVIDR },

	{ CRn( 0), CRm( 0), Op1( 1), Op2( 1), is32, NULL, get_CLIDR },
	{ CRn( 0), CRm( 0), Op1( 1), Op2( 7), is32, NULL, get_AIDR },

	{ CRn( 0), CRm( 1), Op1( 0), Op2( 0), is32, NULL, get_ID_PFR0 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 1), is32, NULL, get_ID_PFR1 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 2), is32, NULL, get_ID_DFR0 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 3), is32, NULL, get_ID_AFR0 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 4), is32, NULL, get_ID_MMFR0 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 5), is32, NULL, get_ID_MMFR1 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 6), is32, NULL, get_ID_MMFR2 },
	{ CRn( 0), CRm( 1), Op1( 0), Op2( 7), is32, NULL, get_ID_MMFR3 },

	{ CRn( 0), CRm( 2), Op1( 0), Op2( 0), is32, NULL, get_ID_ISAR0 },
	{ CRn( 0), CRm( 2), Op1( 0), Op2( 1), is32, NULL, get_ID_ISAR1 },
	{ CRn( 0), CRm( 2), Op1( 0), Op2( 2), is32, NULL, get_ID_ISAR2 },
	{ CRn( 0), CRm( 2), Op1( 0), Op2( 3), is32, NULL, get_ID_ISAR3 },
	{ CRn( 0), CRm( 2), Op1( 0), Op2( 4), is32, NULL, get_ID_ISAR4 },
	{ CRn( 0), CRm( 2), Op1( 0), Op2( 5), is32, NULL, get_ID_ISAR5 },
};

/*
 * Reads a register value from a userspace address to a kernel
 * variable. Make sure that register size matches sizeof(*__val).
 */
static int reg_from_user(void *val, const void __user *uaddr, u64 id)
{
	if (copy_from_user(val, uaddr, KVM_REG_SIZE(id)) != 0)
		return -EFAULT;
	return 0;
}

/*
 * Writes a register value to a userspace address from a kernel variable.
 * Make sure that register size matches sizeof(*__val).
 */
static int reg_to_user(void __user *uaddr, const void *val, u64 id)
{
	if (copy_to_user(uaddr, val, KVM_REG_SIZE(id)) != 0)
		return -EFAULT;
	return 0;
}

static int get_invariant_cp15(u64 id, void __user *uaddr)
{
	struct coproc_params params;
	const struct coproc_reg *r;
	int ret;

	if (!index_to_params(id, &params))
		return -ENOENT;

	r = find_reg(&params, invariant_cp15, ARRAY_SIZE(invariant_cp15));
	if (!r)
		return -ENOENT;

	ret = -ENOENT;
	if (KVM_REG_SIZE(id) == 4) {
		u32 val = r->val;

		ret = reg_to_user(uaddr, &val, id);
	} else if (KVM_REG_SIZE(id) == 8) {
		ret = reg_to_user(uaddr, &r->val, id);
	}
	return ret;
}

static int set_invariant_cp15(u64 id, void __user *uaddr)
{
	struct coproc_params params;
	const struct coproc_reg *r;
	int err;
	u64 val;

	if (!index_to_params(id, &params))
		return -ENOENT;
	r = find_reg(&params, invariant_cp15, ARRAY_SIZE(invariant_cp15));
	if (!r)
		return -ENOENT;

	err = -ENOENT;
	if (KVM_REG_SIZE(id) == 4) {
		u32 val32;

		err = reg_from_user(&val32, uaddr, id);
		if (!err)
			val = val32;
	} else if (KVM_REG_SIZE(id) == 8) {
		err = reg_from_user(&val, uaddr, id);
	}
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
		return false;

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

/* Which cache CCSIDR represents depends on CSSELR value. */
static u32 get_ccsidr(u32 csselr)
{
	u32 ccsidr;

	/* Make sure noone else changes CSSELR during this! */
	local_irq_disable();
	/* Put value into CSSELR */
	asm volatile("mcr p15, 2, %0, c0, c0, 0" : : "r" (csselr));
	isb();
	/* Read result out of CCSIDR */
	asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r" (ccsidr));
	local_irq_enable();

	return ccsidr;
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

#ifdef CONFIG_VFPv3
static const int vfp_sysregs[] = { KVM_REG_ARM_VFP_FPEXC,
				   KVM_REG_ARM_VFP_FPSCR,
				   KVM_REG_ARM_VFP_FPINST,
				   KVM_REG_ARM_VFP_FPINST2,
				   KVM_REG_ARM_VFP_MVFR0,
				   KVM_REG_ARM_VFP_MVFR1,
				   KVM_REG_ARM_VFP_FPSID };

static unsigned int num_fp_regs(void)
{
	if (((fmrx(MVFR0) & MVFR0_A_SIMD_MASK) >> MVFR0_A_SIMD_BIT) == 2)
		return 32;
	else
		return 16;
}

static unsigned int num_vfp_regs(void)
{
	/* Normal FP regs + control regs. */
	return num_fp_regs() + ARRAY_SIZE(vfp_sysregs);
}

static int copy_vfp_regids(u64 __user *uindices)
{
	unsigned int i;
	const u64 u32reg = KVM_REG_ARM | KVM_REG_SIZE_U32 | KVM_REG_ARM_VFP;
	const u64 u64reg = KVM_REG_ARM | KVM_REG_SIZE_U64 | KVM_REG_ARM_VFP;

	for (i = 0; i < num_fp_regs(); i++) {
		if (put_user((u64reg | KVM_REG_ARM_VFP_BASE_REG) + i,
			     uindices))
			return -EFAULT;
		uindices++;
	}

	for (i = 0; i < ARRAY_SIZE(vfp_sysregs); i++) {
		if (put_user(u32reg | vfp_sysregs[i], uindices))
			return -EFAULT;
		uindices++;
	}

	return num_vfp_regs();
}

static int vfp_get_reg(const struct kvm_vcpu *vcpu, u64 id, void __user *uaddr)
{
	u32 vfpid = (id & KVM_REG_ARM_VFP_MASK);
	u32 val;

	/* Fail if we have unknown bits set. */
	if (id & ~(KVM_REG_ARCH_MASK|KVM_REG_SIZE_MASK|KVM_REG_ARM_COPROC_MASK
		   | ((1 << KVM_REG_ARM_COPROC_SHIFT)-1)))
		return -ENOENT;

	if (vfpid < num_fp_regs()) {
		if (KVM_REG_SIZE(id) != 8)
			return -ENOENT;
		return reg_to_user(uaddr, &vcpu->arch.ctxt.vfp.fpregs[vfpid],
				   id);
	}

	/* FP control registers are all 32 bit. */
	if (KVM_REG_SIZE(id) != 4)
		return -ENOENT;

	switch (vfpid) {
	case KVM_REG_ARM_VFP_FPEXC:
		return reg_to_user(uaddr, &vcpu->arch.ctxt.vfp.fpexc, id);
	case KVM_REG_ARM_VFP_FPSCR:
		return reg_to_user(uaddr, &vcpu->arch.ctxt.vfp.fpscr, id);
	case KVM_REG_ARM_VFP_FPINST:
		return reg_to_user(uaddr, &vcpu->arch.ctxt.vfp.fpinst, id);
	case KVM_REG_ARM_VFP_FPINST2:
		return reg_to_user(uaddr, &vcpu->arch.ctxt.vfp.fpinst2, id);
	case KVM_REG_ARM_VFP_MVFR0:
		val = fmrx(MVFR0);
		return reg_to_user(uaddr, &val, id);
	case KVM_REG_ARM_VFP_MVFR1:
		val = fmrx(MVFR1);
		return reg_to_user(uaddr, &val, id);
	case KVM_REG_ARM_VFP_FPSID:
		val = fmrx(FPSID);
		return reg_to_user(uaddr, &val, id);
	default:
		return -ENOENT;
	}
}

static int vfp_set_reg(struct kvm_vcpu *vcpu, u64 id, const void __user *uaddr)
{
	u32 vfpid = (id & KVM_REG_ARM_VFP_MASK);
	u32 val;

	/* Fail if we have unknown bits set. */
	if (id & ~(KVM_REG_ARCH_MASK|KVM_REG_SIZE_MASK|KVM_REG_ARM_COPROC_MASK
		   | ((1 << KVM_REG_ARM_COPROC_SHIFT)-1)))
		return -ENOENT;

	if (vfpid < num_fp_regs()) {
		if (KVM_REG_SIZE(id) != 8)
			return -ENOENT;
		return reg_from_user(&vcpu->arch.ctxt.vfp.fpregs[vfpid],
				     uaddr, id);
	}

	/* FP control registers are all 32 bit. */
	if (KVM_REG_SIZE(id) != 4)
		return -ENOENT;

	switch (vfpid) {
	case KVM_REG_ARM_VFP_FPEXC:
		return reg_from_user(&vcpu->arch.ctxt.vfp.fpexc, uaddr, id);
	case KVM_REG_ARM_VFP_FPSCR:
		return reg_from_user(&vcpu->arch.ctxt.vfp.fpscr, uaddr, id);
	case KVM_REG_ARM_VFP_FPINST:
		return reg_from_user(&vcpu->arch.ctxt.vfp.fpinst, uaddr, id);
	case KVM_REG_ARM_VFP_FPINST2:
		return reg_from_user(&vcpu->arch.ctxt.vfp.fpinst2, uaddr, id);
	/* These are invariant. */
	case KVM_REG_ARM_VFP_MVFR0:
		if (reg_from_user(&val, uaddr, id))
			return -EFAULT;
		if (val != fmrx(MVFR0))
			return -EINVAL;
		return 0;
	case KVM_REG_ARM_VFP_MVFR1:
		if (reg_from_user(&val, uaddr, id))
			return -EFAULT;
		if (val != fmrx(MVFR1))
			return -EINVAL;
		return 0;
	case KVM_REG_ARM_VFP_FPSID:
		if (reg_from_user(&val, uaddr, id))
			return -EFAULT;
		if (val != fmrx(FPSID))
			return -EINVAL;
		return 0;
	default:
		return -ENOENT;
	}
}
#else /* !CONFIG_VFPv3 */
static unsigned int num_vfp_regs(void)
{
	return 0;
}

static int copy_vfp_regids(u64 __user *uindices)
{
	return 0;
}

static int vfp_get_reg(const struct kvm_vcpu *vcpu, u64 id, void __user *uaddr)
{
	return -ENOENT;
}

static int vfp_set_reg(struct kvm_vcpu *vcpu, u64 id, const void __user *uaddr)
{
	return -ENOENT;
}
#endif /* !CONFIG_VFPv3 */

int kvm_arm_coproc_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	const struct coproc_reg *r;
	void __user *uaddr = (void __user *)(long)reg->addr;
	int ret;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_get(reg->id, uaddr);

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_VFP)
		return vfp_get_reg(vcpu, reg->id, uaddr);

	r = index_to_coproc_reg(vcpu, reg->id);
	if (!r)
		return get_invariant_cp15(reg->id, uaddr);

	ret = -ENOENT;
	if (KVM_REG_SIZE(reg->id) == 8) {
		u64 val;

		val = vcpu_cp15_reg64_get(vcpu, r);
		ret = reg_to_user(uaddr, &val, reg->id);
	} else if (KVM_REG_SIZE(reg->id) == 4) {
		ret = reg_to_user(uaddr, &vcpu_cp15(vcpu, r->reg), reg->id);
	}

	return ret;
}

int kvm_arm_coproc_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	const struct coproc_reg *r;
	void __user *uaddr = (void __user *)(long)reg->addr;
	int ret;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_set(reg->id, uaddr);

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_VFP)
		return vfp_set_reg(vcpu, reg->id, uaddr);

	r = index_to_coproc_reg(vcpu, reg->id);
	if (!r)
		return set_invariant_cp15(reg->id, uaddr);

	ret = -ENOENT;
	if (KVM_REG_SIZE(reg->id) == 8) {
		u64 val;

		ret = reg_from_user(&val, uaddr, reg->id);
		if (!ret)
			vcpu_cp15_reg64_set(vcpu, r, val);
	} else if (KVM_REG_SIZE(reg->id) == 4) {
		ret = reg_from_user(&vcpu_cp15(vcpu, r->reg), uaddr, reg->id);
	}

	return ret;
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

static u64 cp15_to_index(const struct coproc_reg *reg)
{
	u64 val = KVM_REG_ARM | (15 << KVM_REG_ARM_COPROC_SHIFT);
	if (reg->is_64bit) {
		val |= KVM_REG_SIZE_U64;
		val |= (reg->Op1 << KVM_REG_ARM_OPC1_SHIFT);
		/*
		 * CRn always denotes the primary coproc. reg. nr. for the
		 * in-kernel representation, but the user space API uses the
		 * CRm for the encoding, because it is modelled after the
		 * MRRC/MCRR instructions: see the ARM ARM rev. c page
		 * B3-1445
		 */
		val |= (reg->CRn << KVM_REG_ARM_CRM_SHIFT);
	} else {
		val |= KVM_REG_SIZE_U32;
		val |= (reg->Op1 << KVM_REG_ARM_OPC1_SHIFT);
		val |= (reg->Op2 << KVM_REG_ARM_32_OPC2_SHIFT);
		val |= (reg->CRm << KVM_REG_ARM_CRM_SHIFT);
		val |= (reg->CRn << KVM_REG_ARM_32_CRN_SHIFT);
	}
	return val;
}

static bool copy_reg_to_user(const struct coproc_reg *reg, u64 __user **uind)
{
	if (!*uind)
		return true;

	if (put_user(cp15_to_index(reg), *uind))
		return false;

	(*uind)++;
	return true;
}

/* Assumed ordered tables, see kvm_coproc_table_init. */
static int walk_cp15(struct kvm_vcpu *vcpu, u64 __user *uind)
{
	const struct coproc_reg *i1, *i2, *end1, *end2;
	unsigned int total = 0;
	size_t num;

	/* We check for duplicates here, to allow arch-specific overrides. */
	i1 = get_target_table(vcpu->arch.target, &num);
	end1 = i1 + num;
	i2 = cp15_regs;
	end2 = cp15_regs + ARRAY_SIZE(cp15_regs);

	BUG_ON(i1 == end1 || i2 == end2);

	/* Walk carefully, as both tables may refer to the same register. */
	while (i1 || i2) {
		int cmp = cmp_reg(i1, i2);
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

unsigned long kvm_arm_num_coproc_regs(struct kvm_vcpu *vcpu)
{
	return ARRAY_SIZE(invariant_cp15)
		+ num_demux_regs()
		+ num_vfp_regs()
		+ walk_cp15(vcpu, (u64 __user *)NULL);
}

int kvm_arm_copy_coproc_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	unsigned int i;
	int err;

	/* Then give them all the invariant registers' indices. */
	for (i = 0; i < ARRAY_SIZE(invariant_cp15); i++) {
		if (put_user(cp15_to_index(&invariant_cp15[i]), uindices))
			return -EFAULT;
		uindices++;
	}

	err = walk_cp15(vcpu, uindices);
	if (err < 0)
		return err;
	uindices += err;

	err = copy_vfp_regids(uindices);
	if (err < 0)
		return err;
	uindices += err;

	return write_demux_regids(uindices);
}

void kvm_coproc_table_init(void)
{
	unsigned int i;

	/* Make sure tables are unique and in order. */
	BUG_ON(check_reg_table(cp15_regs, ARRAY_SIZE(cp15_regs)));
	BUG_ON(check_reg_table(invariant_cp15, ARRAY_SIZE(invariant_cp15)));

	/* We abuse the reset function to overwrite the table itself. */
	for (i = 0; i < ARRAY_SIZE(invariant_cp15); i++)
		invariant_cp15[i].reset(NULL, &invariant_cp15[i]);

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
	asm volatile("mrc p15, 1, %0, c0, c0, 1" : "=r" (cache_levels));
	for (i = 0; i < 7; i++)
		if (((cache_levels >> (i*3)) & 7) == 0)
			break;
	/* Clear all higher bits. */
	cache_levels &= (1 << (i*3))-1;
}

/**
 * kvm_reset_coprocs - sets cp15 registers to reset value
 * @vcpu: The VCPU pointer
 *
 * This function finds the right table above and sets the registers on the
 * virtual CPU struct to their architecturally defined reset values.
 */
void kvm_reset_coprocs(struct kvm_vcpu *vcpu)
{
	size_t num;
	const struct coproc_reg *table;

	/* Catch someone adding a register without putting in reset entry. */
	memset(vcpu->arch.ctxt.cp15, 0x42, sizeof(vcpu->arch.ctxt.cp15));

	/* Generic chip reset first (so target could override). */
	reset_coproc_regs(vcpu, cp15_regs, ARRAY_SIZE(cp15_regs));

	table = get_target_table(vcpu->arch.target, &num);
	reset_coproc_regs(vcpu, table, num);

	for (num = 1; num < NR_CP15_REGS; num++)
		WARN(vcpu_cp15(vcpu, num) == 0x42424242,
		     "Didn't reset vcpu_cp15(vcpu, %zi)", num);
}
