/*
 * Copyright 2016,2017 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "xive: " fmt

#include <linux/types.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/mm.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xive.h>
#include <asm/xive-regs.h>
#include <asm/hvcall.h>

#include "xive-internal.h"

static u32 xive_queue_shift;

struct xive_irq_bitmap {
	unsigned long		*bitmap;
	unsigned int		base;
	unsigned int		count;
	spinlock_t		lock;
	struct list_head	list;
};

static LIST_HEAD(xive_irq_bitmaps);

static int xive_irq_bitmap_add(int base, int count)
{
	struct xive_irq_bitmap *xibm;

	xibm = kzalloc(sizeof(*xibm), GFP_ATOMIC);
	if (!xibm)
		return -ENOMEM;

	spin_lock_init(&xibm->lock);
	xibm->base = base;
	xibm->count = count;
	xibm->bitmap = kzalloc(xibm->count, GFP_KERNEL);
	list_add(&xibm->list, &xive_irq_bitmaps);

	pr_info("Using IRQ range [%x-%x]", xibm->base,
		xibm->base + xibm->count - 1);
	return 0;
}

static int __xive_irq_bitmap_alloc(struct xive_irq_bitmap *xibm)
{
	int irq;

	irq = find_first_zero_bit(xibm->bitmap, xibm->count);
	if (irq != xibm->count) {
		set_bit(irq, xibm->bitmap);
		irq += xibm->base;
	} else {
		irq = -ENOMEM;
	}

	return irq;
}

static int xive_irq_bitmap_alloc(void)
{
	struct xive_irq_bitmap *xibm;
	unsigned long flags;
	int irq = -ENOENT;

	list_for_each_entry(xibm, &xive_irq_bitmaps, list) {
		spin_lock_irqsave(&xibm->lock, flags);
		irq = __xive_irq_bitmap_alloc(xibm);
		spin_unlock_irqrestore(&xibm->lock, flags);
		if (irq >= 0)
			break;
	}
	return irq;
}

static void xive_irq_bitmap_free(int irq)
{
	unsigned long flags;
	struct xive_irq_bitmap *xibm;

	list_for_each_entry(xibm, &xive_irq_bitmaps, list) {
		if ((irq >= xibm->base) && (irq < xibm->base + xibm->count)) {
			spin_lock_irqsave(&xibm->lock, flags);
			clear_bit(irq - xibm->base, xibm->bitmap);
			spin_unlock_irqrestore(&xibm->lock, flags);
			break;
		}
	}
}

static long plpar_int_get_source_info(unsigned long flags,
				      unsigned long lisn,
				      unsigned long *src_flags,
				      unsigned long *eoi_page,
				      unsigned long *trig_page,
				      unsigned long *esb_shift)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	rc = plpar_hcall(H_INT_GET_SOURCE_INFO, retbuf, flags, lisn);
	if (rc) {
		pr_err("H_INT_GET_SOURCE_INFO lisn=%ld failed %ld\n", lisn, rc);
		return rc;
	}

	*src_flags = retbuf[0];
	*eoi_page  = retbuf[1];
	*trig_page = retbuf[2];
	*esb_shift = retbuf[3];

	pr_devel("H_INT_GET_SOURCE_INFO flags=%lx eoi=%lx trig=%lx shift=%lx\n",
		retbuf[0], retbuf[1], retbuf[2], retbuf[3]);

	return 0;
}

#define XIVE_SRC_SET_EISN (1ull << (63 - 62))
#define XIVE_SRC_MASK     (1ull << (63 - 63)) /* unused */

static long plpar_int_set_source_config(unsigned long flags,
					unsigned long lisn,
					unsigned long target,
					unsigned long prio,
					unsigned long sw_irq)
{
	long rc;


	pr_devel("H_INT_SET_SOURCE_CONFIG flags=%lx lisn=%lx target=%lx prio=%lx sw_irq=%lx\n",
		flags, lisn, target, prio, sw_irq);


	rc = plpar_hcall_norets(H_INT_SET_SOURCE_CONFIG, flags, lisn,
				target, prio, sw_irq);
	if (rc) {
		pr_err("H_INT_SET_SOURCE_CONFIG lisn=%ld target=%lx prio=%lx failed %ld\n",
		       lisn, target, prio, rc);
		return rc;
	}

	return 0;
}

