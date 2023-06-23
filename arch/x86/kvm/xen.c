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
#include "lapic.h"

#include <linux/eventfd.h>
#include <linux/kvm_host.h>
#include <linux/sched/stat.h>

#include <trace/events/kvm.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/version.h>
#include <xen/interface/event_channel.h>
#include <xen/interface/sched.h>

#include "trace.h"

static int kvm_xen_set_evtchn(struct kvm_xen_evtchn *xe, struct kvm *kvm);
static int kvm_xen_setattr_evtchn(struct kvm *kvm, struct kvm_xen_hvm_attr *data);
static bool kvm_xen_hcall_evtchn_send(struct kvm_vcpu *vcpu, u64 param, u64 *r);

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
		kvm_gpc_deactivate(kvm, gpc);
		goto out;
	}

	do {
		ret = kvm_gpc_activate(kvm, gpc, NULL, KVM_HOST_USES_PFN, gpa,
				       PAGE_SIZE);
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

void kvm_xen_inject_timer_irqs(struct kvm_vcpu *vcpu)
{
	if (atomic_read(&vcpu->arch.xen.timer_pending) > 0) {
		struct kvm_xen_evtchn e;

		e.vcpu_id = vcpu->vcpu_id;
		e.vcpu_idx = vcpu->vcpu_idx;
		e.port = vcpu->arch.xen.timer_virq;
		e.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

		kvm_xen_set_evtchn(&e, vcpu->kvm);

		vcpu->arch.xen.timer_expires = 0;
		atomic_set(&vcpu->arch.xen.timer_pending, 0);
	}
}

static enum hrtimer_restart xen_timer_callback(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu = container_of(timer, struct kvm_vcpu,
					     arch.xen.timer);
	if (atomic_read(&vcpu->arch.xen.timer_pending))
		return HRTIMER_NORESTART;

	atomic_inc(&vcpu->arch.xen.timer_pending);
	kvm_make_request(KVM_REQ_UNBLOCK, vcpu);
	kvm_vcpu_kick(vcpu);

	return HRTIMER_NORESTART;
}

static void kvm_xen_start_timer(struct kvm_vcpu *vcpu, u64 guest_abs, s64 delta_ns)
{
	atomic_set(&vcpu->arch.xen.timer_pending, 0);
	vcpu->arch.xen.timer_expires = guest_abs;

	if (delta_ns <= 0) {
		xen_timer_callback(&vcpu->arch.xen.timer);
	} else {
		ktime_t ktime_now = ktime_get();
		hrtimer_start(&vcpu->arch.xen.timer,
			      ktime_add_ns(ktime_now, delta_ns),
			      HRTIMER_MODE_ABS_HARD);
	}
}

static void kvm_xen_stop_timer(struct kvm_vcpu *vcpu)
{
	hrtimer_cancel(&vcpu->arch.xen.timer);
	vcpu->arch.xen.timer_expires = 0;
	atomic_set(&vcpu->arch.xen.timer_pending, 0);
}

static void kvm_xen_init_timer(struct kvm_vcpu *vcpu)
{
	hrtimer_init(&vcpu->arch.xen.timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_ABS_HARD);
	vcpu->arch.xen.timer.function = xen_timer_callback;
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
	struct gfn_to_pfn_cache *gpc = &vx->runstate_cache;
	uint64_t *user_times;
	unsigned long flags;
	size_t user_len;
	int *user_state;

	kvm_xen_update_runstate(v, state);

	if (!vx->runstate_cache.active)
		return;

	if (IS_ENABLED(CONFIG_64BIT) && v->kvm->arch.xen.long_mode)
		user_len = sizeof(struct vcpu_runstate_info);
	else
		user_len = sizeof(struct compat_vcpu_runstate_info);

	read_lock_irqsave(&gpc->lock, flags);
	while (!kvm_gfn_to_pfn_cache_check(v->kvm, gpc, gpc->gpa,
					   user_len)) {
		read_unlock_irqrestore(&gpc->lock, flags);

		/* When invoked from kvm_sched_out() we cannot sleep */
		if (state == RUNSTATE_runnable)
			return;

		if (kvm_gfn_to_pfn_cache_refresh(v->kvm, gpc, gpc->gpa, user_len))
			return;

		read_lock_irqsave(&gpc->lock, flags);
	}

	/*
	 * The only difference between 32-bit and 64-bit versions of the
	 * runstate struct us the alignment of uint64_t in 32-bit, which
	 * means that the 64-bit version has an additional 4 bytes of
	 * padding after the first field 'state'.
	 *
	 * So we use 'int __user *user_state' to point to the state field,
	 * and 'uint64_t __user *user_times' for runstate_entry_time. So
	 * the actual array of time[] in each state starts at user_times[1].
	 */
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, state) != 0);
	BUILD_BUG_ON(offsetof(struct compat_vcpu_runstate_info, state) != 0);
	BUILD_BUG_ON(sizeof(struct compat_vcpu_runstate_info) != 0x2c);
#ifdef CONFIG_X86_64
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, state_entry_time) !=
		     offsetof(struct compat_vcpu_runstate_info, state_entry_time) + 4);
	BUILD_BUG_ON(offsetof(struct vcpu_runstate_info, time) !=
		     offsetof(struct compat_vcpu_runstate_info, time) + 4);
#endif

	user_state = gpc->khva;

	if (IS_ENABLED(CONFIG_64BIT) && v->kvm->arch.xen.long_mode)
		user_times = gpc->khva + offsetof(struct vcpu_runstate_info,
						  state_entry_time);
	else
		user_times = gpc->khva + offsetof(struct compat_vcpu_runstate_info,
						  state_entry_time);

	/*
	 * First write the updated state_entry_time at the appropriate
	 * location determined by 'offset'.
	 */
	BUILD_BUG_ON(sizeof_field(struct vcpu_runstate_info, state_entry_time) !=
		     sizeof(user_times[0]));
	BUILD_BUG_ON(sizeof_field(struct compat_vcpu_runstate_info, state_entry_time) !=
		     sizeof(user_times[0]));

	user_times[0] = vx->runstate_entry_time | XEN_RUNSTATE_UPDATE;
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

	*user_state = vx->current_runstate;

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

	memcpy(user_times + 1, vx->runstate_times, sizeof(vx->runstate_times));
	smp_wmb();

	/*
	 * Finally, clear the XEN_RUNSTATE_UPDATE bit in the guest's
	 * runstate_entry_time field.
	 */
	user_times[0] &= ~XEN_RUNSTATE_UPDATE;
	smp_wmb();

	read_unlock_irqrestore(&gpc->lock, flags);

	mark_page_dirty_in_slot(v->kvm, gpc->memslot, gpc->gpa >> PAGE_SHIFT);
}

