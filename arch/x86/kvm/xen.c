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
#include <xen/interface/event_channel.h>

#include "trace.h"

DEFINE_STATIC_KEY_DEFERRED_FALSE(kvm_xen_enabled, HZ);

static int kvm_xen_shared_info_init(struct kvm *kvm, gfn_t gfn)
{
	struct gfn_to_pfn_cache *gpc = &kvm->arch.xen.shinfo_cache;
	struct pvclock_wall_clock *wc;
	gpa_t gpa = gfn_to_gpa(gfn);
	u32 *wc_sec_hi;
	u32 wc_version;
	u64 wall_nsec;
	int ret = 0;
	int idx = srcu_read_lock(&kvm->srcu);

	if (gfn == GPA_INVALID) {
		kvm_gfn_to_pfn_cache_destroy(kvm, gpc);
		goto out;
	}

	do {
		ret = kvm_gfn_to_pfn_cache_init(kvm, gpc, NULL, false, true,
						gpa, PAGE_SIZE, false);
		if (ret)
			goto out;

		/*
		 * This code mirrors kvm_write_wall_clock() except that it writes
		 * directly through the pfn cache and doesn't mark the page dirty.
		 */
		wall_nsec = ktime_get_real_ns() - get_kvmclock_ns(kvm);

		/* It could be invalid again already, so we need to check */
		read_lock_irq(&gpc->lock);

		if (gpc->valid)
			break;

		read_unlock_irq(&gpc->lock);
	} while (1);

	/* Paranoia checks on the 32-bit struct layout */
	BUILD_BUG_ON(offsetof(struct compat_shared_info, wc) != 0x900);
	BUILD_BUG_ON(offsetof(struct compat_shared_info, arch.wc_sec_hi) != 0x924);
	BUILD_BUG_ON(offsetof(struct pvclock_vcpu_time_info, version) != 0);

#ifdef CONFIG_X86_64
	/* Paranoia checks on the 64-bit struct layout */
	BUILD_BUG_ON(offsetof(struct shared_info, wc) != 0xc00);
	BUILD_BUG_ON(offsetof(struct shared_info, wc_sec_hi) != 0xc0c);

	if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode) {
		struct shared_info *shinfo = gpc->khva;

		wc_sec_hi = &shinfo->wc_sec_hi;
		wc = &shinfo->wc;
	} else
