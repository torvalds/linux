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
#include <linux/sched/stat.h>

#include <trace/events/kvm.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>

#include "trace.h"

DEFINE_STATIC_KEY_DEFERRED_FALSE(kvm_xen_enabled, HZ);

static int kvm_xen_shared_info_init(struct kvm *kvm, gfn_t gfn)
{
	gpa_t gpa = gfn_to_gpa(gfn);
	int wc_ofs, sec_hi_ofs;
	int ret = 0;
	int idx = srcu_read_lock(&kvm->srcu);

	if (kvm_is_error_hva(gfn_to_hva(kvm, gfn))) {
		ret = -EFAULT;
		goto out;
	}
	kvm->arch.xen.shinfo_gfn = gfn;

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

static void kvm_xen_update_runstate(struct kvm_vcpu *v, int state)
{
	struct kvm_vcpu_xen *vx = &v->arch.xen;
	u64 now = get_kvmclock_ns(v->kvm);
	u64 delta_ns = now - vx->runstate_entry_time;
	u64 run_delay = current->sched_info.run_delay;

	if (unlikely(!vx->runstate_entry_time))
		vx->current_runstate = RUNSTATE_offline;

	/*
	 * Time waiting for the scheduler isn't "stolen" if the
	 * vCPU wasn't running anyway.
	 */
	if (vx->current_runstate == RUNSTATE_running) {
		u64 steal_ns = run_delay - vx->last_steal;

		delta_ns -= steal_ns;

		vx->runstate_times[RUNSTATE_runnable] += steal_ns;
	}
	vx->last_steal = run_delay;

	vx->runstate_times[vx->current_runstate] += delta_ns;
	vx->current_runstate = state;
	vx->runstate_entry_time = now;
}

void kvm_xen_update_runstate_guest(struct kvm_vcpu *v, int state)
{
	struct kvm_vcpu_xen *vx = &v->arch.xen;
	uint64_t state_entry_time;
	unsigned int offset;

	kvm_xen_update_runstate(v, state);

	if (!vx->runstate_set)
		return;

	BUILD_BUG_ON(sizeof(struct compat_vcpu_runstate_info) != 0x2c);

	offset = offsetof(struct compat_vcpu_runstate_info, state_entry_time);
#ifdef CONFIG_X86_64
	/*
	 * The only difference is alignment of uint64_t in 32-bit.
	 * So the first field 'state' is accessed directly using
	 * offsetof() (where its offset happens to be zero), while the
	 * remaining fields which are all uint64_t, start at 'offset'
	 * which we tweak here by adding 4.
	 */
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, state_entry_time) !=
		     offsetof(struct compat_vcpu_runstate_info, state_entry_time) + 4);
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, time) !=
		     offsetof(struct compat_vcpu_runstate_info, time) + 4);

	if (v->kvm->arch.xen.long_mode)
		offset = offsetof(struct vcpu_runstate_info, state_entry_time);
