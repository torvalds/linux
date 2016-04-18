/*
 * 8253/8254 interval timer emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2006 Intel Corporation
 * Copyright (c) 2007 Keir Fraser, XenSource Inc
 * Copyright (c) 2008 Intel Corporation
 * Copyright 2009 Red Hat, Inc. and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Authors:
 *   Sheng Yang <sheng.yang@intel.com>
 *   Based on QEMU and Xen.
 */

#define pr_fmt(fmt) "pit: " fmt

#include <linux/kvm_host.h>
#include <linux/slab.h>

#include "ioapic.h"
#include "irq.h"
#include "i8254.h"
#include "x86.h"

#ifndef CONFIG_X86_64
#define mod_64(x, y) ((x) - (y) * div64_u64(x, y))
#else
#define mod_64(x, y) ((x) % (y))
#endif

#define RW_STATE_LSB 1
#define RW_STATE_MSB 2
#define RW_STATE_WORD0 3
#define RW_STATE_WORD1 4

static void pit_set_gate(struct kvm_pit *pit, int channel, u32 val)
{
	struct kvm_kpit_channel_state *c = &pit->pit_state.channels[channel];

	switch (c->mode) {
	default:
	case 0:
	case 4:
		/* XXX: just disable/enable counting */
		break;
	case 1:
	case 2:
	case 3:
	case 5:
		/* Restart counting on rising edge. */
		if (c->gate < val)
			c->count_load_time = ktime_get();
		break;
	}

	c->gate = val;
}

static int pit_get_gate(struct kvm_pit *pit, int channel)
{
	return pit->pit_state.channels[channel].gate;
}

static s64 __kpit_elapsed(struct kvm_pit *pit)
{
	s64 elapsed;
	ktime_t remaining;
	struct kvm_kpit_state *ps = &pit->pit_state;

	if (!ps->period)
		return 0;

	/*
	 * The Counter does not stop when it reaches zero. In
	 * Modes 0, 1, 4, and 5 the Counter ``wraps around'' to
	 * the highest count, either FFFF hex for binary counting
	 * or 9999 for BCD counting, and continues counting.
	 * Modes 2 and 3 are periodic; the Counter reloads
	 * itself with the initial count and continues counting
	 * from there.
	 */
	remaining = hrtimer_get_remaining(&ps->timer);
	elapsed = ps->period - ktime_to_ns(remaining);

	return elapsed;
}

static s64 kpit_elapsed(struct kvm_pit *pit, struct kvm_kpit_channel_state *c,
			int channel)
{
	if (channel == 0)
		return __kpit_elapsed(pit);

	return ktime_to_ns(ktime_sub(ktime_get(), c->count_load_time));
}

static int pit_get_count(struct kvm_pit *pit, int channel)
{
	struct kvm_kpit_channel_state *c = &pit->pit_state.channels[channel];
	s64 d, t;
	int counter;

	t = kpit_elapsed(pit, c, channel);
	d = mul_u64_u32_div(t, KVM_PIT_FREQ, NSEC_PER_SEC);

	switch (c->mode) {
	case 0:
	case 1:
	case 4:
	case 5:
		counter = (c->count - d) & 0xffff;
		break;
	case 3:
		/* XXX: may be incorrect for odd counts */
		counter = c->count - (mod_64((2 * d), c->count));
		break;
	default:
		counter = c->count - mod_64(d, c->count);
		break;
	}
	return counter;
}

static int pit_get_out(struct kvm_pit *pit, int channel)
{
	struct kvm_kpit_channel_state *c = &pit->pit_state.channels[channel];
	s64 d, t;
	int out;

	t = kpit_elapsed(pit, c, channel);
	d = mul_u64_u32_div(t, KVM_PIT_FREQ, NSEC_PER_SEC);

	switch (c->mode) {
	default:
	case 0:
		out = (d >= c->count);
		break;
	case 1:
		out = (d < c->count);
		break;
	case 2:
		out = ((mod_64(d, c->count) == 0) && (d != 0));
		break;
	case 3:
		out = (mod_64(d, c->count) < ((c->count + 1) >> 1));
		break;
	case 4:
	case 5:
		out = (d == c->count);
		break;
	}

	return out;
}

static void pit_latch_count(struct kvm_pit *pit, int channel)
{
	struct kvm_kpit_channel_state *c = &pit->pit_state.channels[channel];

	if (!c->count_latched) {
		c->latched_count = pit_get_count(pit, channel);
		c->count_latched = c->rw_mode;
	}
}

