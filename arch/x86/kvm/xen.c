// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * KVM Xen emulation
 */

#include "x86.h"
#include "xen.h"
#include "hyperv.h"

#include <linux/kvm_host.h>

#include <trace/events/kvm.h>
#include <xen/interface/xen.h>

#include "trace.h"

DEFINE_STATIC_KEY_DEFERRED_FALSE(kvm_xen_enabled, HZ);

static int kvm_xen_shared_info_init(struct kvm *kvm, gfn_t gfn)
{
	gpa_t gpa = gfn_to_gpa(gfn);
	int wc_ofs, sec_hi_ofs;
	int ret;
	int idx = srcu_read_lock(&kvm->srcu);

	ret = kvm_gfn_to_hva_cache_init(kvm, &kvm->arch.xen.shinfo_cache,
					gpa, PAGE_SIZE);
	if (ret)
		goto out;

	kvm->arch.xen.shinfo_set = true;

	/* Paranoia checks on the 32-bit struct layout */
	BUILD_BUG_ON(offsetof(struct compat_shared_info, wc) != 0x900);
	BUILD_BUG_ON(offsetof(struct compat_shared_info, arch.wc_sec_hi) != 0x924);
	BUILD_BUG_ON(offsetof(struct pvclock_vcpu_time_info, version) != 0);

	/* 32-bit location by default */
	wc_ofs = offsetof(struct compat_shared_info, wc);
	sec_hi_ofs = offsetof(struct compat_shared_info, arch.wc_sec_hi);

#ifdef CONFIG_X86_64
	/* Paranoia checks on the 64-bit struct layout */
	BUILD_BUG_ON(offsetof(struct shared_info, wc) != 0xc00);
	BUILD_BUG_ON(offsetof(struct shared_info, wc_sec_hi) != 0xc0c);

	if (kvm->arch.xen.long_mode) {
		wc_ofs = offsetof(struct shared_info, wc);
		sec_hi_ofs = offsetof(struct shared_info, wc_sec_hi);
	}
#endif

	kvm_write_wall_clock(kvm, gpa + wc_ofs, sec_hi_ofs - wc_ofs);
	kvm_make_all_cpus_request(kvm, KVM_REQ_MASTERCLOCK_UPDATE);

out:
	srcu_read_unlock(&kvm->srcu, idx);
	return ret;
}

int __kvm_xen_has_interrupt(struct kvm_vcpu *v)
{
	u8 rc = 0;

	/*
	 * If the global upcall vector (HVMIRQ_callback_vector) is set and
	 * the vCPU's evtchn_upcall_pending flag is set, the IRQ is pending.
	 */
	struct gfn_to_hva_cache *ghc = &v->arch.xen.vcpu_info_cache;
	struct kvm_memslots *slots = kvm_memslots(v->kvm);
	unsigned int offset = offsetof(struct vcpu_info, evtchn_upcall_pending);

	/* No need for compat handling here */
	BUILD_BUG_ON(offsetof(struct vcpu_info, evtchn_upcall_pending) !=
		     offsetof(struct compat_vcpu_info, evtchn_upcall_pending));
	BUILD_BUG_ON(sizeof(rc) !=
		     sizeof(((struct vcpu_info *)0)->evtchn_upcall_pending));
	BUILD_BUG_ON(sizeof(rc) !=
		     sizeof(((struct compat_vcpu_info *)0)->evtchn_upcall_pending));

	/*
	 * For efficiency, this mirrors the checks for using the valid
	 * cache in kvm_read_guest_offset_cached(), but just uses
	 * __get_user() instead. And falls back to the slow path.
	 */
	if (likely(slots->generation == ghc->generation &&
		   !kvm_is_error_hva(ghc->hva) && ghc->memslot)) {
		/* Fast path */
		__get_user(rc, (u8 __user *)ghc->hva + offset);
	} else {
		/* Slow path */
		kvm_read_guest_offset_cached(v->kvm, ghc, &rc, offset,
					     sizeof(rc));
	}

	return rc;
}

int kvm_xen_hvm_set_attr(struct kvm *kvm, struct kvm_xen_hvm_attr *data)
{
	int r = -ENOENT;

	mutex_lock(&kvm->lock);

	switch (data->type) {
	case KVM_XEN_ATTR_TYPE_LONG_MODE:
		if (!IS_ENABLED(CONFIG_64BIT) && data->u.long_mode) {
			r = -EINVAL;
		} else {
			kvm->arch.xen.long_mode = !!data->u.long_mode;
			r = 0;
		}
		break;

	case KVM_XEN_ATTR_TYPE_SHARED_INFO:
		if (data->u.shared_info.gfn == GPA_INVALID) {
			kvm->arch.xen.shinfo_set = false;
			r = 0;
			break;
		}
		r = kvm_xen_shared_info_init(kvm, data->u.shared_info.gfn);
		break;


	case KVM_XEN_ATTR_TYPE_UPCALL_VECTOR:
		if (data->u.vector && data->u.vector < 0x10)
			r = -EINVAL;
		else {
			kvm->arch.xen.upcall_vector = data->u.vector;
			r = 0;
		}
		break;

	default:
		break;
	}

	mutex_unlock(&kvm->lock);
	return r;
}

