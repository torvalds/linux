// SPDX-License-Identifier: GPL-2.0
/*
 * Xen event channels (2-level ABI)
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/sync_bitops.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/xen-ops.h>
#include <xen/events.h>
#include <xen/interface/xen.h>
#include <xen/interface/event_channel.h>

#include "events_internal.h"

/*
 * Note sizeof(xen_ulong_t) can be more than sizeof(unsigned long). Be
 * careful to only use bitops which allow for this (e.g
 * test_bit/find_first_bit and friends but not __ffs) and to pass
 * BITS_PER_EVTCHN_WORD as the bitmask length.
 */
#define BITS_PER_EVTCHN_WORD (sizeof(xen_ulong_t)*8)
/*
 * Make a bitmask (i.e. unsigned long *) of a xen_ulong_t
 * array. Primarily to avoid long lines (hence the terse name).
 */
#define BM(x) (unsigned long *)(x)
/* Find the first set bit in a evtchn mask */
#define EVTCHN_FIRST_BIT(w) find_first_bit(BM(&(w)), BITS_PER_EVTCHN_WORD)

#define EVTCHN_MASK_SIZE (EVTCHN_2L_NR_CHANNELS/BITS_PER_EVTCHN_WORD)

static DEFINE_PER_CPU(xen_ulong_t [EVTCHN_MASK_SIZE], cpu_evtchn_mask);

static unsigned evtchn_2l_max_channels(void)
{
	return EVTCHN_2L_NR_CHANNELS;
}

static void evtchn_2l_remove(evtchn_port_t evtchn, unsigned int cpu)
{
	clear_bit(evtchn, BM(per_cpu(cpu_evtchn_mask, cpu)));
}

static void evtchn_2l_bind_to_cpu(evtchn_port_t evtchn, unsigned int cpu,
				  unsigned int old_cpu)
{
	clear_bit(evtchn, BM(per_cpu(cpu_evtchn_mask, old_cpu)));
	set_bit(evtchn, BM(per_cpu(cpu_evtchn_mask, cpu)));
}

static void evtchn_2l_clear_pending(evtchn_port_t port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_clear_bit(port, BM(&s->evtchn_pending[0]));
}

static void evtchn_2l_set_pending(evtchn_port_t port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_set_bit(port, BM(&s->evtchn_pending[0]));
}

static bool evtchn_2l_is_pending(evtchn_port_t port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	return sync_test_bit(port, BM(&s->evtchn_pending[0]));
}

static void evtchn_2l_mask(evtchn_port_t port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_set_bit(port, BM(&s->evtchn_mask[0]));
}

static void evtchn_2l_unmask(evtchn_port_t port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	unsigned int cpu = get_cpu();
	int do_hypercall = 0, evtchn_pending = 0;

	BUG_ON(!irqs_disabled());

	smp_wmb();	/* All writes before unmask must be visible. */

	if (unlikely((cpu != cpu_from_evtchn(port))))
		do_hypercall = 1;
	else {
		/*
		 * Need to clear the mask before checking pending to
		 * avoid a race with an event becoming pending.
		 *
		 * EVTCHNOP_unmask will only trigger an upcall if the
		 * mask bit was set, so if a hypercall is needed
		 * remask the event.
		 */
		sync_clear_bit(port, BM(&s->evtchn_mask[0]));
		evtchn_pending = sync_test_bit(port, BM(&s->evtchn_pending[0]));

		if (unlikely(evtchn_pending && xen_hvm_domain())) {
			sync_set_bit(port, BM(&s->evtchn_mask[0]));
			do_hypercall = 1;
		}
	}

	/* Slow path (hypercall) if this is a non-local port or if this is
	 * an hvm domain and an event is pending (hvm domains don't have
	 * their own implementation of irq_enable). */
	if (do_hypercall) {
		struct evtchn_unmask unmask = { .port = port };
		(void)HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask);
	} else {
		struct vcpu_info *vcpu_info = __this_cpu_read(xen_vcpu);

		/*
		 * The following is basically the equivalent of
		 * 'hw_resend_irq'. Just like a real IO-APIC we 'lose
		 * the interrupt edge' if the channel is masked.
		 */
		if (evtchn_pending &&
		    !sync_test_and_set_bit(port / BITS_PER_EVTCHN_WORD,
					   BM(&vcpu_info->evtchn_pending_sel)))
			vcpu_info->evtchn_upcall_pending = 1;
	}

	put_cpu();
}