static void pit_latch_status(struct kvm_pit *pit, int channel)
{
	struct kvm_kpit_channel_state *c = &pit->pit_state.channels[channel];

	if (!c->status_latched) {
		/* TODO: Return NULL COUNT (bit 6). */
		c->status = ((pit_get_out(pit, channel) << 7) |
				(c->rw_mode << 4) |
				(c->mode << 1) |
				c->bcd);
		c->status_latched = 1;
	}
}

static inline struct kvm_pit *pit_state_to_pit(struct kvm_kpit_state *ps)
{
	return container_of(ps, struct kvm_pit, pit_state);
}

static void kvm_pit_ack_irq(struct kvm_irq_ack_notifier *kian)
{
	struct kvm_kpit_state *ps = container_of(kian, struct kvm_kpit_state,
						 irq_ack_notifier);
	struct kvm_pit *pit = pit_state_to_pit(ps);

	atomic_set(&ps->irq_ack, 1);
	/* irq_ack should be set before pending is read.  Order accesses with
	 * inc(pending) in pit_timer_fn and xchg(irq_ack, 0) in pit_do_work.
	 */
	smp_mb();
	if (atomic_dec_if_positive(&ps->pending) > 0)
		queue_kthread_work(&pit->worker, &pit->expired);
}

void __kvm_migrate_pit_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_pit *pit = vcpu->kvm->arch.vpit;
	struct hrtimer *timer;

	if (!kvm_vcpu_is_bsp(vcpu) || !pit)
		return;

	timer = &pit->pit_state.timer;
	mutex_lock(&pit->pit_state.lock);
	if (hrtimer_cancel(timer))
		hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
	mutex_unlock(&pit->pit_state.lock);
}

static void destroy_pit_timer(struct kvm_pit *pit)
{
	hrtimer_cancel(&pit->pit_state.timer);
	flush_kthread_work(&pit->expired);
}

static void pit_do_work(struct kthread_work *work)
{
	struct kvm_pit *pit = container_of(work, struct kvm_pit, expired);
	struct kvm *kvm = pit->kvm;
	struct kvm_vcpu *vcpu;
	int i;
	struct kvm_kpit_state *ps = &pit->pit_state;

	if (atomic_read(&ps->reinject) && !atomic_xchg(&ps->irq_ack, 0))
		return;

	kvm_set_irq(kvm, pit->irq_source_id, 0, 1, false);
	kvm_set_irq(kvm, pit->irq_source_id, 0, 0, false);

	/*
	 * Provides NMI watchdog support via Virtual Wire mode.
	 * The route is: PIT -> LVT0 in NMI mode.
	 *
	 * Note: Our Virtual Wire implementation does not follow
	 * the MP specification.  We propagate a PIT interrupt to all
	 * VCPUs and only when LVT0 is in NMI mode.  The interrupt can
	 * also be simultaneously delivered through PIC and IOAPIC.
	 */
	if (atomic_read(&kvm->arch.vapics_in_nmi_mode) > 0)
		kvm_for_each_vcpu(i, vcpu, kvm)
			kvm_apic_nmi_wd_deliver(vcpu);
}

static enum hrtimer_restart pit_timer_fn(struct hrtimer *data)
{
	struct kvm_kpit_state *ps = container_of(data, struct kvm_kpit_state, timer);
	struct kvm_pit *pt = pit_state_to_pit(ps);

	if (atomic_read(&ps->reinject))
		atomic_inc(&ps->pending);

	queue_kthread_work(&pt->worker, &pt->expired);

	if (ps->is_periodic) {
		hrtimer_add_expires_ns(&ps->timer, ps->period);
		return HRTIMER_RESTART;
	} else
		return HRTIMER_NORESTART;
}

static inline void kvm_pit_reset_reinject(struct kvm_pit *pit)
{
	atomic_set(&pit->pit_state.pending, 0);
	atomic_set(&pit->pit_state.irq_ack, 1);
}

void kvm_pit_set_reinject(struct kvm_pit *pit, bool reinject)
{
	struct kvm_kpit_state *ps = &pit->pit_state;
	struct kvm *kvm = pit->kvm;

	if (atomic_read(&ps->reinject) == reinject)
		return;

	if (reinject) {
		/* The initial state is preserved while ps->reinject == 0. */
		kvm_pit_reset_reinject(pit);
		kvm_register_irq_ack_notifier(kvm, &ps->irq_ack_notifier);
		kvm_register_irq_mask_notifier(kvm, 0, &pit->mask_notifier);
	} else {
		kvm_unregister_irq_ack_notifier(kvm, &ps->irq_ack_notifier);
		kvm_unregister_irq_mask_notifier(kvm, 0, &pit->mask_notifier);
	}

	atomic_set(&ps->reinject, reinject);
}