static void kvm_xen_inject_vcpu_vector(struct kvm_vcpu *v)
{
	struct kvm_lapic_irq irq = { };
	int r;

	irq.dest_id = v->vcpu_id;
	irq.vector = v->arch.xen.upcall_vector;
	irq.dest_mode = APIC_DEST_PHYSICAL;
	irq.shorthand = APIC_DEST_NOSHORT;
	irq.delivery_mode = APIC_DM_FIXED;
	irq.level = 1;

	/* The fast version will always work for physical unicast */
	WARN_ON_ONCE(!kvm_irq_delivery_to_apic_fast(v->kvm, NULL, &irq, &r, NULL));
}

/*
 * On event channel delivery, the vcpu_info may not have been accessible.
 * In that case, there are bits in vcpu->arch.xen.evtchn_pending_sel which
 * need to be marked into the vcpu_info (and evtchn_upcall_pending set).
 * Do so now that we can sleep in the context of the vCPU to bring the
 * page in, and refresh the pfn cache for it.
 */
void kvm_xen_inject_pending_events(struct kvm_vcpu *v)
{
	unsigned long evtchn_pending_sel = READ_ONCE(v->arch.xen.evtchn_pending_sel);
	struct gfn_to_pfn_cache *gpc = &v->arch.xen.vcpu_info_cache;
	unsigned long flags;

	if (!evtchn_pending_sel)
		return;

	/*
	 * Yes, this is an open-coded loop. But that's just what put_user()
	 * does anyway. Page it in and retry the instruction. We're just a
	 * little more honest about it.
	 */
	read_lock_irqsave(&gpc->lock, flags);
	while (!kvm_gfn_to_pfn_cache_check(v->kvm, gpc, gpc->gpa,
					   sizeof(struct vcpu_info))) {
		read_unlock_irqrestore(&gpc->lock, flags);

		if (kvm_gfn_to_pfn_cache_refresh(v->kvm, gpc, gpc->gpa,
						 sizeof(struct vcpu_info)))
			return;

		read_lock_irqsave(&gpc->lock, flags);
	}

	/* Now gpc->khva is a valid kernel address for the vcpu_info */
	if (IS_ENABLED(CONFIG_64BIT) && v->kvm->arch.xen.long_mode) {
		struct vcpu_info *vi = gpc->khva;

		asm volatile(LOCK_PREFIX "orq %0, %1\n"
			     "notq %0\n"
			     LOCK_PREFIX "andq %0, %2\n"
			     : "=r" (evtchn_pending_sel),
			       "+m" (vi->evtchn_pending_sel),
			       "+m" (v->arch.xen.evtchn_pending_sel)
			     : "0" (evtchn_pending_sel));
		WRITE_ONCE(vi->evtchn_upcall_pending, 1);
	} else {
		u32 evtchn_pending_sel32 = evtchn_pending_sel;
		struct compat_vcpu_info *vi = gpc->khva;

		asm volatile(LOCK_PREFIX "orl %0, %1\n"
			     "notl %0\n"
			     LOCK_PREFIX "andl %0, %2\n"
			     : "=r" (evtchn_pending_sel32),
			       "+m" (vi->evtchn_pending_sel),
			       "+m" (v->arch.xen.evtchn_pending_sel)
			     : "0" (evtchn_pending_sel32));
		WRITE_ONCE(vi->evtchn_upcall_pending, 1);
	}
	read_unlock_irqrestore(&gpc->lock, flags);

	/* For the per-vCPU lapic vector, deliver it as MSI. */
	if (v->arch.xen.upcall_vector)
		kvm_xen_inject_vcpu_vector(v);

	mark_page_dirty_in_slot(v->kvm, gpc->memslot, gpc->gpa >> PAGE_SHIFT);
}

int __kvm_xen_has_interrupt(struct kvm_vcpu *v)
{
	struct gfn_to_pfn_cache *gpc = &v->arch.xen.vcpu_info_cache;
	unsigned long flags;
	u8 rc = 0;

	/*
	 * If the global upcall vector (HVMIRQ_callback_vector) is set and
	 * the vCPU's evtchn_upcall_pending flag is set, the IRQ is pending.
	 */

	/* No need for compat handling here */
	BUILD_BUG_ON(offsetof(struct vcpu_info, evtchn_upcall_pending) !=
		     offsetof(struct compat_vcpu_info, evtchn_upcall_pending));
	BUILD_BUG_ON(sizeof(rc) !=
		     sizeof_field(struct vcpu_info, evtchn_upcall_pending));
	BUILD_BUG_ON(sizeof(rc) !=
		     sizeof_field(struct compat_vcpu_info, evtchn_upcall_pending));

	read_lock_irqsave(&gpc->lock, flags);
	while (!kvm_gfn_to_pfn_cache_check(v->kvm, gpc, gpc->gpa,
					   sizeof(struct vcpu_info))) {
		read_unlock_irqrestore(&gpc->lock, flags);

		/*
		 * This function gets called from kvm_vcpu_block() after setting the
		 * task to TASK_INTERRUPTIBLE, to see if it needs to wake immediately
		 * from a HLT. So we really mustn't sleep. If the page ended up absent
		 * at that point, just return 1 in order to trigger an immediate wake,
		 * and we'll end up getting called again from a context where we *can*
		 * fault in the page and wait for it.
		 */
		if (in_atomic() || !task_is_running(current))
			return 1;

		if (kvm_gfn_to_pfn_cache_refresh(v->kvm, gpc, gpc->gpa,
						 sizeof(struct vcpu_info))) {
			/*
			 * If this failed, userspace has screwed up the
			 * vcpu_info mapping. No interrupts for you.
			 */
			return 0;
		}
		read_lock_irqsave(&gpc->lock, flags);
	}

	rc = ((struct vcpu_info *)gpc->khva)->evtchn_upcall_pending;
	read_unlock_irqrestore(&gpc->lock, flags);
	return rc;
}