static long plpar_int_get_queue_info(unsigned long flags,
				     unsigned long target,
				     unsigned long priority,
				     unsigned long *esn_page,
				     unsigned long *esn_size)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	rc = plpar_hcall(H_INT_GET_QUEUE_INFO, retbuf, flags, target, priority);
	if (rc) {
		pr_err("H_INT_GET_QUEUE_INFO cpu=%ld prio=%ld failed %ld\n",
		       target, priority, rc);
		return rc;
	}

	*esn_page = retbuf[0];
	*esn_size = retbuf[1];

	pr_devel("H_INT_GET_QUEUE_INFO page=%lx size=%lx\n",
		retbuf[0], retbuf[1]);

	return 0;
}

#define XIVE_EQ_ALWAYS_NOTIFY (1ull << (63 - 63))

static long plpar_int_set_queue_config(unsigned long flags,
				       unsigned long target,
				       unsigned long priority,
				       unsigned long qpage,
				       unsigned long qsize)
{
	long rc;

	pr_devel("H_INT_SET_QUEUE_CONFIG flags=%lx target=%lx priority=%lx qpage=%lx qsize=%lx\n",
		flags,  target, priority, qpage, qsize);

	rc = plpar_hcall_norets(H_INT_SET_QUEUE_CONFIG, flags, target,
				priority, qpage, qsize);
	if (rc) {
		pr_err("H_INT_SET_QUEUE_CONFIG cpu=%ld prio=%ld qpage=%lx returned %ld\n",
		       target, priority, qpage, rc);
		return  rc;
	}

	return 0;
}

static long plpar_int_sync(unsigned long flags, unsigned long lisn)
{
	long rc;

	rc = plpar_hcall_norets(H_INT_SYNC, flags, lisn);
	if (rc) {
		pr_err("H_INT_SYNC lisn=%ld returned %ld\n", lisn, rc);
		return  rc;
	}

	return 0;
}

#define XIVE_ESB_FLAG_STORE (1ull << (63 - 63))

static long plpar_int_esb(unsigned long flags,
			  unsigned long lisn,
			  unsigned long offset,
			  unsigned long in_data,
			  unsigned long *out_data)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	pr_devel("H_INT_ESB flags=%lx lisn=%lx offset=%lx in=%lx\n",
		flags,  lisn, offset, in_data);

	rc = plpar_hcall(H_INT_ESB, retbuf, flags, lisn, offset, in_data);
	if (rc) {
		pr_err("H_INT_ESB lisn=%ld offset=%ld returned %ld\n",
		       lisn, offset, rc);
		return  rc;
	}

	*out_data = retbuf[0];

	return 0;
}

static u64 xive_spapr_esb_rw(u32 lisn, u32 offset, u64 data, bool write)
{
	unsigned long read_data;
	long rc;

	rc = plpar_int_esb(write ? XIVE_ESB_FLAG_STORE : 0,
			   lisn, offset, data, &read_data);
	if (rc)
		return -1;

	return write ? 0 : read_data;
}

#define XIVE_SRC_H_INT_ESB     (1ull << (63 - 60))
#define XIVE_SRC_LSI           (1ull << (63 - 61))
#define XIVE_SRC_TRIGGER       (1ull << (63 - 62))
#define XIVE_SRC_STORE_EOI     (1ull << (63 - 63))