int kvm_xen_hvm_get_attr(struct kvm *kvm, struct kvm_xen_hvm_attr *data)
{
	int r = -ENOENT;

	mutex_lock(&kvm->lock);

	switch (data->type) {
	case KVM_XEN_ATTR_TYPE_LONG_MODE:
		data->u.long_mode = kvm->arch.xen.long_mode;
		r = 0;
		break;

	case KVM_XEN_ATTR_TYPE_SHARED_INFO:
		if (kvm->arch.xen.shinfo_set)
			data->u.shared_info.gfn = gpa_to_gfn(kvm->arch.xen.shinfo_cache.gpa);
		else
			data->u.shared_info.gfn = GPA_INVALID;
		r = 0;
		break;

	case KVM_XEN_ATTR_TYPE_UPCALL_VECTOR:
		data->u.vector = kvm->arch.xen.upcall_vector;
		r = 0;
		break;

	default:
		break;
	}

	mutex_unlock(&kvm->lock);
	return r;
}

int kvm_xen_vcpu_set_attr(struct kvm_vcpu *vcpu, struct kvm_xen_vcpu_attr *data)
{
	int idx, r = -ENOENT;

	mutex_lock(&vcpu->kvm->lock);
	idx = srcu_read_lock(&vcpu->kvm->srcu);

	switch (data->type) {
	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO:
		/* No compat necessary here. */
		BUILD_BUG_ON(sizeof(struct vcpu_info) !=
			     sizeof(struct compat_vcpu_info));

		if (data->u.gpa == GPA_INVALID) {
			vcpu->arch.xen.vcpu_info_set = false;
			break;
		}

		r = kvm_gfn_to_hva_cache_init(vcpu->kvm,
					      &vcpu->arch.xen.vcpu_info_cache,
					      data->u.gpa,
					      sizeof(struct vcpu_info));
		if (!r) {
			vcpu->arch.xen.vcpu_info_set = true;
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
		}
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO:
		if (data->u.gpa == GPA_INVALID) {
			vcpu->arch.xen.vcpu_time_info_set = false;
			break;
		}

		r = kvm_gfn_to_hva_cache_init(vcpu->kvm,
					      &vcpu->arch.xen.vcpu_time_info_cache,
					      data->u.gpa,
					      sizeof(struct pvclock_vcpu_time_info));
		if (!r) {
			vcpu->arch.xen.vcpu_time_info_set = true;
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
		}
		break;

	default:
		break;
	}

	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	mutex_unlock(&vcpu->kvm->lock);
	return r;
}

int kvm_xen_vcpu_get_attr(struct kvm_vcpu *vcpu, struct kvm_xen_vcpu_attr *data)
{
	int r = -ENOENT;

	mutex_lock(&vcpu->kvm->lock);

	switch (data->type) {
	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO:
		if (vcpu->arch.xen.vcpu_info_set)
			data->u.gpa = vcpu->arch.xen.vcpu_info_cache.gpa;
		else
			data->u.gpa = GPA_INVALID;
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO:
		if (vcpu->arch.xen.vcpu_time_info_set)
			data->u.gpa = vcpu->arch.xen.vcpu_time_info_cache.gpa;
		else
			data->u.gpa = GPA_INVALID;
		r = 0;
		break;

	default:
		break;
	}

	mutex_unlock(&vcpu->kvm->lock);
	return r;
}

int kvm_xen_write_hypercall_page(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm *kvm = vcpu->kvm;
	u32 page_num = data & ~PAGE_MASK;
	u64 page_addr = data & PAGE_MASK;
	bool lm = is_long_mode(vcpu);

	/* Latch long_mode for shared_info pages etc. */
	vcpu->kvm->arch.xen.long_mode = lm;

	/*
	 * If Xen hypercall intercept is enabled, fill the hypercall
	 * page with VMCALL/VMMCALL instructions since that's what
	 * we catch. Else the VMM has provided the hypercall pages
	 * with instructions of its own choosing, so use those.
	 */
	if (kvm_xen_hypercall_enabled(kvm)) {
		u8 instructions[32];
		int i;

		if (page_num)
			return 1;

		/* mov imm32, %eax */
		instructions[0] = 0xb8;

		/* vmcall / vmmcall */
		kvm_x86_ops.patch_hypercall(vcpu, instructions + 5);

		/* ret */
		instructions[8] = 0xc3;

		/* int3 to pad */
		memset(instructions + 9, 0xcc, sizeof(instructions) - 9);

		for (i = 0; i < PAGE_SIZE / sizeof(instructions); i++) {
			*(u32 *)&instructions[1] = i;
			if (kvm_vcpu_write_guest(vcpu,
						 page_addr + (i * sizeof(instructions)),
						 instructions, sizeof(instructions)))
				return 1;
		}
	} else {
		/*
		 * Note, truncation is a non-issue as 'lm' is guaranteed to be
		 * false for a 32-bit kernel, i.e. when hva_t is only 4 bytes.
		 */
		hva_t blob_addr = lm ? kvm->arch.xen_hvm_config.blob_addr_64
				     : kvm->arch.xen_hvm_config.blob_addr_32;
		u8 blob_size = lm ? kvm->arch.xen_hvm_config.blob_size_64
				  : kvm->arch.xen_hvm_config.blob_size_32;
		u8 *page;

		if (page_num >= blob_size)
			return 1;

		blob_addr += page_num * PAGE_SIZE;

		page = memdup_user((u8 __user *)blob_addr, PAGE_SIZE);
		if (IS_ERR(page))
			return PTR_ERR(page);

		if (kvm_vcpu_write_guest(vcpu, page_addr, page, PAGE_SIZE)) {
			kfree(page);
			return 1;
		}
	}
	return 0;
}