int kvm_xen_hvm_set_attr(struct kvm *kvm, struct kvm_xen_hvm_attr *data)
{
	int r = -ENOENT;


	switch (data->type) {
	case KVM_XEN_ATTR_TYPE_LONG_MODE:
		if (!IS_ENABLED(CONFIG_64BIT) && data->u.long_mode) {
			r = -EINVAL;
		} else {
			mutex_lock(&kvm->lock);
			kvm->arch.xen.long_mode = !!data->u.long_mode;
			mutex_unlock(&kvm->lock);
			r = 0;
		}
		break;

	case KVM_XEN_ATTR_TYPE_SHARED_INFO:
		mutex_lock(&kvm->lock);
		r = kvm_xen_shared_info_init(kvm, data->u.shared_info.gfn);
		mutex_unlock(&kvm->lock);
		break;

	case KVM_XEN_ATTR_TYPE_UPCALL_VECTOR:
		if (data->u.vector && data->u.vector < 0x10)
			r = -EINVAL;
		else {
			mutex_lock(&kvm->lock);
			kvm->arch.xen.upcall_vector = data->u.vector;
			mutex_unlock(&kvm->lock);
			r = 0;
		}
		break;

	case KVM_XEN_ATTR_TYPE_EVTCHN:
		r = kvm_xen_setattr_evtchn(kvm, data);
		break;

	case KVM_XEN_ATTR_TYPE_XEN_VERSION:
		mutex_lock(&kvm->lock);
		kvm->arch.xen.xen_version = data->u.xen_version;
		mutex_unlock(&kvm->lock);
		r = 0;
		break;

	default:
		break;
	}

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

	case KVM_XEN_ATTR_TYPE_XEN_VERSION:
		data->u.xen_version = kvm->arch.xen.xen_version;
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
			kvm_gpc_deactivate(vcpu->kvm, &vcpu->arch.xen.vcpu_info_cache);
			r = 0;
			break;
		}

		r = kvm_gpc_activate(vcpu->kvm,
				     &vcpu->arch.xen.vcpu_info_cache, NULL,
				     KVM_HOST_USES_PFN, data->u.gpa,
				     sizeof(struct vcpu_info));
		if (!r)
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);

		break;

	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO:
		if (data->u.gpa == GPA_INVALID) {
			kvm_gpc_deactivate(vcpu->kvm,
					   &vcpu->arch.xen.vcpu_time_info_cache);
			r = 0;
			break;
		}

		r = kvm_gpc_activate(vcpu->kvm,
				     &vcpu->arch.xen.vcpu_time_info_cache,
				     NULL, KVM_HOST_USES_PFN, data->u.gpa,
				     sizeof(struct pvclock_vcpu_time_info));
		if (!r)
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR:
		if (!sched_info_on()) {
			r = -EOPNOTSUPP;
			break;
		}
		if (data->u.gpa == GPA_INVALID) {
			kvm_gpc_deactivate(vcpu->kvm,
					   &vcpu->arch.xen.runstate_cache);
			r = 0;
			break;
		}

		r = kvm_gpc_activate(vcpu->kvm, &vcpu->arch.xen.runstate_cache,
				     NULL, KVM_HOST_USES_PFN, data->u.gpa,
				     sizeof(struct vcpu_runstate_info));
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

	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_ID:
		if (data->u.vcpu_id >= KVM_MAX_VCPUS)
			r = -EINVAL;
		else {
			vcpu->arch.xen.vcpu_id = data->u.vcpu_id;
			r = 0;
		}
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_TIMER:
		if (data->u.timer.port &&
		    data->u.timer.priority != KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL) {
			r = -EINVAL;
			break;
		}

		if (!vcpu->arch.xen.timer.function)
			kvm_xen_init_timer(vcpu);

		/* Stop the timer (if it's running) before changing the vector */
		kvm_xen_stop_timer(vcpu);
		vcpu->arch.xen.timer_virq = data->u.timer.port;

		/* Start the timer if the new value has a valid vector+expiry. */
		if (data->u.timer.port && data->u.timer.expires_ns)
			kvm_xen_start_timer(vcpu, data->u.timer.expires_ns,
					    data->u.timer.expires_ns -
					    get_kvmclock_ns(vcpu->kvm));

		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_UPCALL_VECTOR:
		if (data->u.vector && data->u.vector < 0x10)
			r = -EINVAL;
		else {
			vcpu->arch.xen.upcall_vector = data->u.vector;
			r = 0;
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
		if (vcpu->arch.xen.vcpu_info_cache.active)
			data->u.gpa = vcpu->arch.xen.vcpu_info_cache.gpa;
		else
			data->u.gpa = GPA_INVALID;
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO:
		if (vcpu->arch.xen.vcpu_time_info_cache.active)
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
		if (vcpu->arch.xen.runstate_cache.active) {
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

	case KVM_XEN_VCPU_ATTR_TYPE_VCPU_ID:
		data->u.vcpu_id = vcpu->arch.xen.vcpu_id;
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_TIMER:
		data->u.timer.port = vcpu->arch.xen.timer_virq;
		data->u.timer.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;
		data->u.timer.expires_ns = vcpu->arch.xen.timer_expires;
		r = 0;
		break;

	case KVM_XEN_VCPU_ATTR_TYPE_UPCALL_VECTOR:
		data->u.vector = vcpu->arch.xen.upcall_vector;
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
		static_call(kvm_x86_patch_hypercall)(vcpu, instructions + 5);

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
	/* Only some feature flags need to be *enabled* by userspace */
	u32 permitted_flags = KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL |
		KVM_XEN_HVM_CONFIG_EVTCHN_SEND;

	if (xhc->flags & ~permitted_flags)
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

static inline int max_evtchn_port(struct kvm *kvm)
{
	if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode)
		return EVTCHN_2L_NR_CHANNELS;
	else
		return COMPAT_EVTCHN_2L_NR_CHANNELS;
}

static bool wait_pending_event(struct kvm_vcpu *vcpu, int nr_ports,
			       evtchn_port_t *ports)
{
	struct kvm *kvm = vcpu->kvm;
	struct gfn_to_pfn_cache *gpc = &kvm->arch.xen.shinfo_cache;
	unsigned long *pending_bits;
	unsigned long flags;
	bool ret = true;
	int idx, i;

	read_lock_irqsave(&gpc->lock, flags);
	idx = srcu_read_lock(&kvm->srcu);
	if (!kvm_gfn_to_pfn_cache_check(kvm, gpc, gpc->gpa, PAGE_SIZE))
		goto out_rcu;

	ret = false;
	if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode) {
		struct shared_info *shinfo = gpc->khva;
		pending_bits = (unsigned long *)&shinfo->evtchn_pending;
	} else {
		struct compat_shared_info *shinfo = gpc->khva;
		pending_bits = (unsigned long *)&shinfo->evtchn_pending;
	}

	for (i = 0; i < nr_ports; i++) {
		if (test_bit(ports[i], pending_bits)) {
			ret = true;
			break;
		}
	}

 out_rcu:
	srcu_read_unlock(&kvm->srcu, idx);
	read_unlock_irqrestore(&gpc->lock, flags);

	return ret;
}

