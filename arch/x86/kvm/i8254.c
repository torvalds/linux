/*
 * 8253/8254 interval timer emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2006 Intel Corporation
 * Copyright (c) 2007 Keir Fraser, XenSource Inc
 * Copyright (c) 2008 Intel Corporation
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

#include <linux/kvm_host.h>

#include "irq.h"
#include "i8254.h"

#ifndef CONFIG_X86_64
#define mod_64(x, y) ((x) - (y) * div64_u64(x, y))
#else
#define mod_64(x, y) ((x) % (y))
#endif

#define RW_STATE_LSB 1
#define RW_STATE_MSB 2
#define RW_STATE_WORD0 3
#define RW_STATE_WORD1 4

/* Compute with 96 bit intermediate result: (a*b)/c */
static u64 muldiv64(u64 a, u32 b, u32 c)
{
	union {
		u64 ll;
		struct {
			u32 low, high;
		} l;
	} u, res;
	u64 rl, rh;

	u.ll = a;
	rl = (u64)u.l.low * (u64)b;
	rh = (u64)u.l.high * (u64)b;
	rh += (rl >> 32);
	res.l.high = div64_u64(rh, c);
	res.l.low = div64_u64(((mod_64(rh, c) << 32) + (rl & 0xffffffff)), c);
	return res.ll;
}