int kvm_xen_hvm_config(struct kvm *kvm, struct kvm_xen_hvm_config *xhc)
{
	if (xhc->flags & ~KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL)
		return -EINVAL;

	/*
	 * With hypercall interception the kernel generates its own
	 * hypercall page so it must not be provided.
	 */
	if ((xhc->flags & KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL) &&
	    (xhc->blob_addr_32 || xhc->blob_addr_64 ||
	     xhc->blob_size_32 || xhc->blob_size_64))
		return -EINVAL;

	mutex_lock(&kvm->lock);

	if (xhc->msr && !kvm->arch.xen_hvm_config.msr)
		static_branch_inc(&kvm_xen_enabled.key);
	else if (!xhc->msr && kvm->arch.xen_hvm_config.msr)
		static_branch_slow_dec_deferred(&kvm_xen_enabled);

	memcpy(&kvm->arch.xen_hvm_config, xhc, sizeof(*xhc));

	mutex_unlock(&kvm->lock);
	return 0;
}

void kvm_xen_destroy_vm(struct kvm *kvm)
{
	if (kvm->arch.xen_hvm_config.msr)
		static_branch_slow_dec_deferred(&kvm_xen_enabled);
}

static int kvm_xen_hypercall_set_result(struct kvm_vcpu *vcpu, u64 result)
{
	kvm_rax_write(vcpu, result);
	return kvm_skip_emulated_instruction(vcpu);
}

static int kvm_xen_hypercall_complete_userspace(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (unlikely(!kvm_is_linear_rip(vcpu, vcpu->arch.xen.hypercall_rip)))
		return 1;

	return kvm_xen_hypercall_set_result(vcpu, run->xen.u.hcall.result);
}

int kvm_xen_hypercall(struct kvm_vcpu *vcpu)
{
	bool longmode;
	u64 input, params[6];

	input = (u64)kvm_register_read(vcpu, VCPU_REGS_RAX);

	/* Hyper-V hypercalls get bit 31 set in EAX */
	if ((input & 0x80000000) &&
	    kvm_hv_hypercall_enabled(vcpu))
		return kvm_hv_hypercall(vcpu);

	longmode = is_64_bit_mode(vcpu);
	if (!longmode) {
		params[0] = (u32)kvm_rbx_read(vcpu);
		params[1] = (u32)kvm_rcx_read(vcpu);
		params[2] = (u32)kvm_rdx_read(vcpu);
		params[3] = (u32)kvm_rsi_read(vcpu);
		params[4] = (u32)kvm_rdi_read(vcpu);
		params[5] = (u32)kvm_rbp_read(vcpu);
	}
#ifdef CONFIG_X86_64
	else {
		params[0] = (u64)kvm_rdi_read(vcpu);
		params[1] = (u64)kvm_rsi_read(vcpu);
		params[2] = (u64)kvm_rdx_read(vcpu);
		params[3] = (u64)kvm_r10_read(vcpu);
		params[4] = (u64)kvm_r8_read(vcpu);
		params[5] = (u64)kvm_r9_read(vcpu);
	}
#endif
	trace_kvm_xen_hypercall(input, params[0], params[1], params[2],
				params[3], params[4], params[5]);

	vcpu->run->exit_reason = KVM_EXIT_XEN;
	vcpu->run->xen.type = KVM_EXIT_XEN_HCALL;
	vcpu->run->xen.u.hcall.longmode = longmode;
	vcpu->run->xen.u.hcall.cpl = kvm_x86_ops.get_cpl(vcpu);
	vcpu->run->xen.u.hcall.input = input;
	vcpu->run->xen.u.hcall.params[0] = params[0];
	vcpu->run->xen.u.hcall.params[1] = params[1];
	vcpu->run->xen.u.hcall.params[2] = params[2];
	vcpu->run->xen.u.hcall.params[3] = params[3];
	vcpu->run->xen.u.hcall.params[4] = params[4];
	vcpu->run->xen.u.hcall.params[5] = params[5];
	vcpu->arch.xen.hypercall_rip = kvm_get_linear_rip(vcpu);
	vcpu->arch.complete_userspace_io =
		kvm_xen_hypercall_complete_userspace;

	return 0;
}
