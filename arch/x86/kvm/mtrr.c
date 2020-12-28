// SPDX-License-Identifier: GPL-2.0-only
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
	}
	return false;
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
		return kvm_pat_valid(data);
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

	return (data & mask) == 0;
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

static u8 mtrr_disabled_type(struct kvm_vcpu *vcpu)
{
	/*
	 * Intel SDM 11.11.2.2: all MTRRs are disabled when
	 * IA32_MTRR_DEF_TYPE.E bit is cleared, and the UC
	 * memory type is applied to all of physical memory.
	 *
	 * However, virtual machines can be run with CPUID such that
	 * there are no MTRRs.  In that case, the firmware will never
	 * enable MTRRs and it is obviously undesirable to run the
	 * guest entirely with UC memory and we use WB.
	 */
	if (guest_cpuid_has(vcpu, X86_FEATURE_MTRR))
		return MTRR_TYPE_UNCACHABLE;
	else
		return MTRR_TYPE_WRBACK;
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
		*unit = array_index_nospec(
			msr - MSR_MTRRfix16K_80000,
			MSR_MTRRfix16K_A0000 - MSR_MTRRfix16K_80000 + 1);
		break;
	case MSR_MTRRfix4K_C0000 ... MSR_MTRRfix4K_F8000:
		*seg = 2;
		*unit = array_index_nospec(
			msr - MSR_MTRRfix4K_C0000,
			MSR_MTRRfix4K_F8000 - MSR_MTRRfix4K_C0000 + 1);
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

static int fixed_mtrr_seg_end_range_index(int seg)
{
	struct fixed_mtrr_segment *mtrr_seg = &fixed_seg_table[seg];
	int n;

	n = (mtrr_seg->end - mtrr_seg->start) >> mtrr_seg->range_shift;
	return mtrr_seg->range_start + n - 1;
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

static int fixed_mtrr_addr_to_seg(u64 addr)
{
	struct fixed_mtrr_segment *mtrr_seg;
	int seg, seg_num = ARRAY_SIZE(fixed_seg_table);

	for (seg = 0; seg < seg_num; seg++) {
		mtrr_seg = &fixed_seg_table[seg];
		if (mtrr_seg->start <= addr && addr < mtrr_seg->end)
			return seg;
	}

	return -1;
}

static int fixed_mtrr_addr_seg_to_range_index(u64 addr, int seg)
{
	struct fixed_mtrr_segment *mtrr_seg;
	int index;

	mtrr_seg = &fixed_seg_table[seg];
	index = mtrr_seg->range_start;
	index += (addr - mtrr_seg->start) >> mtrr_seg->range_shift;
	return index;
}

static u64 fixed_mtrr_range_end_addr(int seg, int index)
{
	struct fixed_mtrr_segment *mtrr_seg = &fixed_seg_table[seg];
	int pos = index - mtrr_seg->range_start;

	return mtrr_seg->start + ((pos + 1) << mtrr_seg->range_shift);
}

static void var_mtrr_range(struct kvm_mtrr_range *range, u64 *start, u64 *end)
{
	u64 mask;

	*start = range->base & PAGE_MASK;

	mask = range->mask & PAGE_MASK;

	/* This cannot overflow because writing to the reserved bits of
	 * variable MTRRs causes a #GP.
	 */
	*end = (*start | ~mask) + 1;
}

static void update_mtrr(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	gfn_t start, end;
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
		var_mtrr_range(&mtrr_state->var_ranges[index], &start, &end);
	}

	kvm_zap_gfn_range(vcpu->kvm, gpa_to_gfn(start), gpa_to_gfn(end));
}

static bool var_mtrr_range_is_valid(struct kvm_mtrr_range *range)
{
	return (range->mask & (1 << 11)) != 0;
}

static void set_var_mtrr_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	struct kvm_mtrr_range *tmp, *cur;
	int index, is_mtrr_mask;

	index = (msr - 0x200) / 2;
	is_mtrr_mask = msr - 0x200 - 2 * index;
	cur = &mtrr_state->var_ranges[index];

	/* remove the entry if it's in the list. */
	if (var_mtrr_range_is_valid(cur))
		list_del(&mtrr_state->var_ranges[index].node);

	/* Extend the mask with all 1 bits to the left, since those
	 * bits must implicitly be 0.  The bits are then cleared
	 * when reading them.
	 */
	if (!is_mtrr_mask)
		cur->base = data;
	else
		cur->mask = data | (-1LL << cpuid_maxphyaddr(vcpu));

	/* add it to the list if it's enabled. */
	if (var_mtrr_range_is_valid(cur)) {
		list_for_each_entry(tmp, &mtrr_state->head, node)
			if (cur->base >= tmp->base)
				break;
		list_add_tail(&cur->node, &tmp->node);
	}
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
	else
		set_var_mtrr_msr(vcpu, msr, data);

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

		*pdata &= (1ULL << cpuid_maxphyaddr(vcpu)) - 1;
	}

	return 0;
}

