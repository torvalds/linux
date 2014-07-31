/*
 * Xen event channels (FIFO-based ABI)
 *
 * Copyright (C) 2013 Citrix Systems R&D ltd.
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Or, when distributed separately from the Linux kernel or
 * incorporated into other software packages, subject to the following
 * license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/cpu.h>

#include <asm/sync_bitops.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/page.h>

#include <xen/xen.h>
#include <xen/xen-ops.h>
#include <xen/events.h>
#include <xen/interface/xen.h>
#include <xen/interface/event_channel.h>

#include "events_internal.h"

#define EVENT_WORDS_PER_PAGE (PAGE_SIZE / sizeof(event_word_t))
#define MAX_EVENT_ARRAY_PAGES (EVTCHN_FIFO_NR_CHANNELS / EVENT_WORDS_PER_PAGE)

struct evtchn_fifo_queue {
	uint32_t head[EVTCHN_FIFO_MAX_QUEUES];
};

static DEFINE_PER_CPU(struct evtchn_fifo_control_block *, cpu_control_block);
static DEFINE_PER_CPU(struct evtchn_fifo_queue, cpu_queue);
static event_word_t *event_array[MAX_EVENT_ARRAY_PAGES] __read_mostly;
static unsigned event_array_pages __read_mostly;

/*
 * sync_set_bit() and friends must be unsigned long aligned.
 */
#if BITS_PER_LONG > 32