static int xive_spapr_populate_irq_data(u32 hw_irq, struct xive_irq_data *data)
{
	long rc;
	unsigned long flags;
	unsigned long eoi_page;
	unsigned long trig_page;
	unsigned long esb_shift;

	memset(data, 0, sizeof(*data));

	rc = plpar_int_get_source_info(0, hw_irq, &flags, &eoi_page, &trig_page,
				       &esb_shift);
	if (rc)
		return  -EINVAL;

	if (flags & XIVE_SRC_H_INT_ESB)
		data->flags  |= XIVE_IRQ_FLAG_H_INT_ESB;
	if (flags & XIVE_SRC_STORE_EOI)
		data->flags  |= XIVE_IRQ_FLAG_STORE_EOI;
	if (flags & XIVE_SRC_LSI)
		data->flags  |= XIVE_IRQ_FLAG_LSI;
	data->eoi_page  = eoi_page;
	data->esb_shift = esb_shift;
	data->trig_page = trig_page;

	/*
	 * No chip-id for the sPAPR backend. This has an impact how we
	 * pick a target. See xive_pick_irq_target().
	 */
	data->src_chip = XIVE_INVALID_CHIP_ID;

	data->eoi_mmio = ioremap(data->eoi_page, 1u << data->esb_shift);
	if (!data->eoi_mmio) {
		pr_err("Failed to map EOI page for irq 0x%x\n", hw_irq);
		return -ENOMEM;
	}

	data->hw_irq = hw_irq;

	/* Full function page supports trigger */
	if (flags & XIVE_SRC_TRIGGER) {
		data->trig_mmio = data->eoi_mmio;
		return 0;
	}

	data->trig_mmio = ioremap(data->trig_page, 1u << data->esb_shift);
	if (!data->trig_mmio) {
		pr_err("Failed to map trigger page for irq 0x%x\n", hw_irq);
		return -ENOMEM;
	}
	return 0;
}

static int xive_spapr_configure_irq(u32 hw_irq, u32 target, u8 prio, u32 sw_irq)
{
	long rc;

	rc = plpar_int_set_source_config(XIVE_SRC_SET_EISN, hw_irq, target,
					 prio, sw_irq);

	return rc == 0 ? 0 : -ENXIO;
}

/* This can be called multiple time to change a queue configuration */
static int xive_spapr_configure_queue(u32 target, struct xive_q *q, u8 prio,
				   __be32 *qpage, u32 order)
{
	s64 rc = 0;
	unsigned long esn_page;
	unsigned long esn_size;
	u64 flags, qpage_phys;

	/* If there's an actual queue page, clean it */
	if (order) {
		if (WARN_ON(!qpage))
			return -EINVAL;
		qpage_phys = __pa(qpage);
	} else {
		qpage_phys = 0;
	}

	/* Initialize the rest of the fields */
	q->msk = order ? ((1u << (order - 2)) - 1) : 0;
	q->idx = 0;
	q->toggle = 0;

	rc = plpar_int_get_queue_info(0, target, prio, &esn_page, &esn_size);
	if (rc) {
		pr_err("Error %lld getting queue info prio %d\n", rc, prio);
		rc = -EIO;
		goto fail;
	}

	/* TODO: add support for the notification page */
	q->eoi_phys = esn_page;

	/* Default is to always notify */
	flags = XIVE_EQ_ALWAYS_NOTIFY;

	/* Configure and enable the queue in HW */
	rc = plpar_int_set_queue_config(flags, target, prio, qpage_phys, order);
	if (rc) {
		pr_err("Error %lld setting queue for prio %d\n", rc, prio);
		rc = -EIO;
	} else {
		q->qpage = qpage;
	}
fail:
	return rc;
}

static int xive_spapr_setup_queue(unsigned int cpu, struct xive_cpu *xc,
				  u8 prio)
{
	struct xive_q *q = &xc->queue[prio];
	__be32 *qpage;

	qpage = xive_queue_page_alloc(cpu, xive_queue_shift);
	if (IS_ERR(qpage))
		return PTR_ERR(qpage);

	return xive_spapr_configure_queue(cpu, q, prio, qpage,
					  xive_queue_shift);
}