static void create_pit_timer(struct kvm_pit *pit, u32 val, int is_period)
{
	struct kvm_kpit_state *ps = &pit->pit_state;
	struct kvm *kvm = pit->kvm;
	s64 interval;

	if (!ioapic_in_kernel(kvm) ||
	    ps->flags & KVM_PIT_FLAGS_HPET_LEGACY)
		return;

	interval = mul_u64_u32_div(val, NSEC_PER_SEC, KVM_PIT_FREQ);

	pr_debug("create pit timer, interval is %llu nsec\n", interval);

	/* TODO The new value only affected after the retriggered */
	hrtimer_cancel(&ps->timer);
	flush_kthread_work(&pit->expired);
	ps->period = interval;
	ps->is_periodic = is_period;

	kvm_pit_reset_reinject(pit);

	/*
	 * Do not allow the guest to program periodic timers with small
	 * interval, since the hrtimers are not throttled by the host
	 * scheduler.
	 */
	if (ps->is_periodic) {
		s64 min_period = min_timer_period_us * 1000LL;

		if (ps->period < min_period) {
			pr_info_ratelimited(
			    "kvm: requested %lld ns "
			    "i8254 timer period limited to %lld ns\n",
			    ps->period, min_period);
			ps->period = min_period;
		}
	}

	hrtimer_start(&ps->timer, ktime_add_ns(ktime_get(), interval),
		      HRTIMER_MODE_ABS);
}

static void pit_load_count(struct kvm_pit *pit, int channel, u32 val)
{
	struct kvm_kpit_state *ps = &pit->pit_state;

	pr_debug("load_count val is %d, channel is %d\n", val, channel);

	/*
	 * The largest possible initial count is 0; this is equivalent
	 * to 216 for binary counting and 104 for BCD counting.
	 */
	if (val == 0)
		val = 0x10000;

	ps->channels[channel].count = val;

	if (channel != 0) {
		ps->channels[channel].count_load_time = ktime_get();
		return;
	}

	/* Two types of timer
	 * mode 1 is one shot, mode 2 is period, otherwise del timer */
	switch (ps->channels[0].mode) {
	case 0:
	case 1:
        /* FIXME: enhance mode 4 precision */
	case 4:
		create_pit_timer(pit, val, 0);
		break;
	case 2:
	case 3:
		create_pit_timer(pit, val, 1);
		break;
	default:
		destroy_pit_timer(pit);
	}
}

void kvm_pit_load_count(struct kvm_pit *pit, int channel, u32 val,
		int hpet_legacy_start)
{
	u8 saved_mode;

	WARN_ON_ONCE(!mutex_is_locked(&pit->pit_state.lock));

	if (hpet_legacy_start) {
		/* save existing mode for later reenablement */
		WARN_ON(channel != 0);
		saved_mode = pit->pit_state.channels[0].mode;
		pit->pit_state.channels[0].mode = 0xff; /* disable timer */
		pit_load_count(pit, channel, val);
		pit->pit_state.channels[0].mode = saved_mode;
	} else {
		pit_load_count(pit, channel, val);
	}
}

static inline struct kvm_pit *dev_to_pit(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_pit, dev);
}

static inline struct kvm_pit *speaker_to_pit(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_pit, speaker_dev);
}

static inline int pit_in_range(gpa_t addr)
{
	return ((addr >= KVM_PIT_BASE_ADDRESS) &&
		(addr < KVM_PIT_BASE_ADDRESS + KVM_PIT_MEM_LENGTH));
}

