// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016,2017 IBM Corporation.
 */

#define pr_fmt(fmt) "xive: " fmt

#include <linux/types.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/msi.h>
#include <linux/vmalloc.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xive.h>
#include <asm/xive-regs.h>
#include <asm/xmon.h>

#include "xive-internal.h"

#undef DEBUG_FLUSH
#undef DEBUG_ALL

#ifdef DEBUG_ALL
#define DBG_VERBOSE(fmt, ...)	pr_devel("cpu %d - " fmt, \
					 smp_processor_id(), ## __VA_ARGS__)
#else
#define DBG_VERBOSE(fmt...)	do { } while(0)
#endif

bool __xive_enabled;
EXPORT_SYMBOL_GPL(__xive_enabled);
bool xive_cmdline_disabled;

/* We use only one priority for now */
static u8 xive_irq_priority;

/* TIMA exported to KVM */
void __iomem *xive_tima;
EXPORT_SYMBOL_GPL(xive_tima);
u32 xive_tima_offset;

/* Backend ops */
static const struct xive_ops *xive_ops;

/* Our global interrupt domain */
static struct irq_domain *xive_irq_domain;

#ifdef CONFIG_SMP
/* The IPIs use the same logical irq number when on the same chip */
static struct xive_ipi_desc {
	unsigned int irq;
	char name[16];
	atomic_t started;
} *xive_ipis;

/*
 * Use early_cpu_to_node() for hot-plugged CPUs
 */
static unsigned int xive_ipi_cpu_to_irq(unsigned int cpu)
{
	return xive_ipis[early_cpu_to_node(cpu)].irq;
}
#endif

/* Xive state for each CPU */
static DEFINE_PER_CPU(struct xive_cpu *, xive_cpu);

/* An invalid CPU target */
#define XIVE_INVALID_TARGET	(-1)

/*
 * Global toggle to switch on/off StoreEOI
 */
static bool xive_store_eoi = true;

static bool xive_is_store_eoi(struct xive_irq_data *xd)
{
	return xd->flags & XIVE_IRQ_FLAG_STORE_EOI && xive_store_eoi;
}

/*
 * Read the next entry in a queue, return its content if it's valid
 * or 0 if there is no new entry.
 *
 * The queue pointer is moved forward unless "just_peek" is set
 */
static u32 xive_read_eq(struct xive_q *q, bool just_peek)
{
	u32 cur;

	if (!q->qpage)
		return 0;
	cur = be32_to_cpup(q->qpage + q->idx);

	/* Check valid bit (31) vs current toggle polarity */
	if ((cur >> 31) == q->toggle)
		return 0;

	/* If consuming from the queue ... */
	if (!just_peek) {
		/* Next entry */
		q->idx = (q->idx + 1) & q->msk;

		/* Wrap around: flip valid toggle */
		if (q->idx == 0)
			q->toggle ^= 1;
	}
	/* Mask out the valid bit (31) */
	return cur & 0x7fffffff;
}

/*
 * Scans all the queue that may have interrupts in them
 * (based on "pending_prio") in priority order until an
 * interrupt is found or all the queues are empty.
 *
 * Then updates the CPPR (Current Processor Priority
 * Register) based on the most favored interrupt found
 * (0xff if none) and return what was found (0 if none).
 *
 * If just_peek is set, return the most favored pending
 * interrupt if any but don't update the queue pointers.
 *
 * Note: This function can operate generically on any number
 * of queues (up to 8). The current implementation of the XIVE
 * driver only uses a single queue however.
 *
 * Note2: This will also "flush" "the pending_count" of a queue
 * into the "count" when that queue is observed to be empty.
 * This is used to keep track of the amount of interrupts
 * targetting a queue. When an interrupt is moved away from
 * a queue, we only decrement that queue count once the queue
 * has been observed empty to avoid races.
 */
static u32 xive_scan_interrupts(struct xive_cpu *xc, bool just_peek)
{
	u32 irq = 0;
	u8 prio = 0;

	/* Find highest pending priority */
	while (xc->pending_prio != 0) {
		struct xive_q *q;

		prio = ffs(xc->pending_prio) - 1;
		DBG_VERBOSE("scan_irq: trying prio %d\n", prio);

		/* Try to fetch */
		irq = xive_read_eq(&xc->queue[prio], just_peek);

		/* Found something ? That's it */
		if (irq) {
			if (just_peek || irq_to_desc(irq))
				break;
			/*
			 * We should never get here; if we do then we must
			 * have failed to synchronize the interrupt properly
			 * when shutting it down.
			 */
			pr_crit("xive: got interrupt %d without descriptor, dropping\n",
				irq);
			WARN_ON(1);
			continue;
		}

		/* Clear pending bits */
		xc->pending_prio &= ~(1 << prio);

		/*
		 * Check if the queue count needs adjusting due to
		 * interrupts being moved away. See description of
		 * xive_dec_target_count()
		 */
		q = &xc->queue[prio];
		if (atomic_read(&q->pending_count)) {
			int p = atomic_xchg(&q->pending_count, 0);
			if (p) {
				WARN_ON(p > atomic_read(&q->count));
				atomic_sub(p, &q->count);
			}
		}
	}

	/* If nothing was found, set CPPR to 0xff */
	if (irq == 0)
		prio = 0xff;

	/* Update HW CPPR to match if necessary */
	if (prio != xc->cppr) {
		DBG_VERBOSE("scan_irq: adjusting CPPR to %d\n", prio);
		xc->cppr = prio;
		out_8(xive_tima + xive_tima_offset + TM_CPPR, prio);
	}

	return irq;
}

/*
 * This is used to perform the magic loads from an ESB
 * described in xive-regs.h
 */
static notrace u8 xive_esb_read(struct xive_irq_data *xd, u32 offset)
{
	u64 val;

	if (offset == XIVE_ESB_SET_PQ_10 && xive_is_store_eoi(xd))
		offset |= XIVE_ESB_LD_ST_MO;

	if ((xd->flags & XIVE_IRQ_FLAG_H_INT_ESB) && xive_ops->esb_rw)
		val = xive_ops->esb_rw(xd->hw_irq, offset, 0, 0);
	else
		val = in_be64(xd->eoi_mmio + offset);

	return (u8)val;
}

static void xive_esb_write(struct xive_irq_data *xd, u32 offset, u64 data)
{
	if ((xd->flags & XIVE_IRQ_FLAG_H_INT_ESB) && xive_ops->esb_rw)
		xive_ops->esb_rw(xd->hw_irq, offset, data, 1);
	else
		out_be64(xd->eoi_mmio + offset, data);
}

#if defined(CONFIG_XMON) || defined(CONFIG_DEBUG_FS)
static void xive_irq_data_dump(struct xive_irq_data *xd, char *buffer, size_t size)
{
	u64 val = xive_esb_read(xd, XIVE_ESB_GET);

	snprintf(buffer, size, "flags=%c%c%c PQ=%c%c 0x%016llx 0x%016llx",
		 xive_is_store_eoi(xd) ? 'S' : ' ',
		 xd->flags & XIVE_IRQ_FLAG_LSI ? 'L' : ' ',
		 xd->flags & XIVE_IRQ_FLAG_H_INT_ESB ? 'H' : ' ',
		 val & XIVE_ESB_VAL_P ? 'P' : '-',
		 val & XIVE_ESB_VAL_Q ? 'Q' : '-',
		 xd->trig_page, xd->eoi_page);
}
#endif

