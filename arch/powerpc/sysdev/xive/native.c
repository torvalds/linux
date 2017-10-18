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
#include <linux/debugfs.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <linux/mm.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xive.h>
#include <asm/xive-regs.h>
#include <asm/opal.h>
#include <asm/kvm_ppc.h>

#include "xive-internal.h"


static u32 xive_provision_size;
static u32 *xive_provision_chips;
static u32 xive_provision_chip_count;
static u32 xive_queue_shift;
static u32 xive_pool_vps = XIVE_INVALID_VP;
static struct kmem_cache *xive_provision_cache;

int xive_native_populate_irq_data(u32 hw_irq, struct xive_irq_data *data)
{
	__be64 flags, eoi_page, trig_page;
	__be32 esb_shift, src_chip;
	u64 opal_flags;
	s64 rc;

	memset(data, 0, sizeof(*data));

	rc = opal_xive_get_irq_info(hw_irq, &flags, &eoi_page, &trig_page,
				    &esb_shift, &src_chip);
	if (rc) {
		pr_err("opal_xive_get_irq_info(0x%x) returned %lld\n",
		       hw_irq, rc);
		return -EINVAL;
	}

	opal_flags = be64_to_cpu(flags);
	if (opal_flags & OPAL_XIVE_IRQ_STORE_EOI)
		data->flags |= XIVE_IRQ_FLAG_STORE_EOI;
	if (opal_flags & OPAL_XIVE_IRQ_LSI)
		data->flags |= XIVE_IRQ_FLAG_LSI;
	if (opal_flags & OPAL_XIVE_IRQ_SHIFT_BUG)
		data->flags |= XIVE_IRQ_FLAG_SHIFT_BUG;
	if (opal_flags & OPAL_XIVE_IRQ_MASK_VIA_FW)
		data->flags |= XIVE_IRQ_FLAG_MASK_FW;
	if (opal_flags & OPAL_XIVE_IRQ_EOI_VIA_FW)
		data->flags |= XIVE_IRQ_FLAG_EOI_FW;
	data->eoi_page = be64_to_cpu(eoi_page);
	data->trig_page = be64_to_cpu(trig_page);
	data->esb_shift = be32_to_cpu(esb_shift);
	data->src_chip = be32_to_cpu(src_chip);

	data->eoi_mmio = ioremap(data->eoi_page, 1u << data->esb_shift);
	if (!data->eoi_mmio) {
		pr_err("Failed to map EOI page for irq 0x%x\n", hw_irq);
		return -ENOMEM;
	}

	if (!data->trig_page)
		return 0;
	if (data->trig_page == data->eoi_page) {
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
EXPORT_SYMBOL_GPL(xive_native_populate_irq_data);

int xive_native_configure_irq(u32 hw_irq, u32 target, u8 prio, u32 sw_irq)
{
	s64 rc;

	for (;;) {
		rc = opal_xive_set_irq_config(hw_irq, target, prio, sw_irq);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	return rc == 0 ? 0 : -ENXIO;
}
EXPORT_SYMBOL_GPL(xive_native_configure_irq);


/* This can be called multiple time to change a queue configuration */
int xive_native_configure_queue(u32 vp_id, struct xive_q *q, u8 prio,
				__be32 *qpage, u32 order, bool can_escalate)
{
	s64 rc = 0;
	__be64 qeoi_page_be;
	__be32 esc_irq_be;
	u64 flags, qpage_phys;

	/* If there's an actual queue page, clean it */
	if (order) {
		if (WARN_ON(!qpage))
			return -EINVAL;
		qpage_phys = __pa(qpage);
	} else
		qpage_phys = 0;

	/* Initialize the rest of the fields */
	q->msk = order ? ((1u << (order - 2)) - 1) : 0;
	q->idx = 0;
	q->toggle = 0;

	rc = opal_xive_get_queue_info(vp_id, prio, NULL, NULL,
				      &qeoi_page_be,
				      &esc_irq_be,
				      NULL);
	if (rc) {
		pr_err("Error %lld getting queue info prio %d\n", rc, prio);
		rc = -EIO;
		goto fail;
	}
	q->eoi_phys = be64_to_cpu(qeoi_page_be);

	/* Default flags */
	flags = OPAL_XIVE_EQ_ALWAYS_NOTIFY | OPAL_XIVE_EQ_ENABLED;

	/* Escalation needed ? */
	if (can_escalate) {
		q->esc_irq = be32_to_cpu(esc_irq_be);
		flags |= OPAL_XIVE_EQ_ESCALATE;
	}

	/* Configure and enable the queue in HW */
	for (;;) {
		rc = opal_xive_set_queue_info(vp_id, prio, qpage_phys, order, flags);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	if (rc) {
		pr_err("Error %lld setting queue for prio %d\n", rc, prio);
		rc = -EIO;
	} else {
		/*
		 * KVM code requires all of the above to be visible before
		 * q->qpage is set due to how it manages IPI EOIs
		 */
		wmb();
		q->qpage = qpage;
	}
fail:
	return rc;
}
EXPORT_SYMBOL_GPL(xive_native_configure_queue);

static void __xive_native_disable_queue(u32 vp_id, struct xive_q *q, u8 prio)
{
	s64 rc;

	/* Disable the queue in HW */
	for (;;) {
		rc = opal_xive_set_queue_info(vp_id, prio, 0, 0, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	if (rc)
		pr_err("Error %lld disabling queue for prio %d\n", rc, prio);
}

void xive_native_disable_queue(u32 vp_id, struct xive_q *q, u8 prio)
{
	__xive_native_disable_queue(vp_id, q, prio);
}
EXPORT_SYMBOL_GPL(xive_native_disable_queue);

static int xive_native_setup_queue(unsigned int cpu, struct xive_cpu *xc, u8 prio)
{
	struct xive_q *q = &xc->queue[prio];
	unsigned int alloc_order;
	struct page *pages;
	__be32 *qpage;

	alloc_order = (xive_queue_shift > PAGE_SHIFT) ?
		(xive_queue_shift - PAGE_SHIFT) : 0;
	pages = alloc_pages_node(cpu_to_node(cpu), GFP_KERNEL, alloc_order);
	if (!pages)
		return -ENOMEM;
	qpage = (__be32 *)page_address(pages);
	memset(qpage, 0, 1 << xive_queue_shift);
	return xive_native_configure_queue(get_hard_smp_processor_id(cpu),
					   q, prio, qpage, xive_queue_shift, false);
}

static void xive_native_cleanup_queue(unsigned int cpu, struct xive_cpu *xc, u8 prio)
{
	struct xive_q *q = &xc->queue[prio];
	unsigned int alloc_order;

	/*
	 * We use the variant with no iounmap as this is called on exec
	 * from an IPI and iounmap isn't safe
	 */
	__xive_native_disable_queue(get_hard_smp_processor_id(cpu), q, prio);
	alloc_order = (xive_queue_shift > PAGE_SHIFT) ?
		(xive_queue_shift - PAGE_SHIFT) : 0;
	free_pages((unsigned long)q->qpage, alloc_order);
	q->qpage = NULL;
}

static bool xive_native_match(struct device_node *node)
{
	return of_device_is_compatible(node, "ibm,opal-xive-vc");
}

#ifdef CONFIG_SMP
static int xive_native_get_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	struct device_node *np;
	unsigned int chip_id;
	s64 irq;

	/* Find the chip ID */
	np = of_get_cpu_node(cpu, NULL);
	if (np) {
		if (of_property_read_u32(np, "ibm,chip-id", &chip_id) < 0)
			chip_id = 0;
	}

	/* Allocate an IPI and populate info about it */
	for (;;) {
		irq = opal_xive_allocate_irq(chip_id);
		if (irq == OPAL_BUSY) {
			msleep(1);
			continue;
		}
		if (irq < 0) {
			pr_err("Failed to allocate IPI on CPU %d\n", cpu);
			return -ENXIO;
		}
		xc->hw_ipi = irq;
		break;
	}
	return 0;
}
#endif /* CONFIG_SMP */

u32 xive_native_alloc_irq(void)
{
	s64 rc;

	for (;;) {
		rc = opal_xive_allocate_irq(OPAL_XIVE_ANY_CHIP);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	if (rc < 0)
		return 0;
	return rc;
}
EXPORT_SYMBOL_GPL(xive_native_alloc_irq);

void xive_native_free_irq(u32 irq)
{
	for (;;) {
		s64 rc = opal_xive_free_irq(irq);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
}
EXPORT_SYMBOL_GPL(xive_native_free_irq);

#ifdef CONFIG_SMP
static void xive_native_put_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	s64 rc;

	/* Free the IPI */
	if (!xc->hw_ipi)
		return;
	for (;;) {
		rc = opal_xive_free_irq(xc->hw_ipi);
		if (rc == OPAL_BUSY) {
			msleep(1);
			continue;
		}
		xc->hw_ipi = 0;
		break;
	}
}
#endif /* CONFIG_SMP */

static void xive_native_shutdown(void)
{
	/* Switch the XIVE to emulation mode */
	opal_xive_reset(OPAL_XIVE_MODE_EMU);
}

/*
 * Perform an "ack" cycle on the current thread, thus
 * grabbing the pending active priorities and updating
 * the CPPR to the most favored one.
 */
static void xive_native_update_pending(struct xive_cpu *xc)
{
	u8 he, cppr;
	u16 ack;

	/* Perform the acknowledge hypervisor to register cycle */
	ack = be16_to_cpu(__raw_readw(xive_tima + TM_SPC_ACK_HV_REG));

	/* Synchronize subsequent queue accesses */
	mb();

	/*
	 * Grab the CPPR and the "HE" field which indicates the source
	 * of the hypervisor interrupt (if any)
	 */
	cppr = ack & 0xff;
	he = GETFIELD(TM_QW3_NSR_HE, (ack >> 8));
	switch(he) {
	case TM_QW3_NSR_HE_NONE: /* Nothing to see here */
		break;
	case TM_QW3_NSR_HE_PHYS: /* Physical thread interrupt */
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
		break;
	case TM_QW3_NSR_HE_POOL: /* HV Pool interrupt (unused) */
	case TM_QW3_NSR_HE_LSI:  /* Legacy FW LSI (unused) */
		pr_err("CPU %d got unexpected interrupt type HE=%d\n",
		       smp_processor_id(), he);
		return;
	}
}

static void xive_native_eoi(u32 hw_irq)
{
	/*
	 * Not normally used except if specific interrupts need
	 * a workaround on EOI.
	 */
	opal_int_eoi(hw_irq);
}

static void xive_native_setup_cpu(unsigned int cpu, struct xive_cpu *xc)
{
	s64 rc;
	u32 vp;
	__be64 vp_cam_be;
	u64 vp_cam;

	if (xive_pool_vps == XIVE_INVALID_VP)
		return;

	/* Enable the pool VP */
	vp = xive_pool_vps + cpu;
	pr_debug("CPU %d setting up pool VP 0x%x\n", cpu, vp);
	for (;;) {
		rc = opal_xive_set_vp_info(vp, OPAL_XIVE_VP_ENABLED, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	if (rc) {
		pr_err("Failed to enable pool VP on CPU %d\n", cpu);
		return;
	}

	/* Grab it's CAM value */
	rc = opal_xive_get_vp_info(vp, NULL, &vp_cam_be, NULL, NULL);
	if (rc) {
		pr_err("Failed to get pool VP info CPU %d\n", cpu);
		return;
	}
	vp_cam = be64_to_cpu(vp_cam_be);

	pr_debug("VP CAM = %llx\n", vp_cam);

	/* Push it on the CPU (set LSMFB to 0xff to skip backlog scan) */
	pr_debug("(Old HW value: %08x)\n",
		 in_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD2));
	out_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD0, 0xff);
	out_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD2,
		 TM_QW2W2_VP | vp_cam);
	pr_debug("(New HW value: %08x)\n",
		 in_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD2));
}

static void xive_native_teardown_cpu(unsigned int cpu, struct xive_cpu *xc)
{
	s64 rc;
	u32 vp;

	if (xive_pool_vps == XIVE_INVALID_VP)
		return;

	/* Pull the pool VP from the CPU */
	in_be64(xive_tima + TM_SPC_PULL_POOL_CTX);

	/* Disable it */
	vp = xive_pool_vps + cpu;
	for (;;) {
		rc = opal_xive_set_vp_info(vp, 0, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
}

void xive_native_sync_source(u32 hw_irq)
{
	opal_xive_sync(XIVE_SYNC_EAS, hw_irq);
}
EXPORT_SYMBOL_GPL(xive_native_sync_source);

static const struct xive_ops xive_native_ops = {
	.populate_irq_data	= xive_native_populate_irq_data,
	.configure_irq		= xive_native_configure_irq,
	.setup_queue		= xive_native_setup_queue,
	.cleanup_queue		= xive_native_cleanup_queue,
	.match			= xive_native_match,
	.shutdown		= xive_native_shutdown,
	.update_pending		= xive_native_update_pending,
	.eoi			= xive_native_eoi,
	.setup_cpu		= xive_native_setup_cpu,
	.teardown_cpu		= xive_native_teardown_cpu,
	.sync_source		= xive_native_sync_source,
#ifdef CONFIG_SMP
	.get_ipi		= xive_native_get_ipi,
	.put_ipi		= xive_native_put_ipi,
#endif /* CONFIG_SMP */
	.name			= "native",
};

static bool xive_parse_provisioning(struct device_node *np)
{
	int rc;

	if (of_property_read_u32(np, "ibm,xive-provision-page-size",
				 &xive_provision_size) < 0)
		return true;
	rc = of_property_count_elems_of_size(np, "ibm,xive-provision-chips", 4);
	if (rc < 0) {
		pr_err("Error %d getting provision chips array\n", rc);
		return false;
	}
	xive_provision_chip_count = rc;
	if (rc == 0)
		return true;

	xive_provision_chips = kzalloc(4 * xive_provision_chip_count,
				       GFP_KERNEL);
	if (WARN_ON(!xive_provision_chips))
		return false;

	rc = of_property_read_u32_array(np, "ibm,xive-provision-chips",
					xive_provision_chips,
					xive_provision_chip_count);
	if (rc < 0) {
		pr_err("Error %d reading provision chips array\n", rc);
		return false;
	}

	xive_provision_cache = kmem_cache_create("xive-provision",
						 xive_provision_size,
						 xive_provision_size,
						 0, NULL);
	if (!xive_provision_cache) {
		pr_err("Failed to allocate provision cache\n");
		return false;
	}
	return true;
}

static void xive_native_setup_pools(void)
{
	/* Allocate a pool big enough */
	pr_debug("XIVE: Allocating VP block for pool size %d\n", nr_cpu_ids);

	xive_pool_vps = xive_native_alloc_vp_block(nr_cpu_ids);
	if (WARN_ON(xive_pool_vps == XIVE_INVALID_VP))
		pr_err("XIVE: Failed to allocate pool VP, KVM might not function\n");

	pr_debug("XIVE: Pool VPs allocated at 0x%x for %d max CPUs\n",
		 xive_pool_vps, nr_cpu_ids);
}

u32 xive_native_default_eq_shift(void)
{
	return xive_queue_shift;
}
EXPORT_SYMBOL_GPL(xive_native_default_eq_shift);

bool xive_native_init(void)
{
	struct device_node *np;
	struct resource r;
	void __iomem *tima;
	struct property *prop;
	u8 max_prio = 7;
	const __be32 *p;
	u32 val, cpu;
	s64 rc;

	if (xive_cmdline_disabled)
		return false;

	pr_devel("xive_native_init()\n");
	np = of_find_compatible_node(NULL, NULL, "ibm,opal-xive-pe");
	if (!np) {
		pr_devel("not found !\n");
		return false;
	}
	pr_devel("Found %s\n", np->full_name);

	/* Resource 1 is HV window */
	if (of_address_to_resource(np, 1, &r)) {
		pr_err("Failed to get thread mgmnt area resource\n");
		return false;
	}
	tima = ioremap(r.start, resource_size(&r));
	if (!tima) {
		pr_err("Failed to map thread mgmnt area\n");
		return false;
	}

	/* Read number of priorities */
	if (of_property_read_u32(np, "ibm,xive-#priorities", &val) == 0)
		max_prio = val - 1;

	/* Iterate the EQ sizes and pick one */
	of_property_for_each_u32(np, "ibm,xive-eq-sizes", prop, p, val) {
		xive_queue_shift = val;
		if (val == PAGE_SHIFT)
			break;
	}

	/* Configure Thread Management areas for KVM */
	for_each_possible_cpu(cpu)
		kvmppc_set_xive_tima(cpu, r.start, tima);

	/* Grab size of provisionning pages */
	xive_parse_provisioning(np);

	/* Switch the XIVE to exploitation mode */
	rc = opal_xive_reset(OPAL_XIVE_MODE_EXPL);
	if (rc) {
		pr_err("Switch to exploitation mode failed with error %lld\n", rc);
		return false;
	}

	/* Setup some dummy HV pool VPs */
	xive_native_setup_pools();

	/* Initialize XIVE core with our backend */
	if (!xive_core_init(&xive_native_ops, tima, TM_QW3_HV_PHYS,
			    max_prio)) {
		opal_xive_reset(OPAL_XIVE_MODE_EMU);
		return false;
	}
	pr_info("Using %dkB queues\n", 1 << (xive_queue_shift - 10));
	return true;
}

static bool xive_native_provision_pages(void)
{
	u32 i;
	void *p;

	for (i = 0; i < xive_provision_chip_count; i++) {
		u32 chip = xive_provision_chips[i];

		/*
		 * XXX TODO: Try to make the allocation local to the node where
		 * the chip resides.
		 */
		p = kmem_cache_alloc(xive_provision_cache, GFP_KERNEL);
		if (!p) {
			pr_err("Failed to allocate provisioning page\n");
			return false;
		}
		opal_xive_donate_page(chip, __pa(p));
	}
	return true;
}

u32 xive_native_alloc_vp_block(u32 max_vcpus)
{
	s64 rc;
	u32 order;

	order = fls(max_vcpus) - 1;
	if (max_vcpus > (1 << order))
		order++;

	pr_debug("VP block alloc, for max VCPUs %d use order %d\n",
		 max_vcpus, order);

	for (;;) {
		rc = opal_xive_alloc_vp_block(order);
		switch (rc) {
		case OPAL_BUSY:
			msleep(1);
			break;
		case OPAL_XIVE_PROVISIONING:
			if (!xive_native_provision_pages())
				return XIVE_INVALID_VP;
			break;
		default:
			if (rc < 0) {
				pr_err("OPAL failed to allocate VCPUs order %d, err %lld\n",
				       order, rc);
				return XIVE_INVALID_VP;
			}
			return rc;
		}
	}
}
EXPORT_SYMBOL_GPL(xive_native_alloc_vp_block);

void xive_native_free_vp_block(u32 vp_base)
{
	s64 rc;

	if (vp_base == XIVE_INVALID_VP)
		return;

	rc = opal_xive_free_vp_block(vp_base);
	if (rc < 0)
		pr_warn("OPAL error %lld freeing VP block\n", rc);
}
EXPORT_SYMBOL_GPL(xive_native_free_vp_block);

int xive_native_enable_vp(u32 vp_id)
{
	s64 rc;

	for (;;) {
		rc = opal_xive_set_vp_info(vp_id, OPAL_XIVE_VP_ENABLED, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	return rc ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(xive_native_enable_vp);

int xive_native_disable_vp(u32 vp_id)
{
	s64 rc;

	for (;;) {
		rc = opal_xive_set_vp_info(vp_id, 0, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(1);
	}
	return rc ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(xive_native_disable_vp);

int xive_native_get_vp_info(u32 vp_id, u32 *out_cam_id, u32 *out_chip_id)
{
	__be64 vp_cam_be;
	__be32 vp_chip_id_be;
	s64 rc;

	rc = opal_xive_get_vp_info(vp_id, NULL, &vp_cam_be, NULL, &vp_chip_id_be);
	if (rc)
		return -EIO;
	*out_cam_id = be64_to_cpu(vp_cam_be) & 0xffffffffu;
	*out_chip_id = be32_to_cpu(vp_chip_id_be);

	return 0;
}
EXPORT_SYMBOL_GPL(xive_native_get_vp_info);