static int pit_ioport_write(struct kvm_vcpu *vcpu,
				struct kvm_io_device *this,
			    gpa_t addr, int len, const void *data)
{
	struct kvm_pit *pit = dev_to_pit(this);
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	int channel, access;
	struct kvm_kpit_channel_state *s;
	u32 val = *(u32 *) data;
	if (!pit_in_range(addr))
		return -EOPNOTSUPP;

	val  &= 0xff;
	addr &= KVM_PIT_CHANNEL_MASK;

	mutex_lock(&pit_state->lock);

	if (val != 0)
		pr_debug("write addr is 0x%x, len is %d, val is 0x%x\n",
			 (unsigned int)addr, len, val);

	if (addr == 3) {
		channel = val >> 6;
		if (channel == 3) {
			/* Read-Back Command. */
			for (channel = 0; channel < 3; channel++) {
				s = &pit_state->channels[channel];
				if (val & (2 << channel)) {
					if (!(val & 0x20))
						pit_latch_count(pit, channel);
					if (!(val & 0x10))
						pit_latch_status(pit, channel);
				}
			}
		} else {
			/* Select Counter <channel>. */
			s = &pit_state->channels[channel];
			access = (val >> 4) & KVM_PIT_CHANNEL_MASK;
			if (access == 0) {
				pit_latch_count(pit, channel);
			} else {
				s->rw_mode = access;
				s->read_state = access;
				s->write_state = access;
				s->mode = (val >> 1) & 7;
				if (s->mode > 5)
					s->mode -= 4;
				s->bcd = val & 1;
			}
		}
	} else {
		/* Write Count. */
		s = &pit_state->channels[addr];
		switch (s->write_state) {
		default:
		case RW_STATE_LSB:
			pit_load_count(pit, addr, val);
			break;
		case RW_STATE_MSB:
			pit_load_count(pit, addr, val << 8);
			break;
		case RW_STATE_WORD0:
			s->write_latch = val;
			s->write_state = RW_STATE_WORD1;
			break;
		case RW_STATE_WORD1:
			pit_load_count(pit, addr, s->write_latch | (val << 8));
			s->write_state = RW_STATE_WORD0;
			break;
		}
	}

	mutex_unlock(&pit_state->lock);
	return 0;
}

static int pit_ioport_read(struct kvm_vcpu *vcpu,
			   struct kvm_io_device *this,
			   gpa_t addr, int len, void *data)
{
	struct kvm_pit *pit = dev_to_pit(this);
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	int ret, count;
	struct kvm_kpit_channel_state *s;
	if (!pit_in_range(addr))
		return -EOPNOTSUPP;

	addr &= KVM_PIT_CHANNEL_MASK;
	if (addr == 3)
		return 0;

	s = &pit_state->channels[addr];

	mutex_lock(&pit_state->lock);

	if (s->status_latched) {
		s->status_latched = 0;
		ret = s->status;
	} else if (s->count_latched) {
		switch (s->count_latched) {
		default:
		case RW_STATE_LSB:
			ret = s->latched_count & 0xff;
			s->count_latched = 0;
			break;
		case RW_STATE_MSB:
			ret = s->latched_count >> 8;
			s->count_latched = 0;
			break;
		case RW_STATE_WORD0:
			ret = s->latched_count & 0xff;
			s->count_latched = RW_STATE_MSB;
			break;
		}
	} else {
		switch (s->read_state) {
		default:
		case RW_STATE_LSB:
			count = pit_get_count(pit, addr);
			ret = count & 0xff;
			break;
		case RW_STATE_MSB:
			count = pit_get_count(pit, addr);
			ret = (count >> 8) & 0xff;
			break;
		case RW_STATE_WORD0:
			count = pit_get_count(pit, addr);
			ret = count & 0xff;
			s->read_state = RW_STATE_WORD1;
			break;
		case RW_STATE_WORD1:
			count = pit_get_count(pit, addr);
			ret = (count >> 8) & 0xff;
			s->read_state = RW_STATE_WORD0;
			break;
		}
	}

	if (len > sizeof(ret))
		len = sizeof(ret);
	memcpy(data, (char *)&ret, len);

	mutex_unlock(&pit_state->lock);
	return 0;
}

static int speaker_ioport_write(struct kvm_vcpu *vcpu,
				struct kvm_io_device *this,
				gpa_t addr, int len, const void *data)
{
	struct kvm_pit *pit = speaker_to_pit(this);
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	u32 val = *(u32 *) data;
	if (addr != KVM_SPEAKER_BASE_ADDRESS)
		return -EOPNOTSUPP;

	mutex_lock(&pit_state->lock);
	pit_state->speaker_data_on = (val >> 1) & 1;
	pit_set_gate(pit, 2, val & 1);
	mutex_unlock(&pit_state->lock);
	return 0;
}

static int speaker_ioport_read(struct kvm_vcpu *vcpu,
				   struct kvm_io_device *this,
				   gpa_t addr, int len, void *data)
{
	struct kvm_pit *pit = speaker_to_pit(this);
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	unsigned int refresh_clock;
	int ret;
	if (addr != KVM_SPEAKER_BASE_ADDRESS)
		return -EOPNOTSUPP;

	/* Refresh clock toggles at about 15us. We approximate as 2^14ns. */
	refresh_clock = ((unsigned int)ktime_to_ns(ktime_get()) >> 14) & 1;

	mutex_lock(&pit_state->lock);
	ret = ((pit_state->speaker_data_on << 1) | pit_get_gate(pit, 2) |
		(pit_get_out(pit, 2) << 5) | (refresh_clock << 4));
	if (len > sizeof(ret))
		len = sizeof(ret);
	memcpy(data, (char *)&ret, len);
	mutex_unlock(&pit_state->lock);
	return 0;
}