#ifdef CONFIG_XMON
static notrace void xive_dump_eq(const char *name, struct xive_q *q)
{
	u32 i0, i1, idx;

	if (!q->qpage)
		return;
	idx = q->idx;
	i0 = be32_to_cpup(q->qpage + idx);
	idx = (idx + 1) & q->msk;
	i1 = be32_to_cpup(q->qpage + idx);
	xmon_printf("%s idx=%d T=%d %08x %08x ...", name,
		     q->idx, q->toggle, i0, i1);
}

notrace void xmon_xive_do_dump(int cpu)
{
	struct xive_cpu *xc = per_cpu(xive_cpu, cpu);

	xmon_printf("CPU %d:", cpu);
	if (xc) {
		xmon_printf("pp=%02x CPPR=%02x ", xc->pending_prio, xc->cppr);

#ifdef CONFIG_SMP
		{
			char buffer[128];

			xive_irq_data_dump(&xc->ipi_data, buffer, sizeof(buffer));
			xmon_printf("IPI=0x%08x %s", xc->hw_ipi, buffer);
		}
#endif
		xive_dump_eq("EQ", &xc->queue[xive_irq_priority]);
	}
	xmon_printf("\n");
}

static struct irq_data *xive_get_irq_data(u32 hw_irq)
{
	unsigned int irq = irq_find_mapping(xive_irq_domain, hw_irq);

	return irq ? irq_get_irq_data(irq) : NULL;
}

int xmon_xive_get_irq_config(u32 hw_irq, struct irq_data *d)
{
	int rc;
	u32 target;
	u8 prio;
	u32 lirq;

	rc = xive_ops->get_irq_config(hw_irq, &target, &prio, &lirq);
	if (rc) {
		xmon_printf("IRQ 0x%08x : no config rc=%d\n", hw_irq, rc);
		return rc;
	}

	xmon_printf("IRQ 0x%08x : target=0x%x prio=%02x lirq=0x%x ",
		    hw_irq, target, prio, lirq);

	if (!d)
		d = xive_get_irq_data(hw_irq);

	if (d) {
		char buffer[128];

		xive_irq_data_dump(irq_data_get_irq_handler_data(d),
				   buffer, sizeof(buffer));
		xmon_printf("%s", buffer);
	}

	xmon_printf("\n");
	return 0;
}

void xmon_xive_get_irq_all(void)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_data *d = irq_domain_get_irq_data(xive_irq_domain, i);

		if (d)
			xmon_xive_get_irq_config(irqd_to_hwirq(d), d);
	}
}

#endif /* CONFIG_XMON */

static unsigned int xive_get_irq(void)
{
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);
	u32 irq;

	/*
	 * This can be called either as a result of a HW interrupt or
	 * as a "replay" because EOI decided there was still something
	 * in one of the queues.
	 *
	 * First we perform an ACK cycle in order to update our mask
	 * of pending priorities. This will also have the effect of
	 * updating the CPPR to the most favored pending interrupts.
	 *
	 * In the future, if we have a way to differentiate a first
	 * entry (on HW interrupt) from a replay triggered by EOI,
	 * we could skip this on replays unless we soft-mask tells us
	 * that a new HW interrupt occurred.
	 */
	xive_ops->update_pending(xc);

	DBG_VERBOSE("get_irq: pending=%02x\n", xc->pending_prio);

	/* Scan our queue(s) for interrupts */
	irq = xive_scan_interrupts(xc, false);

	DBG_VERBOSE("get_irq: got irq 0x%x, new pending=0x%02x\n",
	    irq, xc->pending_prio);

	/* Return pending interrupt if any */
	if (irq == XIVE_BAD_IRQ)
		return 0;
	return irq;
}

/*
 * After EOI'ing an interrupt, we need to re-check the queue
 * to see if another interrupt is pending since multiple
 * interrupts can coalesce into a single notification to the
 * CPU.
 *
 * If we find that there is indeed more in there, we call
 * force_external_irq_replay() to make Linux synthetize an
 * external interrupt on the next call to local_irq_restore().
 */
static void xive_do_queue_eoi(struct xive_cpu *xc)
{
	if (xive_scan_interrupts(xc, true) != 0) {
		DBG_VERBOSE("eoi: pending=0x%02x\n", xc->pending_prio);
		force_external_irq_replay();
	}
}

/*
 * EOI an interrupt at the source. There are several methods
 * to do this depending on the HW version and source type
 */
static void xive_do_source_eoi(struct xive_irq_data *xd)
{
	u8 eoi_val;

	xd->stale_p = false;

	/* If the XIVE supports the new "store EOI facility, use it */
	if (xive_is_store_eoi(xd)) {
		xive_esb_write(xd, XIVE_ESB_STORE_EOI, 0);
		return;
	}

	/*
	 * For LSIs, we use the "EOI cycle" special load rather than
	 * PQ bits, as they are automatically re-triggered in HW when
	 * still pending.
	 */
	if (xd->flags & XIVE_IRQ_FLAG_LSI) {
		xive_esb_read(xd, XIVE_ESB_LOAD_EOI);
		return;
	}

	/*
	 * Otherwise, we use the special MMIO that does a clear of
	 * both P and Q and returns the old Q. This allows us to then
	 * do a re-trigger if Q was set rather than synthesizing an
	 * interrupt in software
	 */
	eoi_val = xive_esb_read(xd, XIVE_ESB_SET_PQ_00);
	DBG_VERBOSE("eoi_val=%x\n", eoi_val);

	/* Re-trigger if needed */
	if ((eoi_val & XIVE_ESB_VAL_Q) && xd->trig_mmio)
		out_be64(xd->trig_mmio, 0);
}

/* irq_chip eoi callback, called with irq descriptor lock held */
static void xive_irq_eoi(struct irq_data *d)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);

	DBG_VERBOSE("eoi_irq: irq=%d [0x%lx] pending=%02x\n",
		    d->irq, irqd_to_hwirq(d), xc->pending_prio);

	/*
	 * EOI the source if it hasn't been disabled and hasn't
	 * been passed-through to a KVM guest
	 */
	if (!irqd_irq_disabled(d) && !irqd_is_forwarded_to_vcpu(d) &&
	    !(xd->flags & XIVE_IRQ_FLAG_NO_EOI))
		xive_do_source_eoi(xd);
	else
		xd->stale_p = true;

	/*
	 * Clear saved_p to indicate that it's no longer occupying
	 * a queue slot on the target queue
	 */
	xd->saved_p = false;

	/* Check for more work in the queue */
	xive_do_queue_eoi(xc);
}

/*
 * Helper used to mask and unmask an interrupt source.
 */