#endif
	/*
	 * First write the updated state_entry_time at the appropriate
	 * location determined by 'offset'.
	 */
	state_entry_time = vx->runstate_entry_time;
	state_entry_time |= XEN_RUNSTATE_UPDATE;

	BUILD_BUG_ON(sizeof(((struct vcpu_runstate_info *)0)->state_entry_time) !=
		     sizeof(state_entry_time));
	BUILD_BUG_ON(sizeof(((struct compat_vcpu_runstate_info *)0)->state_entry_time) !=
		     sizeof(state_entry_time));

	if (kvm_write_guest_offset_cached(v->kvm, &v->arch.xen.runstate_cache,
					  &state_entry_time, offset,
					  sizeof(state_entry_time)))
		return;
	smp_wmb();

	/*
	 * Next, write the new runstate. This is in the *same* place
	 * for 32-bit and 64-bit guests, asserted here for paranoia.
	 */
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, state) !=
		     offsetof(struct compat_vcpu_runstate_info, state));
	BUILD_BUG_ON(sizeof(((struct vcpu_runstate_info *)0)->state) !=
		     sizeof(vx->current_runstate));
	BUILD_BUG_ON(sizeof(((struct compat_vcpu_runstate_info *)0)->state) !=
		     sizeof(vx->current_runstate));

	if (kvm_write_guest_offset_cached(v->kvm, &v->arch.xen.runstate_cache,
					  &vx->current_runstate,
					  offsetof(struct vcpu_runstate_info, state),
					  sizeof(vx->current_runstate)))
		return;

	/*
	 * Write the actual runstate times immediately after the
	 * runstate_entry_time.
	 */
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, state_entry_time) !=
		     offsetof(struct vcpu_runstate_info, time) - sizeof(u64));
	BUILD_BUG_ON(offsetof(struct compat_vcpu_runstate_info, state_entry_time) !=
		     offsetof(struct compat_vcpu_runstate_info, time) - sizeof(u64));
	BUILD_BUG_ON(sizeof(((struct vcpu_runstate_info *)0)->time) !=
		     sizeof(((struct compat_vcpu_runstate_info *)0)->time));
	BUILD_BUG_ON(sizeof(((struct vcpu_runstate_info *)0)->time) !=
		     sizeof(vx->runstate_times));

	if (kvm_write_guest_offset_cached(v->kvm, &v->arch.xen.runstate_cache,
					  &vx->runstate_times[0],
					  offset + sizeof(u64),
					  sizeof(vx->runstate_times)))
		return;

	smp_wmb();

	/*
	 * Finally, clear the XEN_RUNSTATE_UPDATE bit in the guest's
	 * runstate_entry_time field.
	 */

	state_entry_time &= ~XEN_RUNSTATE_UPDATE;
	if (kvm_write_guest_offset_cached(v->kvm, &v->arch.xen.runstate_cache,
					  &state_entry_time, offset,
					  sizeof(state_entry_time)))
		return;
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
			kvm->arch.xen.shinfo_gfn = GPA_INVALID;
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
		data->u.shared_info.gfn = gpa_to_gfn(kvm->arch.xen.shinfo_gfn);
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
		BUILD_BUG_ON(offsetof(struct vcpu_info, time) !=
			     offsetof(struct compat_vcpu_info, time));

		if (data->u.gpa == GPA_INVALID) {
			vcpu->arch.xen.vcpu_info_set = false;
			r = 0;
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
			r = 0;
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

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		if (data->u.gpa == GPA_INVALID) {
			vcpu->arch.xen.runstate_set = false;
			r = 0;
			break;
		}

		r = kvm_gfn_to_hva_cache_init(vcpu->kvm,
					      &vcpu->arch.xen.runstate_cache,
					      data->u.gpa,
					      sizeof(struct vcpu_runstate_info));
		if (!r) {
			vcpu->arch.xen.runstate_set = true;
		}
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_CURRENT:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		if (data->u.runstate.state > RUNSTATE_offline) {
			r = -EINVAL;
			break;
		}

		kvm_xen_update_runstate(vcpu, data->u.runstate.state);
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_DATA:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		if (data->u.runstate.state > RUNSTATE_offline) {
			r = -EINVAL;
			break;
		}
		if (data->u.runstate.state_entry_time !=
		    (data->u.runstate.time_running +
		     data->u.runstate.time_runnable +
		     data->u.runstate.time_blocked +
		     data->u.runstate.time_offline)) {
			r = -EINVAL;
			break;
		}
		if (get_kvmclock_ns(vcpu->kvm) <
		    data->u.runstate.state_entry_time) {
			r = -EINVAL;
			break;
		}

		vcpu->arch.xen.current_runstate = data->u.runstate.state;
		vcpu->arch.xen.runstate_entry_time =
			data->u.runstate.state_entry_time;
		vcpu->arch.xen.runstate_times[RUNSTATE_running] =
			data->u.runstate.time_running;
		vcpu->arch.xen.runstate_times[RUNSTATE_runnable] =
			data->u.runstate.time_runnable;
		vcpu->arch.xen.runstate_times[RUNSTATE_blocked] =
			data->u.runstate.time_blocked;
		vcpu->arch.xen.runstate_times[RUNSTATE_offline] =
			data->u.runstate.time_offline;
		vcpu->arch.xen.last_steal = current->sched_info.run_delay;
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADJUST:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		if (data->u.runstate.state > RUNSTATE_offline &&
		    data->u.runstate.state != (u64)-1) {
			r = -EINVAL;
			break;
		}
		/* The adjustment must add up */
		if (data->u.runstate.state_entry_time !=
		    (data->u.runstate.time_running +
		     data->u.runstate.time_runnable +
		     data->u.runstate.time_blocked +
		     data->u.runstate.time_offline)) {
			r = -EINVAL;
			break;
		}

		if (get_kvmclock_ns(vcpu->kvm) <
		    (vcpu->arch.xen.runstate_entry_time +
		     data->u.runstate.state_entry_time)) {
			r = -EINVAL;
			break;
		}

		vcpu->arch.xen.runstate_entry_time +=
			data->u.runstate.state_entry_time;
		vcpu->arch.xen.runstate_times[RUNSTATE_running] +=
			data->u.runstate.time_running;
		vcpu->arch.xen.runstate_times[RUNSTATE_runnable] +=
			data->u.runstate.time_runnable;
		vcpu->arch.xen.runstate_times[RUNSTATE_blocked] +=
			data->u.runstate.time_blocked;
		vcpu->arch.xen.runstate_times[RUNSTATE_offline] +=
			data->u.runstate.time_offline;

		if (data->u.runstate.state <= RUNSTATE_offline)
			kvm_xen_update_runstate(vcpu, data->u.runstate.state);
		r = 0;
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

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		if (vcpu->arch.xen.runstate_set) {
			data->u.gpa = vcpu->arch.xen.runstate_cache.gpa;
			r = 0;
		}
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_CURRENT:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		data->u.runstate.state = vcpu->arch.xen.current_runstate;
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_DATA:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		data->u.runstate.state = vcpu->arch.xen.current_runstate;
		data->u.runstate.state_entry_time =
			vcpu->arch.xen.runstate_entry_time;
		data->u.runstate.time_running =
			vcpu->arch.xen.runstate_times[RUNSTATE_running];
		data->u.runstate.time_runnable =
			vcpu->arch.xen.runstate_times[RUNSTATE_runnable];
		data->u.runstate.time_blocked =
			vcpu->arch.xen.runstate_times[RUNSTATE_blocked];
		data->u.runstate.time_offline =
			vcpu->arch.xen.runstate_times[RUNSTATE_offline];
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADJUST:
		r = -EINVAL;
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

void kvm_xen_init_vm(struct kvm *kvm)
{
	kvm->arch.xen.shinfo_gfn = GPA_INVALID;
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