static void kvm_pit_reset(struct kvm_pit *pit)
{
	int i;
	struct kvm_kpit_channel_state *c;

	pit->pit_state.flags = 0;
	for (i = 0; i < 3; i++) {
		c = &pit->pit_state.channels[i];
		c->mode = 0xff;
		c->gate = (i != 2);
		pit_load_count(pit, i, 0);
	}

	kvm_pit_reset_reinject(pit);
}

static void pit_mask_notifer(struct kvm_irq_mask_notifier *kimn, bool mask)
{
	struct kvm_pit *pit = container_of(kimn, struct kvm_pit, mask_notifier);

	if (!mask)
		kvm_pit_reset_reinject(pit);
}

static const struct kvm_io_device_ops pit_dev_ops = {
	.read     = pit_ioport_read,
	.write    = pit_ioport_write,
};

static const struct kvm_io_device_ops speaker_dev_ops = {
	.read     = speaker_ioport_read,
	.write    = speaker_ioport_write,
};

/* Caller must hold slots_lock */
struct kvm_pit *kvm_create_pit(struct kvm *kvm, u32 flags)
{
	struct kvm_pit *pit;
	struct kvm_kpit_state *pit_state;
	struct pid *pid;
	pid_t pid_nr;
	int ret;

	pit = kzalloc(sizeof(struct kvm_pit), GFP_KERNEL);
	if (!pit)
		return NULL;

	pit->irq_source_id = kvm_request_irq_source_id(kvm);
	if (pit->irq_source_id < 0)
		goto fail_request;

	mutex_init(&pit->pit_state.lock);

	pid = get_pid(task_tgid(current));
	pid_nr = pid_vnr(pid);
	put_pid(pid);

	init_kthread_worker(&pit->worker);
	pit->worker_task = kthread_run(kthread_worker_fn, &pit->worker,
				       "kvm-pit/%d", pid_nr);
	if (IS_ERR(pit->worker_task))
		goto fail_kthread;

	init_kthread_work(&pit->expired, pit_do_work);

	pit->kvm = kvm;

	pit_state = &pit->pit_state;
	hrtimer_init(&pit_state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	pit_state->timer.function = pit_timer_fn;

	pit_state->irq_ack_notifier.gsi = 0;
	pit_state->irq_ack_notifier.irq_acked = kvm_pit_ack_irq;
	pit->mask_notifier.func = pit_mask_notifer;

	kvm_pit_reset(pit);

	kvm_pit_set_reinject(pit, true);

	kvm_iodevice_init(&pit->dev, &pit_dev_ops);
	ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, KVM_PIT_BASE_ADDRESS,
				      KVM_PIT_MEM_LENGTH, &pit->dev);
	if (ret < 0)
		goto fail_register_pit;

	if (flags & KVM_PIT_SPEAKER_DUMMY) {
		kvm_iodevice_init(&pit->speaker_dev, &speaker_dev_ops);
		ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS,
					      KVM_SPEAKER_BASE_ADDRESS, 4,
					      &pit->speaker_dev);
		if (ret < 0)
			goto fail_register_speaker;
	}

	return pit;

fail_register_speaker:
	kvm_io_bus_unregister_dev(kvm, KVM_PIO_BUS, &pit->dev);
fail_register_pit:
	kvm_pit_set_reinject(pit, false);
	kthread_stop(pit->worker_task);
fail_kthread:
	kvm_free_irq_source_id(kvm, pit->irq_source_id);
fail_request:
	kfree(pit);
	return NULL;
}

void kvm_free_pit(struct kvm *kvm)
{
	struct kvm_pit *pit = kvm->arch.vpit;

	if (pit) {
		kvm_io_bus_unregister_dev(kvm, KVM_PIO_BUS, &pit->dev);
		kvm_io_bus_unregister_dev(kvm, KVM_PIO_BUS, &pit->speaker_dev);
		kvm_pit_set_reinject(pit, false);
		hrtimer_cancel(&pit->pit_state.timer);
		flush_kthread_work(&pit->expired);
		kthread_stop(pit->worker_task);
		kvm_free_irq_source_id(kvm, pit->irq_source_id);
		kfree(pit);
	}
}