static void xive_spapr_cleanup_queue(unsigned int cpu, struct xive_cpu *xc,
				  u8 prio)
{
	struct xive_q *q = &xc->queue[prio];
	unsigned int alloc_order;
	long rc;

	rc = plpar_int_set_queue_config(0, cpu, prio, 0, 0);
	if (rc)
		pr_err("Error %ld setting queue for prio %d\n", rc, prio);

	alloc_order = xive_alloc_order(xive_queue_shift);
	free_pages((unsigned long)q->qpage, alloc_order);
	q->qpage = NULL;
}

static bool xive_spapr_match(struct device_node *node)
{
	/* Ignore cascaded controllers for the moment */
	return 1;
}

#ifdef CONFIG_SMP
static int xive_spapr_get_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	int irq = xive_irq_bitmap_alloc();

	if (irq < 0) {
		pr_err("Failed to allocate IPI on CPU %d\n", cpu);
		return -ENXIO;
	}

	xc->hw_ipi = irq;
	return 0;
}

static void xive_spapr_put_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	xive_irq_bitmap_free(xc->hw_ipi);
}
#endif /* CONFIG_SMP */

static void xive_spapr_shutdown(void)
{
	long rc;

	rc = plpar_hcall_norets(H_INT_RESET, 0);
	if (rc)
		pr_err("H_INT_RESET failed %ld\n", rc);
}

/*
 * Perform an "ack" cycle on the current thread. Grab the pending
 * active priorities and update the CPPR to the most favored one.
 */
static void xive_spapr_update_pending(struct xive_cpu *xc)
{
	u8 nsr, cppr;
	u16 ack;

	/*
	 * Perform the "Acknowledge O/S to Register" cycle.
	 *
	 * Let's speedup the access to the TIMA using the raw I/O
	 * accessor as we don't need the synchronisation routine of
	 * the higher level ones
	 */
	ack = be16_to_cpu(__raw_readw(xive_tima + TM_SPC_ACK_OS_REG));

	/* Synchronize subsequent queue accesses */
	mb();

	/*
	 * Grab the CPPR and the "NSR" field which indicates the source
	 * of the interrupt (if any)
	 */
	cppr = ack & 0xff;
	nsr = ack >> 8;

	if (nsr & TM_QW1_NSR_EO) {
		if (cppr == 0xff)
			return;
		/* Mark the priority pending */
		xc->pending_prio |= 1 << cppr;

		/*
		 * A new interrupt should never have a CPPR less favored
		 * than our current one.
		 */
		if (cppr >= xc->cppr)
			pr_err("CPU %d odd ack CPPR, got %d at %d\n",
			       smp_processor_id(), cppr, xc->cppr);

		/* Update our idea of what the CPPR is */
		xc->cppr = cppr;
	}
}

static void xive_spapr_eoi(u32 hw_irq)
{
	/* Not used */;
}

static void xive_spapr_setup_cpu(unsigned int cpu, struct xive_cpu *xc)
{
	/* Only some debug on the TIMA settings */
	pr_debug("(HW value: %08x %08x %08x)\n",
		 in_be32(xive_tima + TM_QW1_OS + TM_WORD0),
		 in_be32(xive_tima + TM_QW1_OS + TM_WORD1),
		 in_be32(xive_tima + TM_QW1_OS + TM_WORD2));
}

static void xive_spapr_teardown_cpu(unsigned int cpu, struct xive_cpu *xc)
{
	/* Nothing to do */;
}

static void xive_spapr_sync_source(u32 hw_irq)
{
	/* Specs are unclear on what this is doing */
	plpar_int_sync(0, hw_irq);
}

static const struct xive_ops xive_spapr_ops = {
	.populate_irq_data	= xive_spapr_populate_irq_data,
	.configure_irq		= xive_spapr_configure_irq,
	.setup_queue		= xive_spapr_setup_queue,
	.cleanup_queue		= xive_spapr_cleanup_queue,
	.match			= xive_spapr_match,
	.shutdown		= xive_spapr_shutdown,
	.update_pending		= xive_spapr_update_pending,
	.eoi			= xive_spapr_eoi,
	.setup_cpu		= xive_spapr_setup_cpu,
	.teardown_cpu		= xive_spapr_teardown_cpu,
	.sync_source		= xive_spapr_sync_source,
	.esb_rw			= xive_spapr_esb_rw,
#ifdef CONFIG_SMP
	.get_ipi		= xive_spapr_get_ipi,
	.put_ipi		= xive_spapr_put_ipi,
#endif /* CONFIG_SMP */
	.name			= "spapr",
};

