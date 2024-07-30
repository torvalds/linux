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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>
#include <asm/mtrr.h>

#include "cpuid.h"

static u64 *find_mtrr(struct kvm_vcpu *vcpu, unsigned int msr)
{
	int index;

	switch (msr) {
	case MTRRphysBase_MSR(0) ... MTRRphysMask_MSR(KVM_NR_VAR_MTRR - 1):
		index = msr - MTRRphysBase_MSR(0);
		return &vcpu->arch.mtrr_state.var[index];
	case MSR_MTRRfix64K_00000:
		return &vcpu->arch.mtrr_state.fixed_64k;
	case MSR_MTRRfix16K_80000:
	case MSR_MTRRfix16K_A0000:
		index = msr - MSR_MTRRfix16K_80000;
		return &vcpu->arch.mtrr_state.fixed_16k[index];
	case MSR_MTRRfix4K_C0000:
	case MSR_MTRRfix4K_C8000:
	case MSR_MTRRfix4K_D0000:
	case MSR_MTRRfix4K_D8000:
	case MSR_MTRRfix4K_E0000:
	case MSR_MTRRfix4K_E8000:
	case MSR_MTRRfix4K_F0000:
	case MSR_MTRRfix4K_F8000:
		index = msr - MSR_MTRRfix4K_C0000;
		return &vcpu->arch.mtrr_state.fixed_4k[index];
	case MSR_MTRRdefType:
		return &vcpu->arch.mtrr_state.deftype;
	default:
		break;
	}
	return NULL;
}

static bool valid_mtrr_type(unsigned t)
{
	return t < 8 && (1 << t) & 0x73; /* 0, 1, 4, 5, 6 */
}

static bool kvm_mtrr_valid(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	int i;
	u64 mask;

	if (msr == MSR_MTRRdefType) {
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
	if (WARN_ON_ONCE(!(msr >= MTRRphysBase_MSR(0) &&
			   msr <= MTRRphysMask_MSR(KVM_NR_VAR_MTRR - 1))))
		return false;

	mask = kvm_vcpu_reserved_gpa_bits_raw(vcpu);
	if ((msr & 1) == 0) {
		/* MTRR base */
		if (!valid_mtrr_type(data & 0xff))
			return false;
		mask |= 0xf00;
	} else {
		/* MTRR mask */
		mask |= 0x7ff;
	}

	return (data & mask) == 0;
}

int kvm_mtrr_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	u64 *mtrr;

	mtrr = find_mtrr(vcpu, msr);
	if (!mtrr)
		return 1;

	if (!kvm_mtrr_valid(vcpu, msr, data))
		return 1;

	*mtrr = data;
	return 0;
}

int kvm_mtrr_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 *mtrr;

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

	mtrr = find_mtrr(vcpu, msr);
	if (!mtrr)
		return 1;

	*pdata = *mtrr;
	return 0;
}