static DEFINE_PER_CPU(unsigned int, current_word_idx);
static DEFINE_PER_CPU(unsigned int, current_bit_idx);

/*
 * Mask out the i least significant bits of w
 */
#define MASK_LSBS(w, i) (w & ((~((xen_ulong_t)0UL)) << i))

static inline xen_ulong_t active_evtchns(unsigned int cpu,
					 struct shared_info *sh,
					 unsigned int idx)
{
	return sh->evtchn_pending[idx] &
		per_cpu(cpu_evtchn_mask, cpu)[idx] &
		~sh->evtchn_mask[idx];
}

/*
 * Search the CPU's pending events bitmasks.  For each one found, map
 * the event number to an irq, and feed it into do_IRQ() for handling.
 *
 * Xen uses a two-level bitmap to speed searching.  The first level is
 * a bitset of words which contain pending event bits.  The second
 * level is a bitset of pending events themselves.
 */
static void evtchn_2l_handle_events(unsigned cpu, struct evtchn_loop_ctrl *ctrl)
{
	int irq;
	xen_ulong_t pending_words;
	xen_ulong_t pending_bits;
	int start_word_idx, start_bit_idx;
	int word_idx, bit_idx;
	int i;
	struct shared_info *s = HYPERVISOR_shared_info;
	struct vcpu_info *vcpu_info = __this_cpu_read(xen_vcpu);
	evtchn_port_t evtchn;

	/* Timer interrupt has highest priority. */
	irq = irq_evtchn_from_virq(cpu, VIRQ_TIMER, &evtchn);
	if (irq != -1) {
		word_idx = evtchn / BITS_PER_LONG;
		bit_idx = evtchn % BITS_PER_LONG;
		if (active_evtchns(cpu, s, word_idx) & (1ULL << bit_idx))
			generic_handle_irq(irq);
	}

	/*
	 * Master flag must be cleared /before/ clearing
	 * selector flag. xchg_xen_ulong must contain an
	 * appropriate barrier.
	 */
	pending_words = xchg_xen_ulong(&vcpu_info->evtchn_pending_sel, 0);

	start_word_idx = __this_cpu_read(current_word_idx);
	start_bit_idx = __this_cpu_read(current_bit_idx);

	word_idx = start_word_idx;

	for (i = 0; pending_words != 0; i++) {
		xen_ulong_t words;

		words = MASK_LSBS(pending_words, word_idx);

		/*
		 * If we masked out all events, wrap to beginning.
		 */
		if (words == 0) {
			word_idx = 0;
			bit_idx = 0;
			continue;
		}
		word_idx = EVTCHN_FIRST_BIT(words);

		pending_bits = active_evtchns(cpu, s, word_idx);
		bit_idx = 0; /* usually scan entire word from start */
		/*
		 * We scan the starting word in two parts.
		 *
		 * 1st time: start in the middle, scanning the
		 * upper bits.
		 *
		 * 2nd time: scan the whole word (not just the
		 * parts skipped in the first pass) -- if an
		 * event in the previously scanned bits is
		 * pending again it would just be scanned on
		 * the next loop anyway.
		 */
		if (word_idx == start_word_idx) {
			if (i == 0)
				bit_idx = start_bit_idx;
		}

		do {
			xen_ulong_t bits;
			evtchn_port_t port;

			bits = MASK_LSBS(pending_bits, bit_idx);

			/* If we masked out all events, move on. */
			if (bits == 0)
				break;

			bit_idx = EVTCHN_FIRST_BIT(bits);

			/* Process port. */
			port = (word_idx * BITS_PER_EVTCHN_WORD) + bit_idx;
			handle_irq_for_port(port, ctrl);

			bit_idx = (bit_idx + 1) % BITS_PER_EVTCHN_WORD;

			/* Next caller starts at last processed + 1 */
			__this_cpu_write(current_word_idx,
					 bit_idx ? word_idx :
					 (word_idx+1) % BITS_PER_EVTCHN_WORD);
			__this_cpu_write(current_bit_idx, bit_idx);
		} while (bit_idx != 0);

		/* Scan start_l1i twice; all others once. */
		if ((word_idx != start_word_idx) || (i != 0))
			pending_words &= ~(1UL << word_idx);

		word_idx = (word_idx + 1) % BITS_PER_EVTCHN_WORD;
	}
}