static bool kvm_xen_schedop_poll(struct kvm_vcpu *vcpu, bool longmode,
				 u64 param, u64 *r)
{
	int idx, i;
	struct sched_poll sched_poll;
	evtchn_port_t port, *ports;
	gpa_t gpa;

	if (!longmode || !lapic_in_kernel(vcpu) ||
	    !(vcpu->kvm->arch.xen_hvm_config.flags & KVM_XEN_HVM_CONFIG_EVTCHN_SEND))
		return false;

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	gpa = kvm_mmu_gva_to_gpa_system(vcpu, param, NULL);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (!gpa || kvm_vcpu_read_guest(vcpu, gpa, &sched_poll,
					sizeof(sched_poll))) {
		*r = -EFAULT;
		return true;
	}

	if (unlikely(sched_poll.nr_ports > 1)) {
		/* Xen (unofficially) limits number of pollers to 128 */
		if (sched_poll.nr_ports > 128) {
			*r = -EINVAL;
			return true;
		}

		ports = kmalloc_array(sched_poll.nr_ports,
				      sizeof(*ports), GFP_KERNEL);
		if (!ports) {
			*r = -ENOMEM;
			return true;
		}
	} else
		ports = &port;

	for (i = 0; i < sched_poll.nr_ports; i++) {
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		gpa = kvm_mmu_gva_to_gpa_system(vcpu,
						(gva_t)(sched_poll.ports + i),
						NULL);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);

		if (!gpa || kvm_vcpu_read_guest(vcpu, gpa,
						&ports[i], sizeof(port))) {
			*r = -EFAULT;
			goto out;
		}
		if (ports[i] >= max_evtchn_port(vcpu->kvm)) {
			*r = -EINVAL;
			goto out;
		}
	}

	if (sched_poll.nr_ports == 1)
		vcpu->arch.xen.poll_evtchn = port;
	else
		vcpu->arch.xen.poll_evtchn = -1;

	set_bit(vcpu->vcpu_idx, vcpu->kvm->arch.xen.poll_mask);

	if (!wait_pending_event(vcpu, sched_poll.nr_ports, ports)) {
		vcpu->arch.mp_state = KVM_MP_STATE_HALTED;

		if (sched_poll.timeout)
			mod_timer(&vcpu->arch.xen.poll_timer,
				  jiffies + nsecs_to_jiffies(sched_poll.timeout));

		kvm_vcpu_halt(vcpu);

		if (sched_poll.timeout)
			del_timer(&vcpu->arch.xen.poll_timer);

		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	}

	vcpu->arch.xen.poll_evtchn = 0;
	*r = 0;
out:
	/* Really, this is only needed in case of timeout */
	clear_bit(vcpu->vcpu_idx, vcpu->kvm->arch.xen.poll_mask);

	if (unlikely(sched_poll.nr_ports > 1))
		kfree(ports);
	return true;
}

static void cancel_evtchn_poll(struct timer_list *t)
{
	struct kvm_vcpu *vcpu = from_timer(vcpu, t, arch.xen.poll_timer);

	kvm_make_request(KVM_REQ_UNBLOCK, vcpu);
	kvm_vcpu_kick(vcpu);
}

static bool kvm_xen_hcall_sched_op(struct kvm_vcpu *vcpu, bool longmode,
				   int cmd, u64 param, u64 *r)
{
	switch (cmd) {
	case SCHEDOP_poll:
		if (kvm_xen_schedop_poll(vcpu, longmode, param, r))
			return true;
		fallthrough;
	case SCHEDOP_yield:
		kvm_vcpu_on_spin(vcpu, true);
		*r = 0;
		return true;
	default:
		break;
	}

	return false;
}

struct compat_vcpu_set_singleshot_timer {
    uint64_t timeout_abs_ns;
    uint32_t flags;
} __attribute__((packed));