/*
 * get max priority from "/ibm,plat-res-int-priorities"
 */
static bool xive_get_max_prio(u8 *max_prio)
{
	struct device_node *rootdn;
	const __be32 *reg;
	u32 len;
	int prio, found;

	rootdn = of_find_node_by_path("/");
	if (!rootdn) {
		pr_err("not root node found !\n");
		return false;
	}

	reg = of_get_property(rootdn, "ibm,plat-res-int-priorities", &len);
	if (!reg) {
		pr_err("Failed to read 'ibm,plat-res-int-priorities' property\n");
		return false;
	}

	if (len % (2 * sizeof(u32)) != 0) {
		pr_err("invalid 'ibm,plat-res-int-priorities' property\n");
		return false;
	}

	/* HW supports priorities in the range [0-7] and 0xFF is a
	 * wildcard priority used to mask. We scan the ranges reserved
	 * by the hypervisor to find the lowest priority we can use.
	 */
	found = 0xFF;
	for (prio = 0; prio < 8; prio++) {
		int reserved = 0;
		int i;

		for (i = 0; i < len / (2 * sizeof(u32)); i++) {
			int base  = be32_to_cpu(reg[2 * i]);
			int range = be32_to_cpu(reg[2 * i + 1]);

			if (prio >= base && prio < base + range)
				reserved++;
		}

		if (!reserved)
			found = prio;
	}

	if (found == 0xFF) {
		pr_err("no valid priority found in 'ibm,plat-res-int-priorities'\n");
		return false;
	}

	*max_prio = found;
	return true;
}

bool __init xive_spapr_init(void)
{
	struct device_node *np;
	struct resource r;
	void __iomem *tima;
	struct property *prop;
	u8 max_prio;
	u32 val;
	u32 len;
	const __be32 *reg;
	int i;

	if (xive_cmdline_disabled)
		return false;

	pr_devel("%s()\n", __func__);
	np = of_find_compatible_node(NULL, NULL, "ibm,power-ivpe");
	if (!np) {
		pr_devel("not found !\n");
		return false;
	}
	pr_devel("Found %s\n", np->full_name);

	/* Resource 1 is the OS ring TIMA */
	if (of_address_to_resource(np, 1, &r)) {
		pr_err("Failed to get thread mgmnt area resource\n");
		return false;
	}
	tima = ioremap(r.start, resource_size(&r));
	if (!tima) {
		pr_err("Failed to map thread mgmnt area\n");
		return false;
	}

	if (!xive_get_max_prio(&max_prio))
		return false;

	/* Feed the IRQ number allocator with the ranges given in the DT */
	reg = of_get_property(np, "ibm,xive-lisn-ranges", &len);
	if (!reg) {
		pr_err("Failed to read 'ibm,xive-lisn-ranges' property\n");
		return false;
	}

	if (len % (2 * sizeof(u32)) != 0) {
		pr_err("invalid 'ibm,xive-lisn-ranges' property\n");
		return false;
	}

	for (i = 0; i < len / (2 * sizeof(u32)); i++, reg += 2)
		xive_irq_bitmap_add(be32_to_cpu(reg[0]),
				    be32_to_cpu(reg[1]));

	/* Iterate the EQ sizes and pick one */
	of_property_for_each_u32(np, "ibm,xive-eq-sizes", prop, reg, val) {
		xive_queue_shift = val;
		if (val == PAGE_SHIFT)
			break;
	}

	/* Initialize XIVE core with our backend */
	if (!xive_core_init(&xive_spapr_ops, tima, TM_QW1_OS, max_prio))
		return false;

	pr_info("Using %dkB queues\n", 1 << (xive_queue_shift - 10));
	return true;
}