irqreturn_t xen_debug_interrupt(int irq, void *dev_id)
{
	struct shared_info *sh = HYPERVISOR_shared_info;
	int cpu = smp_processor_id();
	xen_ulong_t *cpu_evtchn = per_cpu(cpu_evtchn_mask, cpu);
	int i;
	unsigned long flags;
	static DEFINE_SPINLOCK(debug_lock);
	struct vcpu_info *v;

	spin_lock_irqsave(&debug_lock, flags);

	printk("\nvcpu %d\n  ", cpu);

	for_each_online_cpu(i) {
		int pending;
		v = per_cpu(xen_vcpu, i);
		pending = (get_irq_regs() && i == cpu)
			? xen_irqs_disabled(get_irq_regs())
			: v->evtchn_upcall_mask;
		printk("%d: masked=%d pending=%d event_sel %0*"PRI_xen_ulong"\n  ", i,
		       pending, v->evtchn_upcall_pending,
		       (int)(sizeof(v->evtchn_pending_sel)*2),
		       v->evtchn_pending_sel);
	}
	v = per_cpu(xen_vcpu, cpu);

	printk("\npending:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_pending)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s",
		       (int)sizeof(sh->evtchn_pending[0])*2,
		       sh->evtchn_pending[i],
		       i % 8 == 0 ? "\n   " : " ");
	printk("\nglobal mask:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       sh->evtchn_mask[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nglobally unmasked:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       sh->evtchn_pending[i] & ~sh->evtchn_mask[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nlocal cpu%d mask:\n   ", cpu);
	for (i = (EVTCHN_2L_NR_CHANNELS/BITS_PER_EVTCHN_WORD)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s", (int)(sizeof(cpu_evtchn[0])*2),
		       cpu_evtchn[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nlocally unmasked:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--) {
		xen_ulong_t pending = sh->evtchn_pending[i]
			& ~sh->evtchn_mask[i]
			& cpu_evtchn[i];
		printk("%0*"PRI_xen_ulong"%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       pending, i % 8 == 0 ? "\n   " : " ");
	}

	printk("\npending list:\n");
	for (i = 0; i < EVTCHN_2L_NR_CHANNELS; i++) {
		if (sync_test_bit(i, BM(sh->evtchn_pending))) {
			int word_idx = i / BITS_PER_EVTCHN_WORD;
			printk("  %d: event %d -> irq %u%s%s%s\n",
			       cpu_from_evtchn(i), i,
			       irq_from_evtchn(i),
			       sync_test_bit(word_idx, BM(&v->evtchn_pending_sel))
			       ? "" : " l2-clear",
			       !sync_test_bit(i, BM(sh->evtchn_mask))
			       ? "" : " globally-masked",
			       sync_test_bit(i, BM(cpu_evtchn))
			       ? "" : " locally-masked");
		}
	}

	spin_unlock_irqrestore(&debug_lock, flags);

	return IRQ_HANDLED;
}

static void evtchn_2l_resume(void)
{
	int i;

	for_each_online_cpu(i)
		memset(per_cpu(cpu_evtchn_mask, i), 0, sizeof(xen_ulong_t) *
				EVTCHN_2L_NR_CHANNELS/BITS_PER_EVTCHN_WORD);
}

static int evtchn_2l_percpu_deinit(unsigned int cpu)
{
	memset(per_cpu(cpu_evtchn_mask, cpu), 0, sizeof(xen_ulong_t) *
			EVTCHN_2L_NR_CHANNELS/BITS_PER_EVTCHN_WORD);

	return 0;
}

static const struct evtchn_ops evtchn_ops_2l = {
	.max_channels      = evtchn_2l_max_channels,
	.nr_channels       = evtchn_2l_max_channels,
	.remove            = evtchn_2l_remove,
	.bind_to_cpu       = evtchn_2l_bind_to_cpu,
	.clear_pending     = evtchn_2l_clear_pending,
	.set_pending       = evtchn_2l_set_pending,
	.is_pending        = evtchn_2l_is_pending,
	.mask              = evtchn_2l_mask,
	.unmask            = evtchn_2l_unmask,
	.handle_events     = evtchn_2l_handle_events,
	.resume	           = evtchn_2l_resume,
	.percpu_deinit     = evtchn_2l_percpu_deinit,
};

void __init xen_evtchn_2l_init(void)
{
	pr_info("Using 2-level ABI\n");
	evtchn_ops = &evtchn_ops_2l;
}