static bool kvm_xen_hcall_vcpu_op(struct kvm_vcpu *vcpu, bool longmode, int cmd,
				  int vcpu_id, u64 param, u64 *r)
{
	struct vcpu_set_singleshot_timer oneshot;
	s64 delta;
	gpa_t gpa;
	int idx;

	if (!kvm_xen_timer_enabled(vcpu))
		return false;

	switch (cmd) {
	case VCPUOP_set_singleshot_timer:
		if (vcpu->arch.xen.vcpu_id != vcpu_id) {
			*r = -EINVAL;
			return true;
		}
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		gpa = kvm_mmu_gva_to_gpa_system(vcpu, param, NULL);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);

		/*
		 * The only difference for 32-bit compat is the 4 bytes of
		 * padding after the interesting part of the structure. So
		 * for a faithful emulation of Xen we have to *try* to copy
		 * the padding and return -EFAULT if we can't. Otherwise we
		 * might as well just have copied the 12-byte 32-bit struct.
		 */
		BUILD_BUG_ON(offsetof(struct compat_vcpu_set_singleshot_timer, timeout_abs_ns) !=
			     offsetof(struct vcpu_set_singleshot_timer, timeout_abs_ns));
		BUILD_BUG_ON(sizeof_field(struct compat_vcpu_set_singleshot_timer, timeout_abs_ns) !=
			     sizeof_field(struct vcpu_set_singleshot_timer, timeout_abs_ns));
		BUILD_BUG_ON(offsetof(struct compat_vcpu_set_singleshot_timer, flags) !=
			     offsetof(struct vcpu_set_singleshot_timer, flags));
		BUILD_BUG_ON(sizeof_field(struct compat_vcpu_set_singleshot_timer, flags) !=
			     sizeof_field(struct vcpu_set_singleshot_timer, flags));

		if (!gpa ||
		    kvm_vcpu_read_guest(vcpu, gpa, &oneshot, longmode ? sizeof(oneshot) :
					sizeof(struct compat_vcpu_set_singleshot_timer))) {
			*r = -EFAULT;
			return true;
		}

		delta = oneshot.timeout_abs_ns - get_kvmclock_ns(vcpu->kvm);
		if ((oneshot.flags & VCPU_SSHOTTMR_future) && delta < 0) {
			*r = -ETIME;
			return true;
		}

		kvm_xen_start_timer(vcpu, oneshot.timeout_abs_ns, delta);
		*r = 0;
		return true;

	case VCPUOP_stop_singleshot_timer:
		if (vcpu->arch.xen.vcpu_id != vcpu_id) {
			*r = -EINVAL;
			return true;
		}
		kvm_xen_stop_timer(vcpu);
		*r = 0;
		return true;
	}

	return false;
}

static bool kvm_xen_hcall_set_timer_op(struct kvm_vcpu *vcpu, uint64_t timeout,
				       u64 *r)
{
	if (!kvm_xen_timer_enabled(vcpu))
		return false;

	if (timeout) {
		uint64_t guest_now = get_kvmclock_ns(vcpu->kvm);
		int64_t delta = timeout - guest_now;

		/* Xen has a 'Linux workaround' in do_set_timer_op() which
		 * checks for negative absolute timeout values (caused by
		 * integer overflow), and for values about 13 days in the
		 * future (2^50ns) which would be caused by jiffies
		 * overflow. For those cases, it sets the timeout 100ms in
		 * the future (not *too* soon, since if a guest really did
		 * set a long timeout on purpose we don't want to keep
		 * churning CPU time by waking it up).
		 */
		if (unlikely((int64_t)timeout < 0 ||
			     (delta > 0 && (uint32_t) (delta >> 50) != 0))) {
			delta = 100 * NSEC_PER_MSEC;
			timeout = guest_now + delta;
		}

		kvm_xen_start_timer(vcpu, timeout, delta);
	} else {
		kvm_xen_stop_timer(vcpu);
	}

	*r = 0;
	return true;
}

