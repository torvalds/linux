/*
 * vMTRR implementation
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 * Copyright(C) 2015 Intel Corporation.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *   Marcelo Tosatti <mtosatti@redhat.com>
 *   Paolo Bonzini <pbonzini@redhat.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/kvm_host.h>
#include <asm/mtrr.h>

#include "cpuid.h"
#include "mmu.h"

#define IA32_MTRR_DEF_TYPE_E		(1ULL << 11)
#define IA32_MTRR_DEF_TYPE_FE		(1ULL << 10)
#define IA32_MTRR_DEF_TYPE_TYPE_MASK	(0xff)

static bool msr_mtrr_valid(unsigned msr)
{
	switch (msr) {
	case 0x200 ... 0x200 + 2 * KVM_NR_VAR_MTRR - 1:
	case MSR_MTRRfix64K_00000:
	case MSR_MTRRfix16K_80000:
	case MSR_MTRRfix16K_A0000:
	case MSR_MTRRfix4K_C0000:
	case MSR_MTRRfix4K_C8000:
	case MSR_MTRRfix4K_D0000:
	case MSR_MTRRfix4K_D8000:
	case MSR_MTRRfix4K_E0000:
	case MSR_MTRRfix4K_E8000:
	case MSR_MTRRfix4K_F0000:
	case MSR_MTRRfix4K_F8000:
	case MSR_MTRRdefType:
	case MSR_IA32_CR_PAT:
		return true;
	case 0x2f8:
		return true;
	}
	return false;
}

static bool valid_pat_type(unsigned t)
{
	return t < 8 && (1 << t) & 0xf3; /* 0, 1, 4, 5, 6, 7 */
}

static bool valid_mtrr_type(unsigned t)
{
	return t < 8 && (1 << t) & 0x73; /* 0, 1, 4, 5, 6 */
}