#define BM(w) (unsigned long *)((unsigned long)w & ~0x7UL)
#define EVTCHN_FIFO_BIT(b, w) \
    (((unsigned long)w & 0x4UL) ? (EVTCHN_FIFO_ ##b + 32) : EVTCHN_FIFO_ ##b)

#else

#define BM(w) ((unsigned long *)(w))
#define EVTCHN_FIFO_BIT(b, w) EVTCHN_FIFO_ ##b

#endif

static inline event_word_t *event_word_from_port(unsigned port)
{
	unsigned i = port / EVENT_WORDS_PER_PAGE;

	return event_array[i] + port % EVENT_WORDS_PER_PAGE;
}

static unsigned evtchn_fifo_max_channels(void)
{
	return EVTCHN_FIFO_NR_CHANNELS;
}

static unsigned evtchn_fifo_nr_channels(void)
{
	return event_array_pages * EVENT_WORDS_PER_PAGE;
}

static int init_control_block(int cpu,
                              struct evtchn_fifo_control_block *control_block)
{
	struct evtchn_fifo_queue *q = &per_cpu(cpu_queue, cpu);
	struct evtchn_init_control init_control;
	unsigned int i;

	/* Reset the control block and the local HEADs. */
	clear_page(control_block);
	for (i = 0; i < EVTCHN_FIFO_MAX_QUEUES; i++)
		q->head[i] = 0;

	init_control.control_gfn = virt_to_mfn(control_block);
	init_control.offset      = 0;
	init_control.vcpu        = cpu;

	return HYPERVISOR_event_channel_op(EVTCHNOP_init_control, &init_control);
}

static void free_unused_array_pages(void)
{
	unsigned i;

	for (i = event_array_pages; i < MAX_EVENT_ARRAY_PAGES; i++) {
		if (!event_array[i])
			break;
		free_page((unsigned long)event_array[i]);
		event_array[i] = NULL;
	}
}

static void init_array_page(event_word_t *array_page)
{
	unsigned i;

	for (i = 0; i < EVENT_WORDS_PER_PAGE; i++)
		array_page[i] = 1 << EVTCHN_FIFO_MASKED;
}

static int evtchn_fifo_setup(struct irq_info *info)
{
	unsigned port = info->evtchn;
	unsigned new_array_pages;
	int ret;

	new_array_pages = port / EVENT_WORDS_PER_PAGE + 1;

	if (new_array_pages > MAX_EVENT_ARRAY_PAGES)
		return -EINVAL;

	while (event_array_pages < new_array_pages) {
		void *array_page;
		struct evtchn_expand_array expand_array;

		/* Might already have a page if we've resumed. */
		array_page = event_array[event_array_pages];
		if (!array_page) {
			array_page = (void *)__get_free_page(GFP_KERNEL);
			if (array_page == NULL) {
				ret = -ENOMEM;
				goto error;
			}
			event_array[event_array_pages] = array_page;
		}

		/* Mask all events in this page before adding it. */
		init_array_page(array_page);

		expand_array.array_gfn = virt_to_mfn(array_page);

		ret = HYPERVISOR_event_channel_op(EVTCHNOP_expand_array, &expand_array);
		if (ret < 0)
			goto error;

		event_array_pages++;
	}
	return 0;

  error:
	if (event_array_pages == 0)
		panic("xen: unable to expand event array with initial page (%d)\n", ret);
	else
		pr_err("unable to expand event array (%d)\n", ret);
	free_unused_array_pages();
	return ret;
}

static void evtchn_fifo_bind_to_cpu(struct irq_info *info, unsigned cpu)
{
	/* no-op */
}

static void evtchn_fifo_clear_pending(unsigned port)
{
	event_word_t *word = event_word_from_port(port);
	sync_clear_bit(EVTCHN_FIFO_BIT(PENDING, word), BM(word));
}

static void evtchn_fifo_set_pending(unsigned port)
{
	event_word_t *word = event_word_from_port(port);
	sync_set_bit(EVTCHN_FIFO_BIT(PENDING, word), BM(word));
}

static bool evtchn_fifo_is_pending(unsigned port)
{
	event_word_t *word = event_word_from_port(port);
	return sync_test_bit(EVTCHN_FIFO_BIT(PENDING, word), BM(word));
}

static bool evtchn_fifo_test_and_set_mask(unsigned port)
{
	event_word_t *word = event_word_from_port(port);
	return sync_test_and_set_bit(EVTCHN_FIFO_BIT(MASKED, word), BM(word));
}

static void evtchn_fifo_mask(unsigned port)
{
	event_word_t *word = event_word_from_port(port);
	sync_set_bit(EVTCHN_FIFO_BIT(MASKED, word), BM(word));
}

static bool evtchn_fifo_is_masked(unsigned port)
{
	event_word_t *word = event_word_from_port(port);
	return sync_test_bit(EVTCHN_FIFO_BIT(MASKED, word), BM(word));
}
/*
 * Clear MASKED, spinning if BUSY is set.
 */
static void clear_masked(volatile event_word_t *word)
{
	event_word_t new, old, w;

	w = *word;

	do {
		old = w & ~(1 << EVTCHN_FIFO_BUSY);
		new = old & ~(1 << EVTCHN_FIFO_MASKED);
		w = sync_cmpxchg(word, old, new);
	} while (w != old);
}

static void evtchn_fifo_unmask(unsigned port)
{
	event_word_t *word = event_word_from_port(port);

	BUG_ON(!irqs_disabled());

	clear_masked(word);
	if (evtchn_fifo_is_pending(port)) {
		struct evtchn_unmask unmask = { .port = port };
		(void)HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask);
	}
}

static uint32_t clear_linked(volatile event_word_t *word)
{
	event_word_t new, old, w;

	w = *word;

	do {
		old = w;
		new = (w & ~((1 << EVTCHN_FIFO_LINKED)
			     | EVTCHN_FIFO_LINK_MASK));
	} while ((w = sync_cmpxchg(word, old, new)) != old);

	return w & EVTCHN_FIFO_LINK_MASK;
}

static void handle_irq_for_port(unsigned port)
{
	int irq;

	irq = get_evtchn_to_irq(port);
	if (irq != -1)
		generic_handle_irq(irq);
}

static void consume_one_event(unsigned cpu,
			      struct evtchn_fifo_control_block *control_block,
			      unsigned priority, unsigned long *ready)
{
	struct evtchn_fifo_queue *q = &per_cpu(cpu_queue, cpu);
	uint32_t head;
	unsigned port;
	event_word_t *word;

	head = q->head[priority];

	/*
	 * Reached the tail last time?  Read the new HEAD from the
	 * control block.
	 */
	if (head == 0) {
		rmb(); /* Ensure word is up-to-date before reading head. */
		head = control_block->head[priority];
	}

	port = head;
	word = event_word_from_port(port);
	head = clear_linked(word);

	/*
	 * If the link is non-zero, there are more events in the
	 * queue, otherwise the queue is empty.
	 *
	 * If the queue is empty, clear this priority from our local
	 * copy of the ready word.
	 */
	if (head == 0)
		clear_bit(priority, ready);

	if (evtchn_fifo_is_pending(port) && !evtchn_fifo_is_masked(port))
		handle_irq_for_port(port);

	q->head[priority] = head;
}

static void evtchn_fifo_handle_events(unsigned cpu)
{
	struct evtchn_fifo_control_block *control_block;
	unsigned long ready;
	unsigned q;

	control_block = per_cpu(cpu_control_block, cpu);

	ready = xchg(&control_block->ready, 0);

	while (ready) {
		q = find_first_bit(BM(&ready), EVTCHN_FIFO_MAX_QUEUES);
		consume_one_event(cpu, control_block, q, &ready);
		ready |= xchg(&control_block->ready, 0);
	}
}

static void evtchn_fifo_resume(void)
{
	unsigned cpu;

	for_each_possible_cpu(cpu) {
		void *control_block = per_cpu(cpu_control_block, cpu);
		int ret;

		if (!control_block)
			continue;

		/*
		 * If this CPU is offline, take the opportunity to
		 * free the control block while it is not being
		 * used.
		 */
		if (!cpu_online(cpu)) {
			free_page((unsigned long)control_block);
			per_cpu(cpu_control_block, cpu) = NULL;
			continue;
		}

		ret = init_control_block(cpu, control_block);
		if (ret < 0)
			BUG();
	}

	/*
	 * The event array starts out as empty again and is extended
	 * as normal when events are bound.  The existing pages will
	 * be reused.
	 */
	event_array_pages = 0;
}

static const struct evtchn_ops evtchn_ops_fifo = {
	.max_channels      = evtchn_fifo_max_channels,
	.nr_channels       = evtchn_fifo_nr_channels,
	.setup             = evtchn_fifo_setup,
	.bind_to_cpu       = evtchn_fifo_bind_to_cpu,
	.clear_pending     = evtchn_fifo_clear_pending,
	.set_pending       = evtchn_fifo_set_pending,
	.is_pending        = evtchn_fifo_is_pending,
	.test_and_set_mask = evtchn_fifo_test_and_set_mask,
	.mask              = evtchn_fifo_mask,
	.unmask            = evtchn_fifo_unmask,
	.handle_events     = evtchn_fifo_handle_events,
	.resume            = evtchn_fifo_resume,
};

static int evtchn_fifo_alloc_control_block(unsigned cpu)
{
	void *control_block = NULL;
	int ret = -ENOMEM;

	control_block = (void *)__get_free_page(GFP_KERNEL);
	if (control_block == NULL)
		goto error;

	ret = init_control_block(cpu, control_block);
	if (ret < 0)
		goto error;

	per_cpu(cpu_control_block, cpu) = control_block;

	return 0;

  error:
	free_page((unsigned long)control_block);
	return ret;
}

static int evtchn_fifo_cpu_notification(struct notifier_block *self,
						  unsigned long action,
						  void *hcpu)
{
	int cpu = (long)hcpu;
	int ret = 0;

	switch (action) {
	case CPU_UP_PREPARE:
		if (!per_cpu(cpu_control_block, cpu))
			ret = evtchn_fifo_alloc_control_block(cpu);
		break;
	default:
		break;
	}
	return ret < 0 ? NOTIFY_BAD : NOTIFY_OK;
}

static struct notifier_block evtchn_fifo_cpu_notifier = {
	.notifier_call	= evtchn_fifo_cpu_notification,
};

int __init xen_evtchn_fifo_init(void)
{
	int cpu = get_cpu();
	int ret;

	ret = evtchn_fifo_alloc_control_block(cpu);
	if (ret < 0)
		goto out;

	pr_info("Using FIFO-based ABI\n");

	evtchn_ops = &evtchn_ops_fifo;

	register_cpu_notifier(&evtchn_fifo_cpu_notifier);
out:
	put_cpu();
	return ret;
}