static void xive_do_source_set_mask(struct xive_irq_data *xd,
				    bool mask)
{
	u64 val;

	pr_debug("%s: HW 0x%x %smask\n", __func__, xd->hw_irq, mask ? "" : "un");

	/*
	 * If the interrupt had P set, it may be in a queue.
	 *
	 * We need to make sure we don't re-enable it until it
	 * has been fetched from that queue and EOId. We keep
	 * a copy of that P state and use it to restore the
	 * ESB accordingly on unmask.
	 */
	if (mask) {
		val = xive_esb_read(xd, XIVE_ESB_SET_PQ_01);
		if (!xd->stale_p && !!(val & XIVE_ESB_VAL_P))
			xd->saved_p = true;
		xd->stale_p = false;
	} else if (xd->saved_p) {
		xive_esb_read(xd, XIVE_ESB_SET_PQ_10);
		xd->saved_p = false;
	} else {
		xive_esb_read(xd, XIVE_ESB_SET_PQ_00);
		xd->stale_p = false;
	}
}

/*
 * Try to chose "cpu" as a new interrupt target. Increments
 * the queue accounting for that target if it's not already
 * full.
 */
static bool xive_try_pick_target(int cpu)
{
	struct xive_cpu *xc = per_cpu(xive_cpu, cpu);
	struct xive_q *q = &xc->queue[xive_irq_priority];
	int max;

	/*
	 * Calculate max number of interrupts in that queue.
	 *
	 * We leave a gap of 1 just in case...
	 */
	max = (q->msk + 1) - 1;
	return !!atomic_add_unless(&q->count, 1, max);
}

/*
 * Un-account an interrupt for a target CPU. We don't directly
 * decrement q->count since the interrupt might still be present
 * in the queue.
 *
 * Instead increment a separate counter "pending_count" which
 * will be substracted from "count" later when that CPU observes
 * the queue to be empty.
 */
static void xive_dec_target_count(int cpu)
{
	struct xive_cpu *xc = per_cpu(xive_cpu, cpu);
	struct xive_q *q = &xc->queue[xive_irq_priority];

	if (WARN_ON(cpu < 0 || !xc)) {
		pr_err("%s: cpu=%d xc=%p\n", __func__, cpu, xc);
		return;
	}

	/*
	 * We increment the "pending count" which will be used
	 * to decrement the target queue count whenever it's next
	 * processed and found empty. This ensure that we don't
	 * decrement while we still have the interrupt there
	 * occupying a slot.
	 */
	atomic_inc(&q->pending_count);
}

/* Find a tentative CPU target in a CPU mask */
static int xive_find_target_in_mask(const struct cpumask *mask,
				    unsigned int fuzz)
{
	int cpu, first, num, i;

	/* Pick up a starting point CPU in the mask based on  fuzz */
	num = min_t(int, cpumask_weight(mask), nr_cpu_ids);
	first = fuzz % num;

	/* Locate it */
	cpu = cpumask_first(mask);
	for (i = 0; i < first && cpu < nr_cpu_ids; i++)
		cpu = cpumask_next(cpu, mask);

	/* Sanity check */
	if (WARN_ON(cpu >= nr_cpu_ids))
		cpu = cpumask_first(cpu_online_mask);

	/* Remember first one to handle wrap-around */
	first = cpu;

	/*
	 * Now go through the entire mask until we find a valid
	 * target.
	 */
	do {
		/*
		 * We re-check online as the fallback case passes us
		 * an untested affinity mask
		 */
		if (cpu_online(cpu) && xive_try_pick_target(cpu))
			return cpu;
		cpu = cpumask_next(cpu, mask);
		/* Wrap around */
		if (cpu >= nr_cpu_ids)
			cpu = cpumask_first(mask);
	} while (cpu != first);

	return -1;
}

/*
 * Pick a target CPU for an interrupt. This is done at
 * startup or if the affinity is changed in a way that
 * invalidates the current target.
 */
static int xive_pick_irq_target(struct irq_data *d,
				const struct cpumask *affinity)
{
	static unsigned int fuzz;
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);
	cpumask_var_t mask;
	int cpu = -1;

	/*
	 * If we have chip IDs, first we try to build a mask of
	 * CPUs matching the CPU and find a target in there
	 */
	if (xd->src_chip != XIVE_INVALID_CHIP_ID &&
		zalloc_cpumask_var(&mask, GFP_ATOMIC)) {
		/* Build a mask of matching chip IDs */
		for_each_cpu_and(cpu, affinity, cpu_online_mask) {
			struct xive_cpu *xc = per_cpu(xive_cpu, cpu);
			if (xc->chip_id == xd->src_chip)
				cpumask_set_cpu(cpu, mask);
		}
		/* Try to find a target */
		if (cpumask_empty(mask))
			cpu = -1;
		else
			cpu = xive_find_target_in_mask(mask, fuzz++);
		free_cpumask_var(mask);
		if (cpu >= 0)
			return cpu;
		fuzz--;
	}

	/* No chip IDs, fallback to using the affinity mask */
	return xive_find_target_in_mask(affinity, fuzz++);
}

static unsigned int xive_irq_startup(struct irq_data *d)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	int target, rc;

	xd->saved_p = false;
	xd->stale_p = false;

	pr_debug("%s: irq %d [0x%x] data @%p\n", __func__, d->irq, hw_irq, d);

	/* Pick a target */
	target = xive_pick_irq_target(d, irq_data_get_affinity_mask(d));
	if (target == XIVE_INVALID_TARGET) {
		/* Try again breaking affinity */
		target = xive_pick_irq_target(d, cpu_online_mask);
		if (target == XIVE_INVALID_TARGET)
			return -ENXIO;
		pr_warn("irq %d started with broken affinity\n", d->irq);
	}

	/* Sanity check */
	if (WARN_ON(target == XIVE_INVALID_TARGET ||
		    target >= nr_cpu_ids))
		target = smp_processor_id();

	xd->target = target;

	/*
	 * Configure the logical number to be the Linux IRQ number
	 * and set the target queue
	 */
	rc = xive_ops->configure_irq(hw_irq,
				     get_hard_smp_processor_id(target),
				     xive_irq_priority, d->irq);
	if (rc)
		return rc;

	/* Unmask the ESB */
	xive_do_source_set_mask(xd, false);

	return 0;
}

/* called with irq descriptor lock held */
static void xive_irq_shutdown(struct irq_data *d)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);

	pr_debug("%s: irq %d [0x%x] data @%p\n", __func__, d->irq, hw_irq, d);

	if (WARN_ON(xd->target == XIVE_INVALID_TARGET))
		return;

	/* Mask the interrupt at the source */
	xive_do_source_set_mask(xd, true);

	/*
	 * Mask the interrupt in HW in the IVT/EAS and set the number
	 * to be the "bad" IRQ number
	 */
	xive_ops->configure_irq(hw_irq,
				get_hard_smp_processor_id(xd->target),
				0xff, XIVE_BAD_IRQ);

	xive_dec_target_count(xd->target);
	xd->target = XIVE_INVALID_TARGET;
}

static void xive_irq_unmask(struct irq_data *d)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);

	pr_debug("%s: irq %d data @%p\n", __func__, d->irq, xd);

	xive_do_source_set_mask(xd, false);
}

static void xive_irq_mask(struct irq_data *d)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);

	pr_debug("%s: irq %d data @%p\n", __func__, d->irq, xd);

	xive_do_source_set_mask(xd, true);
}

