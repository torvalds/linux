#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/hrtimer.h>
#include <asm/atomic.h>
#include "kvm_timer.h"

static int __kvm_timer_fn(struct kvm_vcpu *vcpu, struct kvm_timer *ktimer)
{
	int restart_timer = 0;
	wait_queue_head_t *q = &vcpu->wq;

	/* FIXME: this code should not know anything about vcpus */
	if (!atomic_inc_and_test(&ktimer->pending))
		set_bit(KVM_REQ_PENDING_TIMER, &vcpu->requests);

	if (!ktimer->reinject)
		atomic_set(&ktimer->pending, 1);

	if (waitqueue_active(q))
		wake_up_interruptible(q);

	if (ktimer->t_ops->is_periodic(ktimer)) {
		hrtimer_add_expires_ns(&ktimer->timer, ktimer->period);
		restart_timer = 1;
	}

	return restart_timer;
}

enum hrtimer_restart kvm_timer_fn(struct hrtimer *data)
{
	int restart_timer;
	struct kvm_vcpu *vcpu;
	struct kvm_timer *ktimer = container_of(data, struct kvm_timer, timer);

	vcpu = ktimer->vcpu;
	if (!vcpu)
		return HRTIMER_NORESTART;

	restart_timer = __kvm_timer_fn(vcpu, ktimer);
	if (restart_timer)
		return HRTIMER_RESTART;
	else
		return HRTIMER_NORESTART;
}