void kvm_vcpu_mtrr_init(struct kvm_vcpu *vcpu)
{
	INIT_LIST_HEAD(&vcpu->arch.mtrr_state.head);
}

struct mtrr_iter {
	/* input fields. */
	struct kvm_mtrr *mtrr_state;
	u64 start;
	u64 end;

	/* output fields. */
	int mem_type;
	/* mtrr is completely disabled? */
	bool mtrr_disabled;
	/* [start, end) is not fully covered in MTRRs? */
	bool partial_map;

	/* private fields. */
	union {
		/* used for fixed MTRRs. */
		struct {
			int index;
			int seg;
		};

		/* used for var MTRRs. */
		struct {
			struct kvm_mtrr_range *range;
			/* max address has been covered in var MTRRs. */
			u64 start_max;
		};
	};

	bool fixed;
};

static bool mtrr_lookup_fixed_start(struct mtrr_iter *iter)
{
	int seg, index;

	if (!fixed_mtrr_is_enabled(iter->mtrr_state))
		return false;

	seg = fixed_mtrr_addr_to_seg(iter->start);
	if (seg < 0)
		return false;

	iter->fixed = true;
	index = fixed_mtrr_addr_seg_to_range_index(iter->start, seg);
	iter->index = index;
	iter->seg = seg;
	return true;
}

static bool match_var_range(struct mtrr_iter *iter,
			    struct kvm_mtrr_range *range)
{
	u64 start, end;

	var_mtrr_range(range, &start, &end);
	if (!(start >= iter->end || end <= iter->start)) {
		iter->range = range;

		/*
		 * the function is called when we do kvm_mtrr.head walking.
		 * Range has the minimum base address which interleaves
		 * [looker->start_max, looker->end).
		 */
		iter->partial_map |= iter->start_max < start;

		/* update the max address has been covered. */
		iter->start_max = max(iter->start_max, end);
		return true;
	}

	return false;
}

static void __mtrr_lookup_var_next(struct mtrr_iter *iter)
{
	struct kvm_mtrr *mtrr_state = iter->mtrr_state;

	list_for_each_entry_continue(iter->range, &mtrr_state->head, node)
		if (match_var_range(iter, iter->range))
			return;

	iter->range = NULL;
	iter->partial_map |= iter->start_max < iter->end;
}

static void mtrr_lookup_var_start(struct mtrr_iter *iter)
{
	struct kvm_mtrr *mtrr_state = iter->mtrr_state;

	iter->fixed = false;
	iter->start_max = iter->start;
	iter->range = NULL;
	iter->range = list_prepare_entry(iter->range, &mtrr_state->head, node);

	__mtrr_lookup_var_next(iter);
}