static int xive_irq_set_affinity(struct irq_data *d,
				 const struct cpumask *cpumask,
				 bool force)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	u32 target, old_target;
	int rc = 0;

	pr_debug("%s: irq %d/0x%x\n", __func__, d->irq, hw_irq);

	/* Is this valid ? */
	if (cpumask_any_and(cpumask, cpu_online_mask) >= nr_cpu_ids)
		return -EINVAL;

	/*
	 * If existing target is already in the new mask, and is
	 * online then do nothing.
	 */
	if (xd->target != XIVE_INVALID_TARGET &&
	    cpu_online(xd->target) &&
	    cpumask_test_cpu(xd->target, cpumask))
		return IRQ_SET_MASK_OK;

	/* Pick a new target */
	target = xive_pick_irq_target(d, cpumask);

	/* No target found */
	if (target == XIVE_INVALID_TARGET)
		return -ENXIO;

	/* Sanity check */
	if (WARN_ON(target >= nr_cpu_ids))
		target = smp_processor_id();

	old_target = xd->target;

	/*
	 * Only configure the irq if it's not currently passed-through to
	 * a KVM guest
	 */
	if (!irqd_is_forwarded_to_vcpu(d))
		rc = xive_ops->configure_irq(hw_irq,
					     get_hard_smp_processor_id(target),
					     xive_irq_priority, d->irq);
	if (rc < 0) {
		pr_err("Error %d reconfiguring irq %d\n", rc, d->irq);
		return rc;
	}

	pr_debug("  target: 0x%x\n", target);
	xd->target = target;

	/* Give up previous target */
	if (old_target != XIVE_INVALID_TARGET)
	    xive_dec_target_count(old_target);

	return IRQ_SET_MASK_OK;
}

static int xive_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);

	/*
	 * We only support these. This has really no effect other than setting
	 * the corresponding descriptor bits mind you but those will in turn
	 * affect the resend function when re-enabling an edge interrupt.
	 *
	 * Set set the default to edge as explained in map().
	 */
	if (flow_type == IRQ_TYPE_DEFAULT || flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_EDGE_RISING;

	if (flow_type != IRQ_TYPE_EDGE_RISING &&
	    flow_type != IRQ_TYPE_LEVEL_LOW)
		return -EINVAL;

	irqd_set_trigger_type(d, flow_type);

	/*
	 * Double check it matches what the FW thinks
	 *
	 * NOTE: We don't know yet if the PAPR interface will provide
	 * the LSI vs MSI information apart from the device-tree so
	 * this check might have to move into an optional backend call
	 * that is specific to the native backend
	 */
	if ((flow_type == IRQ_TYPE_LEVEL_LOW) !=
	    !!(xd->flags & XIVE_IRQ_FLAG_LSI)) {
		pr_warn("Interrupt %d (HW 0x%x) type mismatch, Linux says %s, FW says %s\n",
			d->irq, (u32)irqd_to_hwirq(d),
			(flow_type == IRQ_TYPE_LEVEL_LOW) ? "Level" : "Edge",
			(xd->flags & XIVE_IRQ_FLAG_LSI) ? "Level" : "Edge");
	}

	return IRQ_SET_MASK_OK_NOCOPY;
}

static int xive_irq_retrigger(struct irq_data *d)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);

	/* This should be only for MSIs */
	if (WARN_ON(xd->flags & XIVE_IRQ_FLAG_LSI))
		return 0;

	/*
	 * To perform a retrigger, we first set the PQ bits to
	 * 11, then perform an EOI.
	 */
	xive_esb_read(xd, XIVE_ESB_SET_PQ_11);
	xive_do_source_eoi(xd);

	return 1;
}

/*
 * Caller holds the irq descriptor lock, so this won't be called
 * concurrently with xive_get_irqchip_state on the same interrupt.
 */
static int xive_irq_set_vcpu_affinity(struct irq_data *d, void *state)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	int rc;
	u8 pq;

	/*
	 * This is called by KVM with state non-NULL for enabling
	 * pass-through or NULL for disabling it
	 */
	if (state) {
		irqd_set_forwarded_to_vcpu(d);

		/* Set it to PQ=10 state to prevent further sends */
		pq = xive_esb_read(xd, XIVE_ESB_SET_PQ_10);
		if (!xd->stale_p) {
			xd->saved_p = !!(pq & XIVE_ESB_VAL_P);
			xd->stale_p = !xd->saved_p;
		}

		/* No target ? nothing to do */
		if (xd->target == XIVE_INVALID_TARGET) {
			/*
			 * An untargetted interrupt should have been
			 * also masked at the source
			 */
			WARN_ON(xd->saved_p);

			return 0;
		}

		/*
		 * If P was set, adjust state to PQ=11 to indicate
		 * that a resend is needed for the interrupt to reach
		 * the guest. Also remember the value of P.
		 *
		 * This also tells us that it's in flight to a host queue
		 * or has already been fetched but hasn't been EOIed yet
		 * by the host. This it's potentially using up a host
		 * queue slot. This is important to know because as long
		 * as this is the case, we must not hard-unmask it when
		 * "returning" that interrupt to the host.
		 *
		 * This saved_p is cleared by the host EOI, when we know
		 * for sure the queue slot is no longer in use.
		 */
		if (xd->saved_p) {
			xive_esb_read(xd, XIVE_ESB_SET_PQ_11);

			/*
			 * Sync the XIVE source HW to ensure the interrupt
			 * has gone through the EAS before we change its
			 * target to the guest. That should guarantee us
			 * that we *will* eventually get an EOI for it on
			 * the host. Otherwise there would be a small window
			 * for P to be seen here but the interrupt going
			 * to the guest queue.
			 */
			if (xive_ops->sync_source)
				xive_ops->sync_source(hw_irq);
		}
	} else {
		irqd_clr_forwarded_to_vcpu(d);

		/* No host target ? hard mask and return */
		if (xd->target == XIVE_INVALID_TARGET) {
			xive_do_source_set_mask(xd, true);
			return 0;
		}

		/*
		 * Sync the XIVE source HW to ensure the interrupt
		 * has gone through the EAS before we change its
		 * target to the host.
		 */
		if (xive_ops->sync_source)
			xive_ops->sync_source(hw_irq);

		/*
		 * By convention we are called with the interrupt in
		 * a PQ=10 or PQ=11 state, ie, it won't fire and will
		 * have latched in Q whether there's a pending HW
		 * interrupt or not.
		 *
		 * First reconfigure the target.
		 */
		rc = xive_ops->configure_irq(hw_irq,
					     get_hard_smp_processor_id(xd->target),
					     xive_irq_priority, d->irq);
		if (rc)
			return rc;

		/*
		 * Then if saved_p is not set, effectively re-enable the
		 * interrupt with an EOI. If it is set, we know there is
		 * still a message in a host queue somewhere that will be
		 * EOId eventually.
		 *
		 * Note: We don't check irqd_irq_disabled(). Effectively,
		 * we *will* let the irq get through even if masked if the
		 * HW is still firing it in order to deal with the whole
		 * saved_p business properly. If the interrupt triggers
		 * while masked, the generic code will re-mask it anyway.
		 */
		if (!xd->saved_p)
			xive_do_source_eoi(xd);

	}
	return 0;
}