bool kvm_mtrr_valid(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	int i;
	u64 mask;

	if (!msr_mtrr_valid(msr))
		return false;

	if (msr == MSR_IA32_CR_PAT) {
		for (i = 0; i < 8; i++)
			if (!valid_pat_type((data >> (i * 8)) & 0xff))
				return false;
		return true;
	} else if (msr == MSR_MTRRdefType) {
		if (data & ~0xcff)
			return false;
		return valid_mtrr_type(data & 0xff);
	} else if (msr >= MSR_MTRRfix64K_00000 && msr <= MSR_MTRRfix4K_F8000) {
		for (i = 0; i < 8 ; i++)
			if (!valid_mtrr_type((data >> (i * 8)) & 0xff))
				return false;
		return true;
	}

	/* variable MTRRs */
	WARN_ON(!(msr >= 0x200 && msr < 0x200 + 2 * KVM_NR_VAR_MTRR));

	mask = (~0ULL) << cpuid_maxphyaddr(vcpu);
	if ((msr & 1) == 0) {
		/* MTRR base */
		if (!valid_mtrr_type(data & 0xff))
			return false;
		mask |= 0xf00;
	} else
		/* MTRR mask */
		mask |= 0x7ff;
	if (data & mask) {
		kvm_inject_gp(vcpu, 0);
		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(kvm_mtrr_valid);

static bool mtrr_is_enabled(struct kvm_mtrr *mtrr_state)
{
	return !!(mtrr_state->deftype & IA32_MTRR_DEF_TYPE_E);
}

static bool fixed_mtrr_is_enabled(struct kvm_mtrr *mtrr_state)
{
	return !!(mtrr_state->deftype & IA32_MTRR_DEF_TYPE_FE);
}

static u8 mtrr_default_type(struct kvm_mtrr *mtrr_state)
{
	return mtrr_state->deftype & IA32_MTRR_DEF_TYPE_TYPE_MASK;
}

/*
* Three terms are used in the following code:
* - segment, it indicates the address segments covered by fixed MTRRs.
* - unit, it corresponds to the MSR entry in the segment.
* - range, a range is covered in one memory cache type.
*/
struct fixed_mtrr_segment {
	u64 start;
	u64 end;

	int range_shift;

	/* the start position in kvm_mtrr.fixed_ranges[]. */
	int range_start;
};

static struct fixed_mtrr_segment fixed_seg_table[] = {
	/* MSR_MTRRfix64K_00000, 1 unit. 64K fixed mtrr. */
	{
		.start = 0x0,
		.end = 0x80000,
		.range_shift = 16, /* 64K */
		.range_start = 0,
	},

	/*
	 * MSR_MTRRfix16K_80000 ... MSR_MTRRfix16K_A0000, 2 units,
	 * 16K fixed mtrr.
	 */
	{
		.start = 0x80000,
		.end = 0xc0000,
		.range_shift = 14, /* 16K */
		.range_start = 8,
	},

	/*
	 * MSR_MTRRfix4K_C0000 ... MSR_MTRRfix4K_F8000, 8 units,
	 * 4K fixed mtrr.
	 */
	{
		.start = 0xc0000,
		.end = 0x100000,
		.range_shift = 12, /* 12K */
		.range_start = 24,
	}
};

/*
 * The size of unit is covered in one MSR, one MSR entry contains
 * 8 ranges so that unit size is always 8 * 2^range_shift.
 */
static u64 fixed_mtrr_seg_unit_size(int seg)
{
	return 8 << fixed_seg_table[seg].range_shift;
}

static bool fixed_msr_to_seg_unit(u32 msr, int *seg, int *unit)
{
	switch (msr) {
	case MSR_MTRRfix64K_00000:
		*seg = 0;
		*unit = 0;
		break;
	case MSR_MTRRfix16K_80000 ... MSR_MTRRfix16K_A0000:
		*seg = 1;
		*unit = msr - MSR_MTRRfix16K_80000;
		break;
	case MSR_MTRRfix4K_C0000 ... MSR_MTRRfix4K_F8000:
		*seg = 2;
		*unit = msr - MSR_MTRRfix4K_C0000;
		break;
	default:
		return false;
	}

	return true;
}

static void fixed_mtrr_seg_unit_range(int seg, int unit, u64 *start, u64 *end)
{
	struct fixed_mtrr_segment *mtrr_seg = &fixed_seg_table[seg];
	u64 unit_size = fixed_mtrr_seg_unit_size(seg);

	*start = mtrr_seg->start + unit * unit_size;
	*end = *start + unit_size;
	WARN_ON(*end > mtrr_seg->end);
}

static int fixed_mtrr_seg_unit_range_index(int seg, int unit)
{
	struct fixed_mtrr_segment *mtrr_seg = &fixed_seg_table[seg];

	WARN_ON(mtrr_seg->start + unit * fixed_mtrr_seg_unit_size(seg)
		> mtrr_seg->end);

	/* each unit has 8 ranges. */
	return mtrr_seg->range_start + 8 * unit;
}

static bool fixed_msr_to_range(u32 msr, u64 *start, u64 *end)
{
	int seg, unit;

	if (!fixed_msr_to_seg_unit(msr, &seg, &unit))
		return false;

	fixed_mtrr_seg_unit_range(seg, unit, start, end);
	return true;
}

static int fixed_msr_to_range_index(u32 msr)
{
	int seg, unit;

	if (!fixed_msr_to_seg_unit(msr, &seg, &unit))
		return -1;

	return fixed_mtrr_seg_unit_range_index(seg, unit);
}

static void update_mtrr(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	gfn_t start, end, mask;
	int index;

	if (msr == MSR_IA32_CR_PAT || !tdp_enabled ||
	      !kvm_arch_has_noncoherent_dma(vcpu->kvm))
		return;

	if (!mtrr_is_enabled(mtrr_state) && msr != MSR_MTRRdefType)
		return;

	/* fixed MTRRs. */
	if (fixed_msr_to_range(msr, &start, &end)) {
		if (!fixed_mtrr_is_enabled(mtrr_state))
			return;
	} else if (msr == MSR_MTRRdefType) {
		start = 0x0;
		end = ~0ULL;
	} else {
		/* variable range MTRRs. */
		index = (msr - 0x200) / 2;
		start = mtrr_state->var_ranges[index].base & PAGE_MASK;
		mask = mtrr_state->var_ranges[index].mask & PAGE_MASK;
		mask |= ~0ULL << cpuid_maxphyaddr(vcpu);

		end = ((start & mask) | ~mask) + 1;
	}

	kvm_zap_gfn_range(vcpu->kvm, gpa_to_gfn(start), gpa_to_gfn(end));
}

int kvm_mtrr_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	int index;

	if (!kvm_mtrr_valid(vcpu, msr, data))
		return 1;

	index = fixed_msr_to_range_index(msr);
	if (index >= 0)
		*(u64 *)&vcpu->arch.mtrr_state.fixed_ranges[index] = data;
	else if (msr == MSR_MTRRdefType)
		vcpu->arch.mtrr_state.deftype = data;
	else if (msr == MSR_IA32_CR_PAT)
		vcpu->arch.pat = data;
	else {	/* Variable MTRRs */
		int is_mtrr_mask;

		index = (msr - 0x200) / 2;
		is_mtrr_mask = msr - 0x200 - 2 * index;
		if (!is_mtrr_mask)
			vcpu->arch.mtrr_state.var_ranges[index].base = data;
		else
			vcpu->arch.mtrr_state.var_ranges[index].mask = data;
	}

	update_mtrr(vcpu, msr);
	return 0;
}

int kvm_mtrr_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	int index;

	/* MSR_MTRRcap is a readonly MSR. */
	if (msr == MSR_MTRRcap) {
		/*
		 * SMRR = 0
		 * WC = 1
		 * FIX = 1
		 * VCNT = KVM_NR_VAR_MTRR
		 */
		*pdata = 0x500 | KVM_NR_VAR_MTRR;
		return 0;
	}

	if (!msr_mtrr_valid(msr))
		return 1;

	index = fixed_msr_to_range_index(msr);
	if (index >= 0)
		*pdata = *(u64 *)&vcpu->arch.mtrr_state.fixed_ranges[index];
	else if (msr == MSR_MTRRdefType)
		*pdata = vcpu->arch.mtrr_state.deftype;
	else if (msr == MSR_IA32_CR_PAT)
		*pdata = vcpu->arch.pat;
	else {	/* Variable MTRRs */
		int is_mtrr_mask;

		index = (msr - 0x200) / 2;
		is_mtrr_mask = msr - 0x200 - 2 * index;
		if (!is_mtrr_mask)
			*pdata = vcpu->arch.mtrr_state.var_ranges[index].base;
		else
			*pdata = vcpu->arch.mtrr_state.var_ranges[index].mask;
	}

	return 0;
}