#endif
	{
		struct compat_shared_info *shinfo = gpc->khva;

		wc_sec_hi = &shinfo->arch.wc_sec_hi;
		wc = &shinfo->wc;
	}

	/* Increment and ensure an odd value */
	wc_version = wc->version = (wc->version + 1) | 1;
	smp_wmb();

	wc->nsec = do_div(wall_nsec,  1000000000);
	wc->sec = (u32)wall_nsec;
	*wc_sec_hi = wall_nsec >> 32;
	smp_wmb();

	wc->version = wc_version + 1;
	read_unlock_irq(&gpc->lock);

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

	BUILD_BUG_ON(sizeof_field(struct vcpu_runstate_info, state_entry_time) !=
		     sizeof(state_entry_time));
	BUILD_BUG_ON(sizeof_field(struct compat_vcpu_runstate_info, state_entry_time) !=
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
	BUILD_BUG_ON(sizeof_field(struct vcpu_runstate_info, state) !=
		     sizeof(vx->current_runstate));
	BUILD_BUG_ON(sizeof_field(struct compat_vcpu_runstate_info, state) !=
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
	BUILD_BUG_ON(sizeof_field(struct vcpu_runstate_info, time) !=
		     sizeof_field(struct compat_vcpu_runstate_info, time));
	BUILD_BUG_ON(sizeof_field(struct vcpu_runstate_info, time) !=
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
	unsigned long evtchn_pending_sel = READ_ONCE(v->arch.xen.evtchn_pending_sel);
	bool atomic = in_atomic() || !task_is_running(current);
	int err;
	u8 rc = 0;

	/*
	 * If the global upcall vector (HVMIRQ_callback_vector) is set and
	 * the vCPU's evtchn_upcall_pending flag is set, the IRQ is pending.
	 */
	struct gfn_to_hva_cache *ghc = &v->arch.xen.vcpu_info_cache;
	struct kvm_memslots *slots = kvm_memslots(v->kvm);
	bool ghc_valid = slots->generation == ghc->generation &&
		!kvm_is_error_hva(ghc->hva) && ghc->memslot;

	unsigned int offset = offsetof(struct vcpu_info, evtchn_upcall_pending);

	/* No need for compat handling here */
	BUILD_BUG_ON(offsetof(struct vcpu_info, evtchn_upcall_pending) !=
		     offsetof(struct compat_vcpu_info, evtchn_upcall_pending));
	BUILD_BUG_ON(sizeof(rc) !=
		     sizeof_field(struct vcpu_info, evtchn_upcall_pending));
	BUILD_BUG_ON(sizeof(rc) !=
		     sizeof_field(struct compat_vcpu_info, evtchn_upcall_pending));

	/*
	 * For efficiency, this mirrors the checks for using the valid
	 * cache in kvm_read_guest_offset_cached(), but just uses
	 * __get_user() instead. And falls back to the slow path.
	 */
	if (!evtchn_pending_sel && ghc_valid) {
		/* Fast path */
		pagefault_disable();
		err = __get_user(rc, (u8 __user *)ghc->hva + offset);
		pagefault_enable();
		if (!err)
			return rc;
	}

	/* Slow path */

	/*
	 * This function gets called from kvm_vcpu_block() after setting the
	 * task to TASK_INTERRUPTIBLE, to see if it needs to wake immediately
	 * from a HLT. So we really mustn't sleep. If the page ended up absent
	 * at that point, just return 1 in order to trigger an immediate wake,
	 * and we'll end up getting called again from a context where we *can*
	 * fault in the page and wait for it.
	 */
	if (atomic)
		return 1;

	if (!ghc_valid) {
		err = kvm_gfn_to_hva_cache_init(v->kvm, ghc, ghc->gpa, ghc->len);
		if (err || !ghc->memslot) {
			/*
			 * If this failed, userspace has screwed up the
			 * vcpu_info mapping. No interrupts for you.
			 */
			return 0;
		}
	}

	/*
	 * Now we have a valid (protected by srcu) userspace HVA in
	 * ghc->hva which points to the struct vcpu_info. If there
	 * are any bits in the in-kernel evtchn_pending_sel then
	 * we need to write those to the guest vcpu_info and set
	 * its evtchn_upcall_pending flag. If there aren't any bits
	 * to add, we only want to *check* evtchn_upcall_pending.
	 */
	if (evtchn_pending_sel) {
		bool long_mode = v->kvm->arch.xen.long_mode;

		if (!user_access_begin((void __user *)ghc->hva, sizeof(struct vcpu_info)))
			return 0;

		if (IS_ENABLED(CONFIG_64BIT) && long_mode) {
			struct vcpu_info __user *vi = (void __user *)ghc->hva;

			/* Attempt to set the evtchn_pending_sel bits in the
			 * guest, and if that succeeds then clear the same
			 * bits in the in-kernel version. */
			asm volatile("1:\t" LOCK_PREFIX "orq %0, %1\n"
				     "\tnotq %0\n"
				     "\t" LOCK_PREFIX "andq %0, %2\n"
				     "2:\n"
				     _ASM_EXTABLE_UA(1b, 2b)
				     : "=r" (evtchn_pending_sel),
				       "+m" (vi->evtchn_pending_sel),
				       "+m" (v->arch.xen.evtchn_pending_sel)
				     : "0" (evtchn_pending_sel));
		} else {
			struct compat_vcpu_info __user *vi = (void __user *)ghc->hva;
			u32 evtchn_pending_sel32 = evtchn_pending_sel;

			/* Attempt to set the evtchn_pending_sel bits in the
			 * guest, and if that succeeds then clear the same
			 * bits in the in-kernel version. */
			asm volatile("1:\t" LOCK_PREFIX "orl %0, %1\n"
				     "\tnotl %0\n"
				     "\t" LOCK_PREFIX "andl %0, %2\n"
				     "2:\n"
				     _ASM_EXTABLE_UA(1b, 2b)
				     : "=r" (evtchn_pending_sel32),
				       "+m" (vi->evtchn_pending_sel),
				       "+m" (v->arch.xen.evtchn_pending_sel)
				     : "0" (evtchn_pending_sel32));
		}
		rc = 1;
		unsafe_put_user(rc, (u8 __user *)ghc->hva + offset, err);

	err:
		user_access_end();

		mark_page_dirty_in_slot(v->kvm, ghc->memslot, ghc->gpa >> PAGE_SHIFT);
	} else {
		__get_user(rc, (u8 __user *)ghc->hva + offset);
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
		if (kvm->arch.xen.shinfo_cache.active)
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
}

void kvm_xen_destroy_vm(struct kvm *kvm)
{
	kvm_gfn_to_pfn_cache_destroy(kvm, &kvm->arch.xen.shinfo_cache);

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

	longmode = is_64_bit_hypercall(vcpu);
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

static inline int max_evtchn_port(struct kvm *kvm)
{
	if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode)
		return EVTCHN_2L_NR_CHANNELS;
	else
		return COMPAT_EVTCHN_2L_NR_CHANNELS;
}

/*
 * This follows the kvm_set_irq() API, so it returns:
 *  < 0   Interrupt was ignored (masked or not delivered for other reasons)
 *  = 0   Interrupt was coalesced (previous irq is still pending)
 *  > 0   Number of CPUs interrupt was delivered to
 */
int kvm_xen_set_evtchn_fast(struct kvm_kernel_irq_routing_entry *e,
			    struct kvm *kvm)
{
	struct gfn_to_pfn_cache *gpc = &kvm->arch.xen.shinfo_cache;
	struct kvm_vcpu *vcpu;
	unsigned long *pending_bits, *mask_bits;
	unsigned long flags;
	int port_word_bit;
	bool kick_vcpu = false;
	int idx;
	int rc;

	vcpu = kvm_get_vcpu_by_id(kvm, e->xen_evtchn.vcpu);
	if (!vcpu)
		return -1;

	if (!vcpu->arch.xen.vcpu_info_set)
		return -1;

	if (e->xen_evtchn.port >= max_evtchn_port(kvm))
		return -1;

	rc = -EWOULDBLOCK;
	read_lock_irqsave(&gpc->lock, flags);

	idx = srcu_read_lock(&kvm->srcu);
	if (!kvm_gfn_to_pfn_cache_check(kvm, gpc, gpc->gpa, PAGE_SIZE))
		goto out_rcu;

	if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode) {
		struct shared_info *shinfo = gpc->khva;
		pending_bits = (unsigned long *)&shinfo->evtchn_pending;
		mask_bits = (unsigned long *)&shinfo->evtchn_mask;
		port_word_bit = e->xen_evtchn.port / 64;
	} else {
		struct compat_shared_info *shinfo = gpc->khva;
		pending_bits = (unsigned long *)&shinfo->evtchn_pending;
		mask_bits = (unsigned long *)&shinfo->evtchn_mask;
		port_word_bit = e->xen_evtchn.port / 32;
	}

	/*
	 * If this port wasn't already set, and if it isn't masked, then
	 * we try to set the corresponding bit in the in-kernel shadow of
	 * evtchn_pending_sel for the target vCPU. And if *that* wasn't
	 * already set, then we kick the vCPU in question to write to the
	 * *real* evtchn_pending_sel in its own guest vcpu_info struct.
	 */
	if (test_and_set_bit(e->xen_evtchn.port, pending_bits)) {
		rc = 0; /* It was already raised */
	} else if (test_bit(e->xen_evtchn.port, mask_bits)) {
		rc = -1; /* Masked */
	} else {
		rc = 1; /* Delivered. But was the vCPU waking already? */
		if (!test_and_set_bit(port_word_bit, &vcpu->arch.xen.evtchn_pending_sel))
			kick_vcpu = true;
	}

 out_rcu:
	srcu_read_unlock(&kvm->srcu, idx);
	read_unlock_irqrestore(&gpc->lock, flags);

	if (kick_vcpu) {
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_vcpu_kick(vcpu);
	}

	return rc;
}

/* This is the version called from kvm_set_irq() as the .set function */
static int evtchn_set_fn(struct kvm_kernel_irq_routing_entry *e, struct kvm *kvm,
			 int irq_source_id, int level, bool line_status)
{
	bool mm_borrowed = false;
	int rc;

	if (!level)
		return -1;

	rc = kvm_xen_set_evtchn_fast(e, kvm);
	if (rc != -EWOULDBLOCK)
		return rc;

	if (current->mm != kvm->mm) {
		/*
		 * If not on a thread which already belongs to this KVM,
		 * we'd better be in the irqfd workqueue.
		 */
		if (WARN_ON_ONCE(current->mm))
			return -EINVAL;

		kthread_use_mm(kvm->mm);
		mm_borrowed = true;
	}

	/*
	 * For the irqfd workqueue, using the main kvm->lock mutex is
	 * fine since this function is invoked from kvm_set_irq() with
	 * no other lock held, no srcu. In future if it will be called
	 * directly from a vCPU thread (e.g. on hypercall for an IPI)
	 * then it may need to switch to using a leaf-node mutex for
	 * serializing the shared_info mapping.
	 */
	mutex_lock(&kvm->lock);

	/*
	 * It is theoretically possible for the page to be unmapped
	 * and the MMU notifier to invalidate the shared_info before
	 * we even get to use it. In that case, this looks like an
	 * infinite loop. It was tempting to do it via the userspace
	 * HVA instead... but that just *hides* the fact that it's
	 * an infinite loop, because if a fault occurs and it waits
	 * for the page to come back, it can *still* immediately
	 * fault and have to wait again, repeatedly.
	 *
	 * Conversely, the page could also have been reinstated by
	 * another thread before we even obtain the mutex above, so
	 * check again *first* before remapping it.
	 */
	do {
		struct gfn_to_pfn_cache *gpc = &kvm->arch.xen.shinfo_cache;
		int idx;

		rc = kvm_xen_set_evtchn_fast(e, kvm);
		if (rc != -EWOULDBLOCK)
			break;

		idx = srcu_read_lock(&kvm->srcu);
		rc = kvm_gfn_to_pfn_cache_refresh(kvm, gpc, gpc->gpa,
						  PAGE_SIZE, false);
		srcu_read_unlock(&kvm->srcu, idx);
	} while(!rc);

	mutex_unlock(&kvm->lock);

	if (mm_borrowed)
		kthread_unuse_mm(kvm->mm);

	return rc;
}

int kvm_xen_setup_evtchn(struct kvm *kvm,
			 struct kvm_kernel_irq_routing_entry *e,
			 const struct kvm_irq_routing_entry *ue)

{
	if (ue->u.xen_evtchn.port >= max_evtchn_port(kvm))
		return -EINVAL;

	/* We only support 2 level event channels for now */
	if (ue->u.xen_evtchn.priority != KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL)
		return -EINVAL;

	e->xen_evtchn.port = ue->u.xen_evtchn.port;
	e->xen_evtchn.vcpu = ue->u.xen_evtchn.vcpu;
	e->xen_evtchn.priority = ue->u.xen_evtchn.priority;
	e->set = evtchn_set_fn;

	return 0;
}