/* Called with irq descriptor lock held. */
static int xive_get_irqchip_state(struct irq_data *data,
				  enum irqchip_irq_state which, bool *state)
{
	struct xive_irq_data *xd = irq_data_get_irq_handler_data(data);
	u8 pq;

	switch (which) {
	case IRQCHIP_STATE_ACTIVE:
		pq = xive_esb_read(xd, XIVE_ESB_GET);

		/*
		 * The esb value being all 1's means we couldn't get
		 * the PQ state of the interrupt through mmio. It may
		 * happen, for example when querying a PHB interrupt
		 * while the PHB is in an error state. We consider the
		 * interrupt to be inactive in that case.
		 */
		*state = (pq != XIVE_ESB_INVALID) && !xd->stale_p &&
			(xd->saved_p || (!!(pq & XIVE_ESB_VAL_P) &&
			 !irqd_irq_disabled(data)));
		return 0;
	default:
		return -EINVAL;
	}
}

static struct irq_chip xive_irq_chip = {
	.name = "XIVE-IRQ",
	.irq_startup = xive_irq_startup,
	.irq_shutdown = xive_irq_shutdown,
	.irq_eoi = xive_irq_eoi,
	.irq_mask = xive_irq_mask,
	.irq_unmask = xive_irq_unmask,
	.irq_set_affinity = xive_irq_set_affinity,
	.irq_set_type = xive_irq_set_type,
	.irq_retrigger = xive_irq_retrigger,
	.irq_set_vcpu_affinity = xive_irq_set_vcpu_affinity,
	.irq_get_irqchip_state = xive_get_irqchip_state,
};

bool is_xive_irq(struct irq_chip *chip)
{
	return chip == &xive_irq_chip;
}
EXPORT_SYMBOL_GPL(is_xive_irq);

void xive_cleanup_irq_data(struct xive_irq_data *xd)
{
	pr_debug("%s for HW 0x%x\n", __func__, xd->hw_irq);

	if (xd->eoi_mmio) {
		iounmap(xd->eoi_mmio);
		if (xd->eoi_mmio == xd->trig_mmio)
			xd->trig_mmio = NULL;
		xd->eoi_mmio = NULL;
	}
	if (xd->trig_mmio) {
		iounmap(xd->trig_mmio);
		xd->trig_mmio = NULL;
	}
}
EXPORT_SYMBOL_GPL(xive_cleanup_irq_data);

static int xive_irq_alloc_data(unsigned int virq, irq_hw_number_t hw)
{
	struct xive_irq_data *xd;
	int rc;

	xd = kzalloc(sizeof(struct xive_irq_data), GFP_KERNEL);
	if (!xd)
		return -ENOMEM;
	rc = xive_ops->populate_irq_data(hw, xd);
	if (rc) {
		kfree(xd);
		return rc;
	}
	xd->target = XIVE_INVALID_TARGET;
	irq_set_handler_data(virq, xd);

	/*
	 * Turn OFF by default the interrupt being mapped. A side
	 * effect of this check is the mapping the ESB page of the
	 * interrupt in the Linux address space. This prevents page
	 * fault issues in the crash handler which masks all
	 * interrupts.
	 */
	xive_esb_read(xd, XIVE_ESB_SET_PQ_01);

	return 0;
}

void xive_irq_free_data(unsigned int virq)
{
	struct xive_irq_data *xd = irq_get_handler_data(virq);

	if (!xd)
		return;
	irq_set_handler_data(virq, NULL);
	xive_cleanup_irq_data(xd);
	kfree(xd);
}
EXPORT_SYMBOL_GPL(xive_irq_free_data);

#ifdef CONFIG_SMP

static void xive_cause_ipi(int cpu)
{
	struct xive_cpu *xc;
	struct xive_irq_data *xd;

	xc = per_cpu(xive_cpu, cpu);

	DBG_VERBOSE("IPI CPU %d -> %d (HW IRQ 0x%x)\n",
		    smp_processor_id(), cpu, xc->hw_ipi);

	xd = &xc->ipi_data;
	if (WARN_ON(!xd->trig_mmio))
		return;
	out_be64(xd->trig_mmio, 0);
}

static irqreturn_t xive_muxed_ipi_action(int irq, void *dev_id)
{
	return smp_ipi_demux();
}

static void xive_ipi_eoi(struct irq_data *d)
{
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);

	/* Handle possible race with unplug and drop stale IPIs */
	if (!xc)
		return;

	DBG_VERBOSE("IPI eoi: irq=%d [0x%lx] (HW IRQ 0x%x) pending=%02x\n",
		    d->irq, irqd_to_hwirq(d), xc->hw_ipi, xc->pending_prio);

	xive_do_source_eoi(&xc->ipi_data);
	xive_do_queue_eoi(xc);
}

static void xive_ipi_do_nothing(struct irq_data *d)
{
	/*
	 * Nothing to do, we never mask/unmask IPIs, but the callback
	 * has to exist for the struct irq_chip.
	 */
}

static struct irq_chip xive_ipi_chip = {
	.name = "XIVE-IPI",
	.irq_eoi = xive_ipi_eoi,
	.irq_mask = xive_ipi_do_nothing,
	.irq_unmask = xive_ipi_do_nothing,
};

/*
 * IPIs are marked per-cpu. We use separate HW interrupts under the
 * hood but associated with the same "linux" interrupt
 */
struct xive_ipi_alloc_info {
	irq_hw_number_t hwirq;
};

static int xive_ipi_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				     unsigned int nr_irqs, void *arg)
{
	struct xive_ipi_alloc_info *info = arg;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, info->hwirq + i, &xive_ipi_chip,
				    domain->host_data, handle_percpu_irq,
				    NULL, NULL);
	}
	return 0;
}

static const struct irq_domain_ops xive_ipi_irq_domain_ops = {
	.alloc  = xive_ipi_irq_domain_alloc,
};

static int __init xive_init_ipis(void)
{
	struct fwnode_handle *fwnode;
	struct irq_domain *ipi_domain;
	unsigned int node;
	int ret = -ENOMEM;

	fwnode = irq_domain_alloc_named_fwnode("XIVE-IPI");
	if (!fwnode)
		goto out;

	ipi_domain = irq_domain_create_linear(fwnode, nr_node_ids,
					      &xive_ipi_irq_domain_ops, NULL);
	if (!ipi_domain)
		goto out_free_fwnode;

	xive_ipis = kcalloc(nr_node_ids, sizeof(*xive_ipis), GFP_KERNEL | __GFP_NOFAIL);
	if (!xive_ipis)
		goto out_free_domain;

	for_each_node(node) {
		struct xive_ipi_desc *xid = &xive_ipis[node];
		struct xive_ipi_alloc_info info = { node };

		/*
		 * Map one IPI interrupt per node for all cpus of that node.
		 * Since the HW interrupt number doesn't have any meaning,
		 * simply use the node number.
		 */
		ret = irq_domain_alloc_irqs(ipi_domain, 1, node, &info);
		if (ret < 0)
			goto out_free_xive_ipis;
		xid->irq = ret;

		snprintf(xid->name, sizeof(xid->name), "IPI-%d", node);
	}

	return ret;

out_free_xive_ipis:
	kfree(xive_ipis);
out_free_domain:
	irq_domain_remove(ipi_domain);
out_free_fwnode:
	irq_domain_free_fwnode(fwnode);
out:
	return ret;
}