int kvm_xen_hypercall(struct kvm_vcpu *vcpu)
{
	bool longmode;
	u64 input, params[6], r = -ENOSYS;
	bool handled = false;
	u8 cpl;

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
	cpl = static_call(kvm_x86_get_cpl)(vcpu);
	trace_kvm_xen_hypercall(input, params[0], params[1], params[2],
				params[3], params[4], params[5]);

	/*
	 * Only allow hypercall acceleration for CPL0. The rare hypercalls that
	 * are permitted in guest userspace can be handled by the VMM.
	 */
	if (unlikely(cpl > 0))
		goto handle_in_userspace;

	switch (input) {
	case __HYPERVISOR_xen_version:
		if (params[0] == XENVER_version && vcpu->kvm->arch.xen.xen_version) {
			r = vcpu->kvm->arch.xen.xen_version;
			handled = true;
		}
		break;
	case __HYPERVISOR_event_channel_op:
		if (params[0] == EVTCHNOP_send)
			handled = kvm_xen_hcall_evtchn_send(vcpu, params[1], &r);
		break;
	case __HYPERVISOR_sched_op:
		handled = kvm_xen_hcall_sched_op(vcpu, longmode, params[0],
						 params[1], &r);
		break;
	case __HYPERVISOR_vcpu_op:
		handled = kvm_xen_hcall_vcpu_op(vcpu, longmode, params[0], params[1],
						params[2], &r);
		break;
	case __HYPERVISOR_set_timer_op: {
		u64 timeout = params[0];
		/* In 32-bit mode, the 64-bit timeout is in two 32-bit params. */
		if (!longmode)
			timeout |= params[1] << 32;
		handled = kvm_xen_hcall_set_timer_op(vcpu, timeout, &r);
		break;
	}
	default:
		break;
	}

	if (handled)
		return kvm_xen_hypercall_set_result(vcpu, r);

handle_in_userspace:
	vcpu->run->exit_reason = KVM_EXIT_XEN;
	vcpu->run->xen.type = KVM_EXIT_XEN_HCALL;
	vcpu->run->xen.u.hcall.longmode = longmode;
	vcpu->run->xen.u.hcall.cpl = cpl;
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

static void kvm_xen_check_poller(struct kvm_vcpu *vcpu, int port)
{
	int poll_evtchn = vcpu->arch.xen.poll_evtchn;

	if ((poll_evtchn == port || poll_evtchn == -1) &&
	    test_and_clear_bit(vcpu->vcpu_idx, vcpu->kvm->arch.xen.poll_mask)) {
		kvm_make_request(KVM_REQ_UNBLOCK, vcpu);
		kvm_vcpu_kick(vcpu);
	}
}

/*
 * The return value from this function is propagated to kvm_set_irq() API,
 * so it returns:
 *  < 0   Interrupt was ignored (masked or not delivered for other reasons)
 *  = 0   Interrupt was coalesced (previous irq is still pending)
 *  > 0   Number of CPUs interrupt was delivered to
 *
 * It is also called directly from kvm_arch_set_irq_inatomic(), where the
 * only check on its return value is a comparison with -EWOULDBLOCK'.
 */
int kvm_xen_set_evtchn_fast(struct kvm_xen_evtchn *xe, struct kvm *kvm)
{
	struct gfn_to_pfn_cache *gpc = &kvm->arch.xen.shinfo_cache;
	struct kvm_vcpu *vcpu;
	unsigned long *pending_bits, *mask_bits;
	unsigned long flags;
	int port_word_bit;
	bool kick_vcpu = false;
	int vcpu_idx, idx, rc;

	vcpu_idx = READ_ONCE(xe->vcpu_idx);
	if (vcpu_idx >= 0)
		vcpu = kvm_get_vcpu(kvm, vcpu_idx);
	else {
		vcpu = kvm_get_vcpu_by_id(kvm, xe->vcpu_id);
		if (!vcpu)
			return -EINVAL;
		WRITE_ONCE(xe->vcpu_idx, vcpu->vcpu_idx);
	}

	if (!vcpu->arch.xen.vcpu_info_cache.active)
		return -EINVAL;

	if (xe->port >= max_evtchn_port(kvm))
		return -EINVAL;

	rc = -EWOULDBLOCK;

	idx = srcu_read_lock(&kvm->srcu);

	read_lock_irqsave(&gpc->lock, flags);
	if (!kvm_gfn_to_pfn_cache_check(kvm, gpc, gpc->gpa, PAGE_SIZE))
		goto out_rcu;

	if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode) {
		struct shared_info *shinfo = gpc->khva;
		pending_bits = (unsigned long *)&shinfo->evtchn_pending;
		mask_bits = (unsigned long *)&shinfo->evtchn_mask;
		port_word_bit = xe->port / 64;
	} else {
		struct compat_shared_info *shinfo = gpc->khva;
		pending_bits = (unsigned long *)&shinfo->evtchn_pending;
		mask_bits = (unsigned long *)&shinfo->evtchn_mask;
		port_word_bit = xe->port / 32;
	}

	/*
	 * If this port wasn't already set, and if it isn't masked, then
	 * we try to set the corresponding bit in the in-kernel shadow of
	 * evtchn_pending_sel for the target vCPU. And if *that* wasn't
	 * already set, then we kick the vCPU in question to write to the
	 * *real* evtchn_pending_sel in its own guest vcpu_info struct.
	 */
	if (test_and_set_bit(xe->port, pending_bits)) {
		rc = 0; /* It was already raised */
	} else if (test_bit(xe->port, mask_bits)) {
		rc = -ENOTCONN; /* Masked */
		kvm_xen_check_poller(vcpu, xe->port);
	} else {
		rc = 1; /* Delivered to the bitmap in shared_info. */
		/* Now switch to the vCPU's vcpu_info to set the index and pending_sel */
		read_unlock_irqrestore(&gpc->lock, flags);
		gpc = &vcpu->arch.xen.vcpu_info_cache;

		read_lock_irqsave(&gpc->lock, flags);
		if (!kvm_gfn_to_pfn_cache_check(kvm, gpc, gpc->gpa, sizeof(struct vcpu_info))) {
			/*
			 * Could not access the vcpu_info. Set the bit in-kernel
			 * and prod the vCPU to deliver it for itself.
			 */
			if (!test_and_set_bit(port_word_bit, &vcpu->arch.xen.evtchn_pending_sel))
				kick_vcpu = true;
			goto out_rcu;
		}

		if (IS_ENABLED(CONFIG_64BIT) && kvm->arch.xen.long_mode) {
			struct vcpu_info *vcpu_info = gpc->khva;
			if (!test_and_set_bit(port_word_bit, &vcpu_info->evtchn_pending_sel)) {
				WRITE_ONCE(vcpu_info->evtchn_upcall_pending, 1);
				kick_vcpu = true;
			}
		} else {
			struct compat_vcpu_info *vcpu_info = gpc->khva;
			if (!test_and_set_bit(port_word_bit,
					      (unsigned long *)&vcpu_info->evtchn_pending_sel)) {
				WRITE_ONCE(vcpu_info->evtchn_upcall_pending, 1);
				kick_vcpu = true;
			}
		}

		/* For the per-vCPU lapic vector, deliver it as MSI. */
		if (kick_vcpu && vcpu->arch.xen.upcall_vector) {
			kvm_xen_inject_vcpu_vector(vcpu);
			kick_vcpu = false;
		}
	}

 out_rcu:
	read_unlock_irqrestore(&gpc->lock, flags);
	srcu_read_unlock(&kvm->srcu, idx);

	if (kick_vcpu) {
		kvm_make_request(KVM_REQ_UNBLOCK, vcpu);
		kvm_vcpu_kick(vcpu);
	}

	return rc;
}

static int kvm_xen_set_evtchn(struct kvm_xen_evtchn *xe, struct kvm *kvm)
{
	bool mm_borrowed = false;
	int rc;

	rc = kvm_xen_set_evtchn_fast(xe, kvm);
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

		rc = kvm_xen_set_evtchn_fast(xe, kvm);
		if (rc != -EWOULDBLOCK)
			break;

		idx = srcu_read_lock(&kvm->srcu);
		rc = kvm_gfn_to_pfn_cache_refresh(kvm, gpc, gpc->gpa, PAGE_SIZE);
		srcu_read_unlock(&kvm->srcu, idx);
	} while(!rc);

	mutex_unlock(&kvm->lock);

	if (mm_borrowed)
		kthread_unuse_mm(kvm->mm);

	return rc;
}

/* This is the version called from kvm_set_irq() as the .set function */
static int evtchn_set_fn(struct kvm_kernel_irq_routing_entry *e, struct kvm *kvm,
			 int irq_source_id, int level, bool line_status)
{
	if (!level)
		return -EINVAL;

	return kvm_xen_set_evtchn(&e->xen_evtchn, kvm);
}

/*
 * Set up an event channel interrupt from the KVM IRQ routing table.
 * Used for e.g. PIRQ from passed through physical devices.
 */
int kvm_xen_setup_evtchn(struct kvm *kvm,
			 struct kvm_kernel_irq_routing_entry *e,
			 const struct kvm_irq_routing_entry *ue)