static void pit_set_gate(struct kvm *kvm, int channel, u32 val)
{
	struct kvm_kpit_channel_state *c =
		&kvm->arch.vpit->pit_state.channels[channel];

	WARN_ON(!mutex_is_locked(&kvm->arch.vpit->pit_state.lock));

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

int pit_get_gate(struct kvm *kvm, int channel)
{
	WARN_ON(!mutex_is_locked(&kvm->arch.vpit->pit_state.lock));

	return kvm->arch.vpit->pit_state.channels[channel].gate;
}

static int pit_get_count(struct kvm *kvm, int channel)
{
	struct kvm_kpit_channel_state *c =
		&kvm->arch.vpit->pit_state.channels[channel];
	s64 d, t;
	int counter;

	WARN_ON(!mutex_is_locked(&kvm->arch.vpit->pit_state.lock));

	t = ktime_to_ns(ktime_sub(ktime_get(), c->count_load_time));
	d = muldiv64(t, KVM_PIT_FREQ, NSEC_PER_SEC);

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

static int pit_get_out(struct kvm *kvm, int channel)
{
	struct kvm_kpit_channel_state *c =
		&kvm->arch.vpit->pit_state.channels[channel];
	s64 d, t;
	int out;

	WARN_ON(!mutex_is_locked(&kvm->arch.vpit->pit_state.lock));

	t = ktime_to_ns(ktime_sub(ktime_get(), c->count_load_time));
	d = muldiv64(t, KVM_PIT_FREQ, NSEC_PER_SEC);

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

static void pit_latch_count(struct kvm *kvm, int channel)
{
	struct kvm_kpit_channel_state *c =
		&kvm->arch.vpit->pit_state.channels[channel];

	WARN_ON(!mutex_is_locked(&kvm->arch.vpit->pit_state.lock));

	if (!c->count_latched) {
		c->latched_count = pit_get_count(kvm, channel);
		c->count_latched = c->rw_mode;
	}
}

static void pit_latch_status(struct kvm *kvm, int channel)
{
	struct kvm_kpit_channel_state *c =
		&kvm->arch.vpit->pit_state.channels[channel];

	WARN_ON(!mutex_is_locked(&kvm->arch.vpit->pit_state.lock));

	if (!c->status_latched) {
		/* TODO: Return NULL COUNT (bit 6). */
		c->status = ((pit_get_out(kvm, channel) << 7) |
				(c->rw_mode << 4) |
				(c->mode << 1) |
				c->bcd);
		c->status_latched = 1;
	}
}

int __pit_timer_fn(struct kvm_kpit_state *ps)
{
	struct kvm_vcpu *vcpu0 = ps->pit->kvm->vcpus[0];
	struct kvm_kpit_timer *pt = &ps->pit_timer;

	atomic_inc(&pt->pending);
	smp_mb__after_atomic_inc();
	/* FIXME: handle case where the guest is in guest mode */
	if (vcpu0 && waitqueue_active(&vcpu0->wq)) {
		vcpu0->arch.mp_state = KVM_MP_STATE_RUNNABLE;
		wake_up_interruptible(&vcpu0->wq);
	}

	pt->timer.expires = ktime_add_ns(pt->timer.expires, pt->period);
	pt->scheduled = ktime_to_ns(pt->timer.expires);

	return (pt->period == 0 ? 0 : 1);
}

int pit_has_pending_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_pit *pit = vcpu->kvm->arch.vpit;

	if (pit && vcpu->vcpu_id == 0)
		return atomic_read(&pit->pit_state.pit_timer.pending);

	return 0;
}

static enum hrtimer_restart pit_timer_fn(struct hrtimer *data)
{
	struct kvm_kpit_state *ps;
	int restart_timer = 0;

	ps = container_of(data, struct kvm_kpit_state, pit_timer.timer);

	restart_timer = __pit_timer_fn(ps);

	if (restart_timer)
		return HRTIMER_RESTART;
	else
		return HRTIMER_NORESTART;
}

static void destroy_pit_timer(struct kvm_kpit_timer *pt)
{
	pr_debug("pit: execute del timer!\n");
	hrtimer_cancel(&pt->timer);
}

static void create_pit_timer(struct kvm_kpit_timer *pt, u32 val, int is_period)
{
	s64 interval;

	interval = muldiv64(val, NSEC_PER_SEC, KVM_PIT_FREQ);

	pr_debug("pit: create pit timer, interval is %llu nsec\n", interval);

	/* TODO The new value only affected after the retriggered */
	hrtimer_cancel(&pt->timer);
	pt->period = (is_period == 0) ? 0 : interval;
	pt->timer.function = pit_timer_fn;
	atomic_set(&pt->pending, 0);

	hrtimer_start(&pt->timer, ktime_add_ns(ktime_get(), interval),
		      HRTIMER_MODE_ABS);
}

static void pit_load_count(struct kvm *kvm, int channel, u32 val)
{
	struct kvm_kpit_state *ps = &kvm->arch.vpit->pit_state;

	WARN_ON(!mutex_is_locked(&ps->lock));

	pr_debug("pit: load_count val is %d, channel is %d\n", val, channel);

	/*
	 * Though spec said the state of 8254 is undefined after power-up,
	 * seems some tricky OS like Windows XP depends on IRQ0 interrupt
	 * when booting up.
	 * So here setting initialize rate for it, and not a specific number
	 */
	if (val == 0)
		val = 0x10000;

	ps->channels[channel].count_load_time = ktime_get();
	ps->channels[channel].count = val;

	if (channel != 0)
		return;

	/* Two types of timer
	 * mode 1 is one shot, mode 2 is period, otherwise del timer */
	switch (ps->channels[0].mode) {
	case 1:
        /* FIXME: enhance mode 4 precision */
	case 4:
		create_pit_timer(&ps->pit_timer, val, 0);
		break;
	case 2:
		create_pit_timer(&ps->pit_timer, val, 1);
		break;
	default:
		destroy_pit_timer(&ps->pit_timer);
	}
}

void kvm_pit_load_count(struct kvm *kvm, int channel, u32 val)
{
	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	pit_load_count(kvm, channel, val);
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
}

static void pit_ioport_write(struct kvm_io_device *this,
			     gpa_t addr, int len, const void *data)
{
	struct kvm_pit *pit = (struct kvm_pit *)this->private;
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	struct kvm *kvm = pit->kvm;
	int channel, access;
	struct kvm_kpit_channel_state *s;
	u32 val = *(u32 *) data;

	val  &= 0xff;
	addr &= KVM_PIT_CHANNEL_MASK;

	mutex_lock(&pit_state->lock);

	if (val != 0)
		pr_debug("pit: write addr is 0x%x, len is %d, val is 0x%x\n",
			  (unsigned int)addr, len, val);

	if (addr == 3) {
		channel = val >> 6;
		if (channel == 3) {
			/* Read-Back Command. */
			for (channel = 0; channel < 3; channel++) {
				s = &pit_state->channels[channel];
				if (val & (2 << channel)) {
					if (!(val & 0x20))
						pit_latch_count(kvm, channel);
					if (!(val & 0x10))
						pit_latch_status(kvm, channel);
				}
			}
		} else {
			/* Select Counter <channel>. */
			s = &pit_state->channels[channel];
			access = (val >> 4) & KVM_PIT_CHANNEL_MASK;
			if (access == 0) {
				pit_latch_count(kvm, channel);
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
			pit_load_count(kvm, addr, val);
			break;
		case RW_STATE_MSB:
			pit_load_count(kvm, addr, val << 8);
			break;
		case RW_STATE_WORD0:
			s->write_latch = val;
			s->write_state = RW_STATE_WORD1;
			break;
		case RW_STATE_WORD1:
			pit_load_count(kvm, addr, s->write_latch | (val << 8));
			s->write_state = RW_STATE_WORD0;
			break;
		}
	}

	mutex_unlock(&pit_state->lock);
}

static void pit_ioport_read(struct kvm_io_device *this,
			    gpa_t addr, int len, void *data)
{
	struct kvm_pit *pit = (struct kvm_pit *)this->private;
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	struct kvm *kvm = pit->kvm;
	int ret, count;
	struct kvm_kpit_channel_state *s;

	addr &= KVM_PIT_CHANNEL_MASK;
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
			count = pit_get_count(kvm, addr);
			ret = count & 0xff;
			break;
		case RW_STATE_MSB:
			count = pit_get_count(kvm, addr);
			ret = (count >> 8) & 0xff;
			break;
		case RW_STATE_WORD0:
			count = pit_get_count(kvm, addr);
			ret = count & 0xff;
			s->read_state = RW_STATE_WORD1;
			break;
		case RW_STATE_WORD1:
			count = pit_get_count(kvm, addr);
			ret = (count >> 8) & 0xff;
			s->read_state = RW_STATE_WORD0;
			break;
		}
	}

	if (len > sizeof(ret))
		len = sizeof(ret);
	memcpy(data, (char *)&ret, len);

	mutex_unlock(&pit_state->lock);
}

static int pit_in_range(struct kvm_io_device *this, gpa_t addr)
{
	return ((addr >= KVM_PIT_BASE_ADDRESS) &&
		(addr < KVM_PIT_BASE_ADDRESS + KVM_PIT_MEM_LENGTH));
}

static void speaker_ioport_write(struct kvm_io_device *this,
				 gpa_t addr, int len, const void *data)
{
	struct kvm_pit *pit = (struct kvm_pit *)this->private;
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	struct kvm *kvm = pit->kvm;
	u32 val = *(u32 *) data;

	mutex_lock(&pit_state->lock);
	pit_state->speaker_data_on = (val >> 1) & 1;
	pit_set_gate(kvm, 2, val & 1);
	mutex_unlock(&pit_state->lock);
}

static void speaker_ioport_read(struct kvm_io_device *this,
				gpa_t addr, int len, void *data)
{
	struct kvm_pit *pit = (struct kvm_pit *)this->private;
	struct kvm_kpit_state *pit_state = &pit->pit_state;
	struct kvm *kvm = pit->kvm;
	unsigned int refresh_clock;
	int ret;

	/* Refresh clock toggles at about 15us. We approximate as 2^14ns. */
	refresh_clock = ((unsigned int)ktime_to_ns(ktime_get()) >> 14) & 1;

	mutex_lock(&pit_state->lock);
	ret = ((pit_state->speaker_data_on << 1) | pit_get_gate(kvm, 2) |
		(pit_get_out(kvm, 2) << 5) | (refresh_clock << 4));
	if (len > sizeof(ret))
		len = sizeof(ret);
	memcpy(data, (char *)&ret, len);
	mutex_unlock(&pit_state->lock);
}

static int speaker_in_range(struct kvm_io_device *this, gpa_t addr)
{
	return (addr == KVM_SPEAKER_BASE_ADDRESS);
}

void kvm_pit_reset(struct kvm_pit *pit)
{
	int i;
	struct kvm_kpit_channel_state *c;

	mutex_lock(&pit->pit_state.lock);
	for (i = 0; i < 3; i++) {
		c = &pit->pit_state.channels[i];
		c->mode = 0xff;
		c->gate = (i != 2);
		pit_load_count(pit->kvm, i, 0);
	}
	mutex_unlock(&pit->pit_state.lock);

	atomic_set(&pit->pit_state.pit_timer.pending, 0);
	pit->pit_state.inject_pending = 1;
}

struct kvm_pit *kvm_create_pit(struct kvm *kvm)
{
	struct kvm_pit *pit;
	struct kvm_kpit_state *pit_state;

	pit = kzalloc(sizeof(struct kvm_pit), GFP_KERNEL);
	if (!pit)
		return NULL;

	mutex_init(&pit->pit_state.lock);
	mutex_lock(&pit->pit_state.lock);

	/* Initialize PIO device */
	pit->dev.read = pit_ioport_read;
	pit->dev.write = pit_ioport_write;
	pit->dev.in_range = pit_in_range;
	pit->dev.private = pit;
	kvm_io_bus_register_dev(&kvm->pio_bus, &pit->dev);

	pit->speaker_dev.read = speaker_ioport_read;
	pit->speaker_dev.write = speaker_ioport_write;
	pit->speaker_dev.in_range = speaker_in_range;
	pit->speaker_dev.private = pit;
	kvm_io_bus_register_dev(&kvm->pio_bus, &pit->speaker_dev);

	kvm->arch.vpit = pit;
	pit->kvm = kvm;

	pit_state = &pit->pit_state;
	pit_state->pit = pit;
	hrtimer_init(&pit_state->pit_timer.timer,
		     CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	mutex_unlock(&pit->pit_state.lock);

	kvm_pit_reset(pit);

	return pit;
}

void kvm_free_pit(struct kvm *kvm)
{
	struct hrtimer *timer;

	if (kvm->arch.vpit) {
		mutex_lock(&kvm->arch.vpit->pit_state.lock);
		timer = &kvm->arch.vpit->pit_state.pit_timer.timer;
		hrtimer_cancel(timer);
		mutex_unlock(&kvm->arch.vpit->pit_state.lock);
		kfree(kvm->arch.vpit);
	}
}

void __inject_pit_timer_intr(struct kvm *kvm)
{
	mutex_lock(&kvm->lock);
	kvm_ioapic_set_irq(kvm->arch.vioapic, 0, 1);
	kvm_ioapic_set_irq(kvm->arch.vioapic, 0, 0);
	kvm_pic_set_irq(pic_irqchip(kvm), 0, 1);
	kvm_pic_set_irq(pic_irqchip(kvm), 0, 0);
	mutex_unlock(&kvm->lock);
}

void kvm_inject_pit_timer_irqs(struct kvm_vcpu *vcpu)
{
	struct kvm_pit *pit = vcpu->kvm->arch.vpit;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_kpit_state *ps;

	if (vcpu && pit) {
		ps = &pit->pit_state;

		/* Try to inject pending interrupts when:
		 * 1. Pending exists
		 * 2. Last interrupt was accepted or waited for too long time*/
		if (atomic_read(&ps->pit_timer.pending) &&
		    (ps->inject_pending ||
		    (jiffies - ps->last_injected_time
				>= KVM_MAX_PIT_INTR_INTERVAL))) {
			ps->inject_pending = 0;
			__inject_pit_timer_intr(kvm);
			ps->last_injected_time = jiffies;
		}
	}
}

void kvm_pit_timer_intr_post(struct kvm_vcpu *vcpu, int vec)
{
	struct kvm_arch *arch = &vcpu->kvm->arch;
	struct kvm_kpit_state *ps;

	if (vcpu && arch->vpit) {
		ps = &arch->vpit->pit_state;
		if (atomic_read(&ps->pit_timer.pending) &&
		(((arch->vpic->pics[0].imr & 1) == 0 &&
		  arch->vpic->pics[0].irq_base == vec) ||
		  (arch->vioapic->redirtbl[0].fields.vector == vec &&
		  arch->vioapic->redirtbl[0].fields.mask != 1))) {
			ps->inject_pending = 1;
			atomic_dec(&ps->pit_timer.pending);
			ps->channels[0].count_load_time = ktime_get();
		}
	}
}