static int xive_request_ipi(unsigned int cpu)
{
	struct xive_ipi_desc *xid = &xive_ipis[early_cpu_to_node(cpu)];
	int ret;

	if (atomic_inc_return(&xid->started) > 1)
		return 0;

	ret = request_irq(xid->irq, xive_muxed_ipi_action,
			  IRQF_NO_DEBUG | IRQF_PERCPU | IRQF_NO_THREAD,
			  xid->name, NULL);

	WARN(ret < 0, "Failed to request IPI %d: %d\n", xid->irq, ret);
	return ret;
}

static int xive_setup_cpu_ipi(unsigned int cpu)
{
	unsigned int xive_ipi_irq = xive_ipi_cpu_to_irq(cpu);
	struct xive_cpu *xc;
	int rc;

	pr_debug("Setting up IPI for CPU %d\n", cpu);

	xc = per_cpu(xive_cpu, cpu);

	/* Check if we are already setup */
	if (xc->hw_ipi != XIVE_BAD_IRQ)
		return 0;

	/* Register the IPI */
	xive_request_ipi(cpu);

	/* Grab an IPI from the backend, this will populate xc->hw_ipi */
	if (xive_ops->get_ipi(cpu, xc))
		return -EIO;

	/*
	 * Populate the IRQ data in the xive_cpu structure and
	 * configure the HW / enable the IPIs.
	 */
	rc = xive_ops->populate_irq_data(xc->hw_ipi, &xc->ipi_data);
	if (rc) {
		pr_err("Failed to populate IPI data on CPU %d\n", cpu);
		return -EIO;
	}
	rc = xive_ops->configure_irq(xc->hw_ipi,
				     get_hard_smp_processor_id(cpu),
				     xive_irq_priority, xive_ipi_irq);
	if (rc) {
		pr_err("Failed to map IPI CPU %d\n", cpu);
		return -EIO;
	}
	pr_debug("CPU %d HW IPI 0x%x, virq %d, trig_mmio=%p\n", cpu,
		 xc->hw_ipi, xive_ipi_irq, xc->ipi_data.trig_mmio);

	/* Unmask it */
	xive_do_source_set_mask(&xc->ipi_data, false);

	return 0;
}

static void xive_cleanup_cpu_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	unsigned int xive_ipi_irq = xive_ipi_cpu_to_irq(cpu);

	/* Disable the IPI and free the IRQ data */

	/* Already cleaned up ? */
	if (xc->hw_ipi == XIVE_BAD_IRQ)
		return;

	/* TODO: clear IPI mapping */

	/* Mask the IPI */
	xive_do_source_set_mask(&xc->ipi_data, true);

	/*
	 * Note: We don't call xive_cleanup_irq_data() to free
	 * the mappings as this is called from an IPI on kexec
	 * which is not a safe environment to call iounmap()
	 */

	/* Deconfigure/mask in the backend */
	xive_ops->configure_irq(xc->hw_ipi, hard_smp_processor_id(),
				0xff, xive_ipi_irq);

	/* Free the IPIs in the backend */
	xive_ops->put_ipi(cpu, xc);
}

void __init xive_smp_probe(void)
{
	smp_ops->cause_ipi = xive_cause_ipi;

	/* Register the IPI */
	xive_init_ipis();

	/* Allocate and setup IPI for the boot CPU */
	xive_setup_cpu_ipi(smp_processor_id());
}

#endif /* CONFIG_SMP */

static int xive_irq_domain_map(struct irq_domain *h, unsigned int virq,
			       irq_hw_number_t hw)
{
	int rc;

	/*
	 * Mark interrupts as edge sensitive by default so that resend
	 * actually works. Will fix that up below if needed.
	 */
	irq_clear_status_flags(virq, IRQ_LEVEL);

	rc = xive_irq_alloc_data(virq, hw);
	if (rc)
		return rc;

	irq_set_chip_and_handler(virq, &xive_irq_chip, handle_fasteoi_irq);

	return 0;
}

static void xive_irq_domain_unmap(struct irq_domain *d, unsigned int virq)
{
	xive_irq_free_data(virq);
}

static int xive_irq_domain_xlate(struct irq_domain *h, struct device_node *ct,
				 const u32 *intspec, unsigned int intsize,
				 irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	*out_hwirq = intspec[0];

	/*
	 * If intsize is at least 2, we look for the type in the second cell,
	 * we assume the LSB indicates a level interrupt.
	 */
	if (intsize > 1) {
		if (intspec[1] & 1)
			*out_flags = IRQ_TYPE_LEVEL_LOW;
		else
			*out_flags = IRQ_TYPE_EDGE_RISING;
	} else
		*out_flags = IRQ_TYPE_LEVEL_LOW;

	return 0;
}

static int xive_irq_domain_match(struct irq_domain *h, struct device_node *node,
				 enum irq_domain_bus_token bus_token)
{
	return xive_ops->match(node);
}

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
static const char * const esb_names[] = { "RESET", "OFF", "PENDING", "QUEUED" };

static const struct {
	u64  mask;
	char *name;
} xive_irq_flags[] = {
	{ XIVE_IRQ_FLAG_STORE_EOI, "STORE_EOI" },
	{ XIVE_IRQ_FLAG_LSI,       "LSI"       },
	{ XIVE_IRQ_FLAG_H_INT_ESB, "H_INT_ESB" },
	{ XIVE_IRQ_FLAG_NO_EOI,    "NO_EOI"    },
};

static void xive_irq_domain_debug_show(struct seq_file *m, struct irq_domain *d,
				       struct irq_data *irqd, int ind)
{
	struct xive_irq_data *xd;
	u64 val;
	int i;

	/* No IRQ domain level information. To be done */
	if (!irqd)
		return;

	if (!is_xive_irq(irq_data_get_irq_chip(irqd)))
		return;

	seq_printf(m, "%*sXIVE:\n", ind, "");
	ind++;

	xd = irq_data_get_irq_handler_data(irqd);
	if (!xd) {
		seq_printf(m, "%*snot assigned\n", ind, "");
		return;
	}

	val = xive_esb_read(xd, XIVE_ESB_GET);
	seq_printf(m, "%*sESB:      %s\n", ind, "", esb_names[val & 0x3]);
	seq_printf(m, "%*sPstate:   %s %s\n", ind, "", xd->stale_p ? "stale" : "",
		   xd->saved_p ? "saved" : "");
	seq_printf(m, "%*sTarget:   %d\n", ind, "", xd->target);
	seq_printf(m, "%*sChip:     %d\n", ind, "", xd->src_chip);
	seq_printf(m, "%*sTrigger:  0x%016llx\n", ind, "", xd->trig_page);
	seq_printf(m, "%*sEOI:      0x%016llx\n", ind, "", xd->eoi_page);
	seq_printf(m, "%*sFlags:    0x%llx\n", ind, "", xd->flags);
	for (i = 0; i < ARRAY_SIZE(xive_irq_flags); i++) {
		if (xd->flags & xive_irq_flags[i].mask)
			seq_printf(m, "%*s%s\n", ind + 12, "", xive_irq_flags[i].name);
	}
}
#endif