static void mtrr_lookup_fixed_next(struct mtrr_iter *iter)
{
	/* terminate the lookup. */
	if (fixed_mtrr_range_end_addr(iter->seg, iter->index) >= iter->end) {
		iter->fixed = false;
		iter->range = NULL;
		return;
	}

	iter->index++;

	/* have looked up for all fixed MTRRs. */
	if (iter->index >= ARRAY_SIZE(iter->mtrr_state->fixed_ranges))
		return mtrr_lookup_var_start(iter);

	/* switch to next segment. */
	if (iter->index > fixed_mtrr_seg_end_range_index(iter->seg))
		iter->seg++;
}

static void mtrr_lookup_var_next(struct mtrr_iter *iter)
{
	__mtrr_lookup_var_next(iter);
}

static void mtrr_lookup_start(struct mtrr_iter *iter)
{
	if (!mtrr_is_enabled(iter->mtrr_state)) {
		iter->mtrr_disabled = true;
		return;
	}

	if (!mtrr_lookup_fixed_start(iter))
		mtrr_lookup_var_start(iter);
}

static void mtrr_lookup_init(struct mtrr_iter *iter,
			     struct kvm_mtrr *mtrr_state, u64 start, u64 end)
{
	iter->mtrr_state = mtrr_state;
	iter->start = start;
	iter->end = end;
	iter->mtrr_disabled = false;
	iter->partial_map = false;
	iter->fixed = false;
	iter->range = NULL;

	mtrr_lookup_start(iter);
}

static bool mtrr_lookup_okay(struct mtrr_iter *iter)
{
	if (iter->fixed) {
		iter->mem_type = iter->mtrr_state->fixed_ranges[iter->index];
		return true;
	}

	if (iter->range) {
		iter->mem_type = iter->range->base & 0xff;
		return true;
	}

	return false;
}

static void mtrr_lookup_next(struct mtrr_iter *iter)
{
	if (iter->fixed)
		mtrr_lookup_fixed_next(iter);
	else
		mtrr_lookup_var_next(iter);
}

#define mtrr_for_each_mem_type(_iter_, _mtrr_, _gpa_start_, _gpa_end_) \
	for (mtrr_lookup_init(_iter_, _mtrr_, _gpa_start_, _gpa_end_); \
	     mtrr_lookup_okay(_iter_); mtrr_lookup_next(_iter_))

u8 kvm_mtrr_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	struct mtrr_iter iter;
	u64 start, end;
	int type = -1;
	const int wt_wb_mask = (1 << MTRR_TYPE_WRBACK)
			       | (1 << MTRR_TYPE_WRTHROUGH);

	start = gfn_to_gpa(gfn);
	end = start + PAGE_SIZE;

	mtrr_for_each_mem_type(&iter, mtrr_state, start, end) {
		int curr_type = iter.mem_type;

		/*
		 * Please refer to Intel SDM Volume 3: 11.11.4.1 MTRR
		 * Precedences.
		 */

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

	if (iter.mtrr_disabled)
		return mtrr_disabled_type(vcpu);

	/* not contained in any MTRRs. */
	if (type == -1)
		return mtrr_default_type(mtrr_state);

	/*
	 * We just check one page, partially covered by MTRRs is
	 * impossible.
	 */
	WARN_ON(iter.partial_map);

	return type;
}
EXPORT_SYMBOL_GPL(kvm_mtrr_get_guest_memory_type);

bool kvm_mtrr_check_gfn_range_consistency(struct kvm_vcpu *vcpu, gfn_t gfn,
					  int page_num)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	struct mtrr_iter iter;
	u64 start, end;
	int type = -1;

	start = gfn_to_gpa(gfn);
	end = gfn_to_gpa(gfn + page_num);
	mtrr_for_each_mem_type(&iter, mtrr_state, start, end) {
		if (type == -1) {
			type = iter.mem_type;
			continue;
		}

		if (type != iter.mem_type)
			return false;
	}

	if (iter.mtrr_disabled)
		return true;

	if (!iter.partial_map)
		return true;

	if (type == -1)
		return true;

	return type == mtrr_default_type(mtrr_state);
}
