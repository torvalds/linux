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

static void update_mtrr(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_mtrr *mtrr_state = &vcpu->arch.mtrr_state;
	gfn_t start, end, mask;
	int index;
	bool is_fixed = true;

	if (msr == MSR_IA32_CR_PAT || !tdp_enabled ||
	      !kvm_arch_has_noncoherent_dma(vcpu->kvm))
		return;

	if (!mtrr_is_enabled(mtrr_state) && msr != MSR_MTRRdefType)
		return;

	switch (msr) {
	case MSR_MTRRfix64K_00000:
		start = 0x0;
		end = 0x80000;
		break;
	case MSR_MTRRfix16K_80000:
		start = 0x80000;
		end = 0xa0000;
		break;
	case MSR_MTRRfix16K_A0000:
		start = 0xa0000;
		end = 0xc0000;
		break;
	case MSR_MTRRfix4K_C0000 ... MSR_MTRRfix4K_F8000:
		index = msr - MSR_MTRRfix4K_C0000;
		start = 0xc0000 + index * (32 << 10);
		end = start + (32 << 10);
		break;
	case MSR_MTRRdefType:
		is_fixed = false;
		start = 0x0;
		end = ~0ULL;
		break;
	default:
		/* variable range MTRRs. */
		is_fixed = false;
		index = (msr - 0x200) / 2;
		start = (((u64)mtrr_state->var_ranges[index].base_hi) << 32) +
		       (mtrr_state->var_ranges[index].base_lo & PAGE_MASK);
		mask = (((u64)mtrr_state->var_ranges[index].mask_hi) << 32) +
		       (mtrr_state->var_ranges[index].mask_lo & PAGE_MASK);
		mask |= ~0ULL << cpuid_maxphyaddr(vcpu);

		end = ((start & mask) | ~mask) + 1;
	}

	if (is_fixed && !fixed_mtrr_is_enabled(mtrr_state))
		return;

	kvm_zap_gfn_range(vcpu->kvm, gpa_to_gfn(start), gpa_to_gfn(end));
}

int kvm_mtrr_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	u64 *p = (u64 *)&vcpu->arch.mtrr_state.fixed_ranges;

	if (!kvm_mtrr_valid(vcpu, msr, data))
		return 1;

	if (msr == MSR_MTRRdefType)
		vcpu->arch.mtrr_state.deftype = data;
	else if (msr == MSR_MTRRfix64K_00000)
		p[0] = data;
	else if (msr == MSR_MTRRfix16K_80000 || msr == MSR_MTRRfix16K_A0000)
		p[1 + msr - MSR_MTRRfix16K_80000] = data;
	else if (msr >= MSR_MTRRfix4K_C0000 && msr <= MSR_MTRRfix4K_F8000)
		p[3 + msr - MSR_MTRRfix4K_C0000] = data;
	else if (msr == MSR_IA32_CR_PAT)
		vcpu->arch.pat = data;
	else {	/* Variable MTRRs */
		int idx, is_mtrr_mask;
		u64 *pt;

		idx = (msr - 0x200) / 2;
		is_mtrr_mask = msr - 0x200 - 2 * idx;
		if (!is_mtrr_mask)
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].base_lo;
		else
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].mask_lo;
		*pt = data;
	}

	update_mtrr(vcpu, msr);
	return 0;
}

int kvm_mtrr_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 *p = (u64 *)&vcpu->arch.mtrr_state.fixed_ranges;

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

	if (msr == MSR_MTRRdefType)
		*pdata = vcpu->arch.mtrr_state.deftype;
	else if (msr == MSR_MTRRfix64K_00000)
		*pdata = p[0];
	else if (msr == MSR_MTRRfix16K_80000 || msr == MSR_MTRRfix16K_A0000)
		*pdata = p[1 + msr - MSR_MTRRfix16K_80000];
	else if (msr >= MSR_MTRRfix4K_C0000 && msr <= MSR_MTRRfix4K_F8000)
		*pdata = p[3 + msr - MSR_MTRRfix4K_C0000];
	else if (msr == MSR_IA32_CR_PAT)
		*pdata = vcpu->arch.pat;
	else {	/* Variable MTRRs */
		int idx, is_mtrr_mask;
		u64 *pt;

		idx = (msr - 0x200) / 2;
		is_mtrr_mask = msr - 0x200 - 2 * idx;
		if (!is_mtrr_mask)
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].base_lo;
		else
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].mask_lo;
		*pdata = *pt;
	}

	return 0;
}

/*
 * The function is based on mtrr_type_lookup() in
 * arch/x86/kernel/cpu/mtrr/generic.c
 */
static int get_mtrr_type(struct kvm_mtrr *mtrr_state,
			 u64 start, u64 end)
{
	u64 base, mask;
	u8 prev_match, curr_match;
	int i, num_var_ranges = KVM_NR_VAR_MTRR;

	/* MTRR is completely disabled, use UC for all of physical memory. */
	if (!mtrr_is_enabled(mtrr_state))
		return MTRR_TYPE_UNCACHABLE;

	/* Make end inclusive end, instead of exclusive */
	end--;

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
	prev_match = 0xFF;
	for (i = 0; i < num_var_ranges; ++i) {
		unsigned short start_state, end_state;

		if (!(mtrr_state->var_ranges[i].mask_lo & (1 << 11)))
			continue;

		base = (((u64)mtrr_state->var_ranges[i].base_hi) << 32) +
		       (mtrr_state->var_ranges[i].base_lo & PAGE_MASK);
		mask = (((u64)mtrr_state->var_ranges[i].mask_hi) << 32) +
		       (mtrr_state->var_ranges[i].mask_lo & PAGE_MASK);

		start_state = ((start & mask) == (base & mask));
		end_state = ((end & mask) == (base & mask));
		if (start_state != end_state)
			return 0xFE;

		if ((start & mask) != (base & mask))
			continue;

		curr_match = mtrr_state->var_ranges[i].base_lo & 0xff;
		if (prev_match == 0xFF) {
			prev_match = curr_match;
			continue;
		}

		if (prev_match == MTRR_TYPE_UNCACHABLE ||
		    curr_match == MTRR_TYPE_UNCACHABLE)
			return MTRR_TYPE_UNCACHABLE;

		if ((prev_match == MTRR_TYPE_WRBACK &&
		     curr_match == MTRR_TYPE_WRTHROUGH) ||
		    (prev_match == MTRR_TYPE_WRTHROUGH &&
		     curr_match == MTRR_TYPE_WRBACK)) {
			prev_match = MTRR_TYPE_WRTHROUGH;
			curr_match = MTRR_TYPE_WRTHROUGH;
		}

		if (prev_match != curr_match)
			return MTRR_TYPE_UNCACHABLE;
	}

	if (prev_match != 0xFF)
		return prev_match;

	return mtrr_default_type(mtrr_state);
}

u8 kvm_mtrr_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	u8 mtrr;

	mtrr = get_mtrr_type(&vcpu->arch.mtrr_state, gfn << PAGE_SHIFT,
			     (gfn << PAGE_SHIFT) + PAGE_SIZE);
	if (mtrr == 0xfe || mtrr == 0xff)
		mtrr = MTRR_TYPE_WRBACK;
	return mtrr;
}
EXPORT_SYMBOL_GPL(kvm_mtrr_get_guest_memory_type);