#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
static int xive_irq_domain_translate(struct irq_domain *d,
				     struct irq_fwspec *fwspec,
				     unsigned long *hwirq,
				     unsigned int *type)
{
	return xive_irq_domain_xlate(d, to_of_node(fwspec->fwnode),
				     fwspec->param, fwspec->param_count,
				     hwirq, type);
}

static int xive_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *arg)
{
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	int i, rc;

	rc = xive_irq_domain_translate(domain, fwspec, &hwirq, &type);
	if (rc)
		return rc;

	pr_debug("%s %d/0x%lx #%d\n", __func__, virq, hwirq, nr_irqs);

	for (i = 0; i < nr_irqs; i++) {
		/* TODO: call xive_irq_domain_map() */

		/*
		 * Mark interrupts as edge sensitive by default so that resend
		 * actually works. Will fix that up below if needed.
		 */
		irq_clear_status_flags(virq, IRQ_LEVEL);

		/* allocates and sets handler data */
		rc = xive_irq_alloc_data(virq + i, hwirq + i);
		if (rc)
			return rc;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &xive_irq_chip, domain->host_data);
		irq_set_handler(virq + i, handle_fasteoi_irq);
	}

	return 0;
}

static void xive_irq_domain_free(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	int i;

	pr_debug("%s %d #%d\n", __func__, virq, nr_irqs);

	for (i = 0; i < nr_irqs; i++)
		xive_irq_free_data(virq + i);
}
#endif

static const struct irq_domain_ops xive_irq_domain_ops = {
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	.alloc	= xive_irq_domain_alloc,
	.free	= xive_irq_domain_free,
	.translate = xive_irq_domain_translate,
#endif
	.match = xive_irq_domain_match,
	.map = xive_irq_domain_map,
	.unmap = xive_irq_domain_unmap,
	.xlate = xive_irq_domain_xlate,
#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
	.debug_show = xive_irq_domain_debug_show,
#endif
};

static void __init xive_init_host(struct device_node *np)
{
	xive_irq_domain = irq_domain_add_tree(np, &xive_irq_domain_ops, NULL);
	if (WARN_ON(xive_irq_domain == NULL))
		return;
	irq_set_default_host(xive_irq_domain);
}

static void xive_cleanup_cpu_queues(unsigned int cpu, struct xive_cpu *xc)
{
	if (xc->queue[xive_irq_priority].qpage)
		xive_ops->cleanup_queue(cpu, xc, xive_irq_priority);
}

static int xive_setup_cpu_queues(unsigned int cpu, struct xive_cpu *xc)
{
	int rc = 0;

	/* We setup 1 queues for now with a 64k page */
	if (!xc->queue[xive_irq_priority].qpage)
		rc = xive_ops->setup_queue(cpu, xc, xive_irq_priority);

	return rc;
}

static int xive_prepare_cpu(unsigned int cpu)
{
	struct xive_cpu *xc;

	xc = per_cpu(xive_cpu, cpu);
	if (!xc) {
		xc = kzalloc_node(sizeof(struct xive_cpu),
				  GFP_KERNEL, cpu_to_node(cpu));
		if (!xc)
			return -ENOMEM;
		xc->hw_ipi = XIVE_BAD_IRQ;
		xc->chip_id = XIVE_INVALID_CHIP_ID;
		if (xive_ops->prepare_cpu)
			xive_ops->prepare_cpu(cpu, xc);

		per_cpu(xive_cpu, cpu) = xc;
	}

	/* Setup EQs if not already */
	return xive_setup_cpu_queues(cpu, xc);
}

static void xive_setup_cpu(void)
{
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);

	/* The backend might have additional things to do */
	if (xive_ops->setup_cpu)
		xive_ops->setup_cpu(smp_processor_id(), xc);

	/* Set CPPR to 0xff to enable flow of interrupts */
	xc->cppr = 0xff;
	out_8(xive_tima + xive_tima_offset + TM_CPPR, 0xff);
}

#ifdef CONFIG_SMP
void xive_smp_setup_cpu(void)
{
	pr_debug("SMP setup CPU %d\n", smp_processor_id());

	/* This will have already been done on the boot CPU */
	if (smp_processor_id() != boot_cpuid)
		xive_setup_cpu();

}

int xive_smp_prepare_cpu(unsigned int cpu)
{
	int rc;

	/* Allocate per-CPU data and queues */
	rc = xive_prepare_cpu(cpu);
	if (rc)
		return rc;

	/* Allocate and setup IPI for the new CPU */
	return xive_setup_cpu_ipi(cpu);
}

#ifdef CONFIG_HOTPLUG_CPU
static void xive_flush_cpu_queue(unsigned int cpu, struct xive_cpu *xc)
{
	u32 irq;

	/* We assume local irqs are disabled */
	WARN_ON(!irqs_disabled());

	/* Check what's already in the CPU queue */
	while ((irq = xive_scan_interrupts(xc, false)) != 0) {
		/*
		 * We need to re-route that interrupt to its new destination.
		 * First get and lock the descriptor
		 */
		struct irq_desc *desc = irq_to_desc(irq);
		struct irq_data *d = irq_desc_get_irq_data(desc);
		struct xive_irq_data *xd;

		/*
		 * Ignore anything that isn't a XIVE irq and ignore
		 * IPIs, so can just be dropped.
		 */
		if (d->domain != xive_irq_domain)
			continue;

		/*
		 * The IRQ should have already been re-routed, it's just a
		 * stale in the old queue, so re-trigger it in order to make
		 * it reach is new destination.
		 */
#ifdef DEBUG_FLUSH
		pr_info("CPU %d: Got irq %d while offline, re-sending...\n",
			cpu, irq);
#endif
		raw_spin_lock(&desc->lock);
		xd = irq_desc_get_handler_data(desc);

		/*
		 * Clear saved_p to indicate that it's no longer pending
		 */
		xd->saved_p = false;

		/*
		 * For LSIs, we EOI, this will cause a resend if it's
		 * still asserted. Otherwise do an MSI retrigger.
		 */
		if (xd->flags & XIVE_IRQ_FLAG_LSI)
			xive_do_source_eoi(xd);
		else
			xive_irq_retrigger(d);

		raw_spin_unlock(&desc->lock);
	}
}

void xive_smp_disable_cpu(void)
{
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);
	unsigned int cpu = smp_processor_id();

	/* Migrate interrupts away from the CPU */
	irq_migrate_all_off_this_cpu();

	/* Set CPPR to 0 to disable flow of interrupts */
	xc->cppr = 0;
	out_8(xive_tima + xive_tima_offset + TM_CPPR, 0);

	/* Flush everything still in the queue */
	xive_flush_cpu_queue(cpu, xc);

	/* Re-enable CPPR  */
	xc->cppr = 0xff;
	out_8(xive_tima + xive_tima_offset + TM_CPPR, 0xff);
}