u8 kvm_mtrr_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	u64 base, mask, start;
	int i, num_var_ranges, type;
	const int wt_wb_mask = (1 << MTRR_TYPE_WRBACK)
			       | (1 << MTRR_TYPE_WRTHROUGH);

	start = gfn_to_gpa(gfn);
	num_var_ranges = KVM_NR_VAR_MTRR;
	type = -1;

	/* MTRR is completely disabled, use UC for all of physical memory. */
	if (!mtrr_is_enabled(mtrr_state))
		return MTRR_TYPE_UNCACHABLE;

	/* Look in fixed ranges. Just return the type as per start */
	if (fixed_mtrr_is_enabled(mtrr_state) && (start < 0x100000)) {
		int idx;

		if (start < 0x80000) {
			idx = 0;
			idx += (start >> 16);
			return mtrr_state->fixed_ranges[idx];
		} else if (start < 0xC0000) {
			idx = 1 * 8;
			idx += ((start - 0x80000) >> 14);
			return mtrr_state->fixed_ranges[idx];
		} else if (start < 0x1000000) {
			idx = 3 * 8;
			idx += ((start - 0xC0000) >> 12);
			return mtrr_state->fixed_ranges[idx];
		}
	}

	/*
	 * Look in variable ranges
	 * Look of multiple ranges matching this address and pick type
	 * as per MTRR precedence
	 */
	for (i = 0; i < num_var_ranges; ++i) {
		int curr_type;

		if (!(mtrr_state->var_ranges[i].mask & (1 << 11)))
			continue;

		base = mtrr_state->var_ranges[i].base & PAGE_MASK;
		mask = mtrr_state->var_ranges[i].mask & PAGE_MASK;

		if ((start & mask) != (base & mask))
			continue;

		/*
		 * Please refer to Intel SDM Volume 3: 11.11.4.1 MTRR
		 * Precedences.
		 */

		curr_type = mtrr_state->var_ranges[i].base & 0xff;
		if (type == -1) {
			type = curr_type;
			continue;
		}

		/*
		 * If two or more variable memory ranges match and the
		 * memory types are identical, then that memory type is
		 * used.
		 */
		if (type == curr_type)
			continue;

		/*
		 * If two or more variable memory ranges match and one of
		 * the memory types is UC, the UC memory type used.
		 */
		if (curr_type == MTRR_TYPE_UNCACHABLE)
			return MTRR_TYPE_UNCACHABLE;

		/*
		 * If two or more variable memory ranges match and the
		 * memory types are WT and WB, the WT memory type is used.
		 */
		if (((1 << type) & wt_wb_mask) &&
		      ((1 << curr_type) & wt_wb_mask)) {
			type = MTRR_TYPE_WRTHROUGH;
			continue;
		}

		/*
		 * For overlaps not defined by the above rules, processor
		 * behavior is undefined.
		 */

		/* We use WB for this undefined behavior. :( */
		return MTRR_TYPE_WRBACK;
	}

	if (type != -1)
		return type;

	return mtrr_default_type(mtrr_state);
}
EXPORT_SYMBOL_GPL(kvm_mtrr_get_guest_memory_type);