{
	struct kvm_vcpu *vcpu;

	if (ue->u.xen_evtchn.port >= max_evtchn_port(kvm))
		return -EINVAL;

	/* We only support 2 level event channels for now */
	if (ue->u.xen_evtchn.priority != KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL)
		return -EINVAL;

	/*
	 * Xen gives us interesting mappings from vCPU index to APIC ID,
	 * which means kvm_get_vcpu_by_id() has to iterate over all vCPUs
	 * to find it. Do that once at setup time, instead of every time.
	 * But beware that on live update / live migration, the routing
	 * table might be reinstated before the vCPU threads have finished
	 * recreating their vCPUs.
	 */
	vcpu = kvm_get_vcpu_by_id(kvm, ue->u.xen_evtchn.vcpu);
	if (vcpu)
		e->xen_evtchn.vcpu_idx = vcpu->vcpu_idx;
	else
		e->xen_evtchn.vcpu_idx = -1;

	e->xen_evtchn.port = ue->u.xen_evtchn.port;
	e->xen_evtchn.vcpu_id = ue->u.xen_evtchn.vcpu;
	e->xen_evtchn.priority = ue->u.xen_evtchn.priority;
	e->set = evtchn_set_fn;

	return 0;
}

/*
 * Explicit event sending from userspace with KVM_XEN_HVM_EVTCHN_SEND ioctl.
 */
int kvm_xen_hvm_evtchn_send(struct kvm *kvm, struct kvm_irq_routing_xen_evtchn *uxe)
{
	struct kvm_xen_evtchn e;
	int ret;

	if (!uxe->port || uxe->port >= max_evtchn_port(kvm))
		return -EINVAL;

	/* We only support 2 level event channels for now */
	if (uxe->priority != KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL)
		return -EINVAL;

	e.port = uxe->port;
	e.vcpu_id = uxe->vcpu;
	e.vcpu_idx = -1;
	e.priority = uxe->priority;

	ret = kvm_xen_set_evtchn(&e, kvm);

	/*
	 * None of that 'return 1 if it actually got delivered' nonsense.
	 * We don't care if it was masked (-ENOTCONN) either.
	 */
	if (ret > 0 || ret == -ENOTCONN)
		ret = 0;

	return ret;
}

/*
 * Support for *outbound* event channel events via the EVTCHNOP_send hypercall.
 */
struct evtchnfd {
	u32 send_port;
	u32 type;
	union {
		struct kvm_xen_evtchn port;
		struct {
			u32 port; /* zero */
			struct eventfd_ctx *ctx;
		} eventfd;
	} deliver;
};

/*
 * Update target vCPU or priority for a registered sending channel.
 */
static int kvm_xen_eventfd_update(struct kvm *kvm,
				  struct kvm_xen_hvm_attr *data)
{
	u32 port = data->u.evtchn.send_port;
	struct evtchnfd *evtchnfd;

	if (!port || port >= max_evtchn_port(kvm))
		return -EINVAL;

	mutex_lock(&kvm->lock);
	evtchnfd = idr_find(&kvm->arch.xen.evtchn_ports, port);
	mutex_unlock(&kvm->lock);

	if (!evtchnfd)
		return -ENOENT;

	/* For an UPDATE, nothing may change except the priority/vcpu */
	if (evtchnfd->type != data->u.evtchn.type)
		return -EINVAL;

	/*
	 * Port cannot change, and if it's zero that was an eventfd
	 * which can't be changed either.
	 */
	if (!evtchnfd->deliver.port.port ||
	    evtchnfd->deliver.port.port != data->u.evtchn.deliver.port.port)
		return -EINVAL;

	/* We only support 2 level event channels for now */
	if (data->u.evtchn.deliver.port.priority != KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL)
		return -EINVAL;

	mutex_lock(&kvm->lock);
	evtchnfd->deliver.port.priority = data->u.evtchn.deliver.port.priority;
	if (evtchnfd->deliver.port.vcpu_id != data->u.evtchn.deliver.port.vcpu) {
		evtchnfd->deliver.port.vcpu_id = data->u.evtchn.deliver.port.vcpu;
		evtchnfd->deliver.port.vcpu_idx = -1;
	}
	mutex_unlock(&kvm->lock);
	return 0;
}

/*
 * Configure the target (eventfd or local port delivery) for sending on
 * a given event channel.
 */
static int kvm_xen_eventfd_assign(struct kvm *kvm,
				  struct kvm_xen_hvm_attr *data)
{
	u32 port = data->u.evtchn.send_port;
	struct eventfd_ctx *eventfd = NULL;
	struct evtchnfd *evtchnfd = NULL;
	int ret = -EINVAL;

	if (!port || port >= max_evtchn_port(kvm))
		return -EINVAL;

	evtchnfd = kzalloc(sizeof(struct evtchnfd), GFP_KERNEL);
	if (!evtchnfd)
		return -ENOMEM;

	switch(data->u.evtchn.type) {
	case EVTCHNSTAT_ipi:
		/* IPI  must map back to the same port# */
		if (data->u.evtchn.deliver.port.port != data->u.evtchn.send_port)
			goto out_noeventfd; /* -EINVAL */
		break;

	case EVTCHNSTAT_interdomain:
		if (data->u.evtchn.deliver.port.port) {
			if (data->u.evtchn.deliver.port.port >= max_evtchn_port(kvm))
				goto out_noeventfd; /* -EINVAL */
		} else {
			eventfd = eventfd_ctx_fdget(data->u.evtchn.deliver.eventfd.fd);
			if (IS_ERR(eventfd)) {
				ret = PTR_ERR(eventfd);
				goto out_noeventfd;
			}
		}
		break;

	case EVTCHNSTAT_virq:
	case EVTCHNSTAT_closed:
	case EVTCHNSTAT_unbound:
	case EVTCHNSTAT_pirq:
	default: /* Unknown event channel type */
		goto out; /* -EINVAL */
	}

	evtchnfd->send_port = data->u.evtchn.send_port;
	evtchnfd->type = data->u.evtchn.type;
	if (eventfd) {
		evtchnfd->deliver.eventfd.ctx = eventfd;
	} else {
		/* We only support 2 level event channels for now */
		if (data->u.evtchn.deliver.port.priority != KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL)
			goto out; /* -EINVAL; */

		evtchnfd->deliver.port.port = data->u.evtchn.deliver.port.port;
		evtchnfd->deliver.port.vcpu_id = data->u.evtchn.deliver.port.vcpu;
		evtchnfd->deliver.port.vcpu_idx = -1;
		evtchnfd->deliver.port.priority = data->u.evtchn.deliver.port.priority;
	}