void xive_flush_interrupt(void)
{
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);
	unsigned int cpu = smp_processor_id();

	/* Called if an interrupt occurs while the CPU is hot unplugged */
	xive_flush_cpu_queue(cpu, xc);
}

#endif /* CONFIG_HOTPLUG_CPU */

#endif /* CONFIG_SMP */

void xive_teardown_cpu(void)
{
	struct xive_cpu *xc = __this_cpu_read(xive_cpu);
	unsigned int cpu = smp_processor_id();

	/* Set CPPR to 0 to disable flow of interrupts */
	xc->cppr = 0;
	out_8(xive_tima + xive_tima_offset + TM_CPPR, 0);

	if (xive_ops->teardown_cpu)
		xive_ops->teardown_cpu(cpu, xc);

#ifdef CONFIG_SMP
	/* Get rid of IPI */
	xive_cleanup_cpu_ipi(cpu, xc);
#endif

	/* Disable and free the queues */
	xive_cleanup_cpu_queues(cpu, xc);
}

void xive_shutdown(void)
{
	xive_ops->shutdown();
}

bool __init xive_core_init(struct device_node *np, const struct xive_ops *ops,
			   void __iomem *area, u32 offset, u8 max_prio)
{
	xive_tima = area;
	xive_tima_offset = offset;
	xive_ops = ops;
	xive_irq_priority = max_prio;

	ppc_md.get_irq = xive_get_irq;
	__xive_enabled = true;

	pr_debug("Initializing host..\n");
	xive_init_host(np);

	pr_debug("Initializing boot CPU..\n");

	/* Allocate per-CPU data and queues */
	xive_prepare_cpu(smp_processor_id());

	/* Get ready for interrupts */
	xive_setup_cpu();

	pr_info("Interrupt handling initialized with %s backend\n",
		xive_ops->name);
	pr_info("Using priority %d for all interrupts\n", max_prio);

	return true;
}

__be32 *xive_queue_page_alloc(unsigned int cpu, u32 queue_shift)
{
	unsigned int alloc_order;
	struct page *pages;
	__be32 *qpage;

	alloc_order = xive_alloc_order(queue_shift);
	pages = alloc_pages_node(cpu_to_node(cpu), GFP_KERNEL, alloc_order);
	if (!pages)
		return ERR_PTR(-ENOMEM);
	qpage = (__be32 *)page_address(pages);
	memset(qpage, 0, 1 << queue_shift);

	return qpage;
}

static int __init xive_off(char *arg)
{
	xive_cmdline_disabled = true;
	return 0;
}
__setup("xive=off", xive_off);

static int __init xive_store_eoi_cmdline(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strncmp(arg, "off", 3) == 0) {
		pr_info("StoreEOI disabled on kernel command line\n");
		xive_store_eoi = false;
	}
	return 0;
}
__setup("xive.store-eoi=", xive_store_eoi_cmdline);

#ifdef CONFIG_DEBUG_FS
static void xive_debug_show_ipi(struct seq_file *m, int cpu)
{
	struct xive_cpu *xc = per_cpu(xive_cpu, cpu);

	seq_printf(m, "CPU %d: ", cpu);
	if (xc) {
		seq_printf(m, "pp=%02x CPPR=%02x ", xc->pending_prio, xc->cppr);

#ifdef CONFIG_SMP
		{
			char buffer[128];

			xive_irq_data_dump(&xc->ipi_data, buffer, sizeof(buffer));
			seq_printf(m, "IPI=0x%08x %s", xc->hw_ipi, buffer);
		}
#endif
	}
	seq_puts(m, "\n");
}

static void xive_debug_show_irq(struct seq_file *m, struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	int rc;
	u32 target;
	u8 prio;
	u32 lirq;
	char buffer[128];

	rc = xive_ops->get_irq_config(hw_irq, &target, &prio, &lirq);
	if (rc) {
		seq_printf(m, "IRQ 0x%08x : no config rc=%d\n", hw_irq, rc);
		return;
	}

	seq_printf(m, "IRQ 0x%08x : target=0x%x prio=%02x lirq=0x%x ",
		   hw_irq, target, prio, lirq);

	xive_irq_data_dump(irq_data_get_irq_handler_data(d), buffer, sizeof(buffer));
	seq_puts(m, buffer);
	seq_puts(m, "\n");
}

static int xive_irq_debug_show(struct seq_file *m, void *private)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_data *d = irq_domain_get_irq_data(xive_irq_domain, i);

		if (d)
			xive_debug_show_irq(m, d);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(xive_irq_debug);

static int xive_ipi_debug_show(struct seq_file *m, void *private)
{
	int cpu;

	if (xive_ops->debug_show)
		xive_ops->debug_show(m, private);

	for_each_possible_cpu(cpu)
		xive_debug_show_ipi(m, cpu);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(xive_ipi_debug);

static void xive_eq_debug_show_one(struct seq_file *m, struct xive_q *q, u8 prio)
{
	int i;

	seq_printf(m, "EQ%d idx=%d T=%d\n", prio, q->idx, q->toggle);
	if (q->qpage) {
		for (i = 0; i < q->msk + 1; i++) {
			if (!(i % 8))
				seq_printf(m, "%05d ", i);
			seq_printf(m, "%08x%s", be32_to_cpup(q->qpage + i),
				   (i + 1) % 8 ? " " : "\n");
		}
	}
	seq_puts(m, "\n");
}

static int xive_eq_debug_show(struct seq_file *m, void *private)
{
	int cpu = (long)m->private;
	struct xive_cpu *xc = per_cpu(xive_cpu, cpu);

	if (xc)
		xive_eq_debug_show_one(m, &xc->queue[xive_irq_priority],
				       xive_irq_priority);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(xive_eq_debug);

static void xive_core_debugfs_create(void)
{
	struct dentry *xive_dir;
	struct dentry *xive_eq_dir;
	long cpu;
	char name[16];

	xive_dir = debugfs_create_dir("xive", arch_debugfs_dir);
	if (IS_ERR(xive_dir))
		return;

	debugfs_create_file("ipis", 0400, xive_dir,
			    NULL, &xive_ipi_debug_fops);
	debugfs_create_file("interrupts", 0400, xive_dir,
			    NULL, &xive_irq_debug_fops);
	xive_eq_dir = debugfs_create_dir("eqs", xive_dir);
	for_each_possible_cpu(cpu) {
		snprintf(name, sizeof(name), "cpu%ld", cpu);
		debugfs_create_file(name, 0400, xive_eq_dir, (void *)cpu,
				    &xive_eq_debug_fops);
	}
	debugfs_create_bool("store-eoi", 0600, xive_dir, &xive_store_eoi);

	if (xive_ops->debug_create)
		xive_ops->debug_create(xive_dir);
}
#else
static inline void xive_core_debugfs_create(void) { }
#endif /* CONFIG_DEBUG_FS */

int xive_core_debug_init(void)
{
	if (xive_enabled() && IS_ENABLED(CONFIG_DEBUG_FS))
		xive_core_debugfs_create();

	return 0;
}