	mutex_lock(&kvm->lock);
	ret = idr_alloc(&kvm->arch.xen.evtchn_ports, evtchnfd, port, port + 1,
			GFP_KERNEL);
	mutex_unlock(&kvm->lock);
	if (ret >= 0)
		return 0;

	if (ret == -ENOSPC)
		ret = -EEXIST;
out:
	if (eventfd)
		eventfd_ctx_put(eventfd);
out_noeventfd:
	kfree(evtchnfd);
	return ret;
}

static int kvm_xen_eventfd_deassign(struct kvm *kvm, u32 port)
{
	struct evtchnfd *evtchnfd;

	mutex_lock(&kvm->lock);
	evtchnfd = idr_remove(&kvm->arch.xen.evtchn_ports, port);
	mutex_unlock(&kvm->lock);

	if (!evtchnfd)
		return -ENOENT;

	if (kvm)
		synchronize_srcu(&kvm->srcu);
	if (!evtchnfd->deliver.port.port)
		eventfd_ctx_put(evtchnfd->deliver.eventfd.ctx);
	kfree(evtchnfd);
	return 0;
}

static int kvm_xen_eventfd_reset(struct kvm *kvm)
{
	struct evtchnfd *evtchnfd;
	int i;

	mutex_lock(&kvm->lock);
	idr_for_each_entry(&kvm->arch.xen.evtchn_ports, evtchnfd, i) {
		idr_remove(&kvm->arch.xen.evtchn_ports, evtchnfd->send_port);
		synchronize_srcu(&kvm->srcu);
		if (!evtchnfd->deliver.port.port)
			eventfd_ctx_put(evtchnfd->deliver.eventfd.ctx);
		kfree(evtchnfd);
	}
	mutex_unlock(&kvm->lock);

	return 0;
}

static int kvm_xen_setattr_evtchn(struct kvm *kvm, struct kvm_xen_hvm_attr *data)
{
	u32 port = data->u.evtchn.send_port;

	if (data->u.evtchn.flags == KVM_XEN_EVTCHN_RESET)
		return kvm_xen_eventfd_reset(kvm);

	if (!port || port >= max_evtchn_port(kvm))
		return -EINVAL;

	if (data->u.evtchn.flags == KVM_XEN_EVTCHN_DEASSIGN)
		return kvm_xen_eventfd_deassign(kvm, port);
	if (data->u.evtchn.flags == KVM_XEN_EVTCHN_UPDATE)
		return kvm_xen_eventfd_update(kvm, data);
	if (data->u.evtchn.flags)
		return -EINVAL;

	return kvm_xen_eventfd_assign(kvm, data);
}

static bool kvm_xen_hcall_evtchn_send(struct kvm_vcpu *vcpu, u64 param, u64 *r)
{
	struct evtchnfd *evtchnfd;
	struct evtchn_send send;
	gpa_t gpa;
	int idx;

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	gpa = kvm_mmu_gva_to_gpa_system(vcpu, param, NULL);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (!gpa || kvm_vcpu_read_guest(vcpu, gpa, &send, sizeof(send))) {
		*r = -EFAULT;
		return true;
	}

	/* The evtchn_ports idr is protected by vcpu->kvm->srcu */
	evtchnfd = idr_find(&vcpu->kvm->arch.xen.evtchn_ports, send.port);
	if (!evtchnfd)
		return false;

	if (evtchnfd->deliver.port.port) {
		int ret = kvm_xen_set_evtchn(&evtchnfd->deliver.port, vcpu->kvm);
		if (ret < 0 && ret != -ENOTCONN)
			return false;
	} else {
		eventfd_signal(evtchnfd->deliver.eventfd.ctx, 1);
	}

	*r = 0;
	return true;
}

void kvm_xen_init_vcpu(struct kvm_vcpu *vcpu)
{
	vcpu->arch.xen.vcpu_id = vcpu->vcpu_idx;
	vcpu->arch.xen.poll_evtchn = 0;

	timer_setup(&vcpu->arch.xen.poll_timer, cancel_evtchn_poll, 0);

	kvm_gpc_init(&vcpu->arch.xen.runstate_cache);
	kvm_gpc_init(&vcpu->arch.xen.vcpu_info_cache);
	kvm_gpc_init(&vcpu->arch.xen.vcpu_time_info_cache);
}

void kvm_xen_destroy_vcpu(struct kvm_vcpu *vcpu)
{
	if (kvm_xen_timer_enabled(vcpu))
		kvm_xen_stop_timer(vcpu);

	kvm_gpc_deactivate(vcpu->kvm, &vcpu->arch.xen.runstate_cache);
	kvm_gpc_deactivate(vcpu->kvm, &vcpu->arch.xen.vcpu_info_cache);
	kvm_gpc_deactivate(vcpu->kvm, &vcpu->arch.xen.vcpu_time_info_cache);

	del_timer_sync(&vcpu->arch.xen.poll_timer);
}

void kvm_xen_init_vm(struct kvm *kvm)
{
	idr_init(&kvm->arch.xen.evtchn_ports);
	kvm_gpc_init(&kvm->arch.xen.shinfo_cache);
}

void kvm_xen_destroy_vm(struct kvm *kvm)
{
	struct evtchnfd *evtchnfd;
	int i;

	kvm_gpc_deactivate(kvm, &kvm->arch.xen.shinfo_cache);

	idr_for_each_entry(&kvm->arch.xen.evtchn_ports, evtchnfd, i) {
		if (!evtchnfd->deliver.port.port)
			eventfd_ctx_put(evtchnfd->deliver.eventfd.ctx);
		kfree(evtchnfd);
	}
	idr_destroy(&kvm->arch.xen.evtchn_ports);

	if (kvm->arch.xen_hvm_config.msr)
		static_branch_slow_dec_deferred(&kvm_xen_enabled);
}
