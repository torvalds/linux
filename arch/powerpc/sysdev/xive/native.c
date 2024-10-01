// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016,2017 IBM Corporation.
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
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <linux/mm.h>
#include <linux/kmemleak.h>

#include <asm/machdep.h>
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
static bool xive_has_single_esc;
bool xive_has_save_restore;

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
	if (opal_flags & OPAL_XIVE_IRQ_STORE_EOI2)
		data->flags |= XIVE_IRQ_FLAG_STORE_EOI;
	if (opal_flags & OPAL_XIVE_IRQ_LSI)
		data->flags |= XIVE_IRQ_FLAG_LSI;
	data->eoi_page = be64_to_cpu(eoi_page);
	data->trig_page = be64_to_cpu(trig_page);
	data->esb_shift = be32_to_cpu(esb_shift);
	data->src_chip = be32_to_cpu(src_chip);

	data->eoi_mmio = ioremap(data->eoi_page, 1u << data->esb_shift);
	if (!data->eoi_mmio) {
		pr_err("Failed to map EOI page for irq 0x%x\n", hw_irq);
		return -ENOMEM;
	}

	data->hw_irq = hw_irq;

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
		msleep(OPAL_BUSY_DELAY_MS);
	}
	return rc == 0 ? 0 : -ENXIO;
}
EXPORT_SYMBOL_GPL(xive_native_configure_irq);

static int xive_native_get_irq_config(u32 hw_irq, u32 *target, u8 *prio,
				      u32 *sw_irq)
{
	s64 rc;
	__be64 vp;
	__be32 lirq;

	rc = opal_xive_get_irq_config(hw_irq, &vp, prio, &lirq);

	*target = be64_to_cpu(vp);
	*sw_irq = be32_to_cpu(lirq);

	return rc == 0 ? 0 : -ENXIO;
}

#define vp_err(vp, fmt, ...) pr_err("VP[0x%x]: " fmt, vp, ##__VA_ARGS__)

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
		vp_err(vp_id, "Failed to get queue %d info : %lld\n", prio, rc);
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
		msleep(OPAL_BUSY_DELAY_MS);
	}
	if (rc) {
		vp_err(vp_id, "Failed to set queue %d info: %lld\n", prio, rc);
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
		msleep(OPAL_BUSY_DELAY_MS);
	}
	if (rc)
		vp_err(vp_id, "Failed to disable queue %d : %lld\n", prio, rc);
}

void xive_native_disable_queue(u32 vp_id, struct xive_q *q, u8 prio)
{
	__xive_native_disable_queue(vp_id, q, prio);
}
EXPORT_SYMBOL_GPL(xive_native_disable_queue);

static int xive_native_setup_queue(unsigned int cpu, struct xive_cpu *xc, u8 prio)
{
	struct xive_q *q = &xc->queue[prio];
	__be32 *qpage;

	qpage = xive_queue_page_alloc(cpu, xive_queue_shift);
	if (IS_ERR(qpage))
		return PTR_ERR(qpage);

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
	alloc_order = xive_alloc_order(xive_queue_shift);
	free_pages((unsigned long)q->qpage, alloc_order);
	q->qpage = NULL;
}

static bool xive_native_match(struct device_node *node)
{
	return of_device_is_compatible(node, "ibm,opal-xive-vc");
}

static s64 opal_xive_allocate_irq(u32 chip_id)
{
	s64 irq = opal_xive_allocate_irq_raw(chip_id);

	/*
	 * Old versions of skiboot can incorrectly return 0xffffffff to
	 * indicate no space, fix it up here.
	 */
	return irq == 0xffffffff ? OPAL_RESOURCE : irq;
}

#ifdef CONFIG_SMP
static int xive_native_get_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	s64 irq;

	/* Allocate an IPI and populate info about it */
	for (;;) {
		irq = opal_xive_allocate_irq(xc->chip_id);
		if (irq == OPAL_BUSY) {
			msleep(OPAL_BUSY_DELAY_MS);
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

u32 xive_native_alloc_irq_on_chip(u32 chip_id)
{
	s64 rc;

	for (;;) {
		rc = opal_xive_allocate_irq(chip_id);
		if (rc != OPAL_BUSY)
			break;
		msleep(OPAL_BUSY_DELAY_MS);
	}
	if (rc < 0)
		return 0;
	return rc;
}
EXPORT_SYMBOL_GPL(xive_native_alloc_irq_on_chip);

void xive_native_free_irq(u32 irq)
{
	for (;;) {
		s64 rc = opal_xive_free_irq(irq);
		if (rc != OPAL_BUSY)
			break;
		msleep(OPAL_BUSY_DELAY_MS);
	}
}
EXPORT_SYMBOL_GPL(xive_native_free_irq);

#ifdef CONFIG_SMP
static void xive_native_put_ipi(unsigned int cpu, struct xive_cpu *xc)
{
	s64 rc;

	/* Free the IPI */
	if (xc->hw_ipi == XIVE_BAD_IRQ)
		return;
	for (;;) {
		rc = opal_xive_free_irq(xc->hw_ipi);
		if (rc == OPAL_BUSY) {
			msleep(OPAL_BUSY_DELAY_MS);
			continue;
		}
		xc->hw_ipi = XIVE_BAD_IRQ;
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
	he = (ack >> 8) >> 6;
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

static void xive_native_prepare_cpu(unsigned int cpu, struct xive_cpu *xc)
{
	xc->chip_id = cpu_to_chip_id(cpu);
}

static void xive_native_setup_cpu(unsigned int cpu, struct xive_cpu *xc)
{
	s64 rc;
	u32 vp;
	__be64 vp_cam_be;
	u64 vp_cam;

	if (xive_pool_vps == XIVE_INVALID_VP)
		return;

	/* Check if pool VP already active, if it is, pull it */
	if (in_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD2) & TM_QW2W2_VP)
		in_be64(xive_tima + TM_SPC_PULL_POOL_CTX);

	/* Enable the pool VP */
	vp = xive_pool_vps + cpu;
	for (;;) {
		rc = opal_xive_set_vp_info(vp, OPAL_XIVE_VP_ENABLED, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(OPAL_BUSY_DELAY_MS);
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

	/* Push it on the CPU (set LSMFB to 0xff to skip backlog scan) */
	out_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD0, 0xff);
	out_be32(xive_tima + TM_QW2_HV_POOL + TM_WORD2, TM_QW2W2_VP | vp_cam);
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
		msleep(OPAL_BUSY_DELAY_MS);
	}
}

void xive_native_sync_source(u32 hw_irq)
{
	opal_xive_sync(XIVE_SYNC_EAS, hw_irq);
}
EXPORT_SYMBOL_GPL(xive_native_sync_source);

void xive_native_sync_queue(u32 hw_irq)
{
	opal_xive_sync(XIVE_SYNC_QUEUE, hw_irq);
}
EXPORT_SYMBOL_GPL(xive_native_sync_queue);

#ifdef CONFIG_DEBUG_FS
static int xive_native_debug_create(struct dentry *xive_dir)
{
	debugfs_create_bool("save-restore", 0600, xive_dir, &xive_has_save_restore);
	return 0;
}
#endif

static const struct xive_ops xive_native_ops = {
	.populate_irq_data	= xive_native_populate_irq_data,
	.configure_irq		= xive_native_configure_irq,
	.get_irq_config		= xive_native_get_irq_config,
	.setup_queue		= xive_native_setup_queue,
	.cleanup_queue		= xive_native_cleanup_queue,
	.match			= xive_native_match,
	.shutdown		= xive_native_shutdown,
	.update_pending		= xive_native_update_pending,
	.prepare_cpu		= xive_native_prepare_cpu,
	.setup_cpu		= xive_native_setup_cpu,
	.teardown_cpu		= xive_native_teardown_cpu,
	.sync_source		= xive_native_sync_source,
#ifdef CONFIG_SMP
	.get_ipi		= xive_native_get_ipi,
	.put_ipi		= xive_native_put_ipi,
#endif /* CONFIG_SMP */
#ifdef CONFIG_DEBUG_FS
	.debug_create		= xive_native_debug_create,
#endif /* CONFIG_DEBUG_FS */
	.name			= "native",
};

static bool __init xive_parse_provisioning(struct device_node *np)
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

	xive_provision_chips = kcalloc(4, xive_provision_chip_count,
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

static void __init xive_native_setup_pools(void)
{
	/* Allocate a pool big enough */
	pr_debug("XIVE: Allocating VP block for pool size %u\n", nr_cpu_ids);

	xive_pool_vps = xive_native_alloc_vp_block(nr_cpu_ids);
	if (WARN_ON(xive_pool_vps == XIVE_INVALID_VP))
		pr_err("XIVE: Failed to allocate pool VP, KVM might not function\n");

	pr_debug("XIVE: Pool VPs allocated at 0x%x for %u max CPUs\n",
		 xive_pool_vps, nr_cpu_ids);
}

u32 xive_native_default_eq_shift(void)
{
	return xive_queue_shift;
}
EXPORT_SYMBOL_GPL(xive_native_default_eq_shift);

unsigned long xive_tima_os;
EXPORT_SYMBOL_GPL(xive_tima_os);

bool __init xive_native_init(void)
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
	pr_devel("Found %pOF\n", np);

	/* Resource 1 is HV window */
	if (of_address_to_resource(np, 1, &r)) {
		pr_err("Failed to get thread mgmnt area resource\n");
		goto err_put;
	}
	tima = ioremap(r.start, resource_size(&r));
	if (!tima) {
		pr_err("Failed to map thread mgmnt area\n");
		goto err_put;
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

	/* Do we support single escalation */
	if (of_get_property(np, "single-escalation-support", NULL) != NULL)
		xive_has_single_esc = true;

	if (of_get_property(np, "vp-save-restore", NULL))
		xive_has_save_restore = true;

	/* Configure Thread Management areas for KVM */
	for_each_possible_cpu(cpu)
		kvmppc_set_xive_tima(cpu, r.start, tima);

	/* Resource 2 is OS window */
	if (of_address_to_resource(np, 2, &r)) {
		pr_err("Failed to get thread mgmnt area resource\n");
		goto err_put;
	}

	xive_tima_os = r.start;

	/* Grab size of provisioning pages */
	xive_parse_provisioning(np);

	/* Switch the XIVE to exploitation mode */
	rc = opal_xive_reset(OPAL_XIVE_MODE_EXPL);
	if (rc) {
		pr_err("Switch to exploitation mode failed with error %lld\n", rc);
		goto err_put;
	}

	/* Setup some dummy HV pool VPs */
	xive_native_setup_pools();

	/* Initialize XIVE core with our backend */
	if (!xive_core_init(np, &xive_native_ops, tima, TM_QW3_HV_PHYS,
			    max_prio)) {
		opal_xive_reset(OPAL_XIVE_MODE_EMU);
		goto err_put;
	}
	of_node_put(np);
	pr_info("Using %dkB queues\n", 1 << (xive_queue_shift - 10));
	return true;

err_put:
	of_node_put(np);
	return false;
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
		kmemleak_ignore(p);
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
			msleep(OPAL_BUSY_DELAY_MS);
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

int xive_native_enable_vp(u32 vp_id, bool single_escalation)
{
	s64 rc;
	u64 flags = OPAL_XIVE_VP_ENABLED;

	if (single_escalation)
		flags |= OPAL_XIVE_VP_SINGLE_ESCALATION;
	for (;;) {
		rc = opal_xive_set_vp_info(vp_id, flags, 0);
		if (rc != OPAL_BUSY)
			break;
		msleep(OPAL_BUSY_DELAY_MS);
	}
	if (rc)
		vp_err(vp_id, "Failed to enable VP : %lld\n", rc);
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
		msleep(OPAL_BUSY_DELAY_MS);
	}
	if (rc)
		vp_err(vp_id, "Failed to disable VP : %lld\n", rc);
	return rc ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(xive_native_disable_vp);

int xive_native_get_vp_info(u32 vp_id, u32 *out_cam_id, u32 *out_chip_id)
{
	__be64 vp_cam_be;
	__be32 vp_chip_id_be;
	s64 rc;

	rc = opal_xive_get_vp_info(vp_id, NULL, &vp_cam_be, NULL, &vp_chip_id_be);
	if (rc) {
		vp_err(vp_id, "Failed to get VP info : %lld\n", rc);
		return -EIO;
	}
	*out_cam_id = be64_to_cpu(vp_cam_be) & 0xffffffffu;
	*out_chip_id = be32_to_cpu(vp_chip_id_be);

	return 0;
}
EXPORT_SYMBOL_GPL(xive_native_get_vp_info);

bool xive_native_has_single_escalation(void)
{
	return xive_has_single_esc;
}
EXPORT_SYMBOL_GPL(xive_native_has_single_escalation);

bool xive_native_has_save_restore(void)
{
	return xive_has_save_restore;
}
EXPORT_SYMBOL_GPL(xive_native_has_save_restore);

int xive_native_get_queue_info(u32 vp_id, u32 prio,
			       u64 *out_qpage,
			       u64 *out_qsize,
			       u64 *out_qeoi_page,
			       u32 *out_escalate_irq,
			       u64 *out_qflags)
{
	__be64 qpage;
	__be64 qsize;
	__be64 qeoi_page;
	__be32 escalate_irq;
	__be64 qflags;
	s64 rc;

	rc = opal_xive_get_queue_info(vp_id, prio, &qpage, &qsize,
				      &qeoi_page, &escalate_irq, &qflags);
	if (rc) {
		vp_err(vp_id, "failed to get queue %d info : %lld\n", prio, rc);
		return -EIO;
	}

	if (out_qpage)
		*out_qpage = be64_to_cpu(qpage);
	if (out_qsize)
		*out_qsize = be64_to_cpu(qsize);
	if (out_qeoi_page)
		*out_qeoi_page = be64_to_cpu(qeoi_page);
	if (out_escalate_irq)
		*out_escalate_irq = be32_to_cpu(escalate_irq);
	if (out_qflags)
		*out_qflags = be64_to_cpu(qflags);

	return 0;
}
EXPORT_SYMBOL_GPL(xive_native_get_queue_info);

int xive_native_get_queue_state(u32 vp_id, u32 prio, u32 *qtoggle, u32 *qindex)
{
	__be32 opal_qtoggle;
	__be32 opal_qindex;
	s64 rc;

	rc = opal_xive_get_queue_state(vp_id, prio, &opal_qtoggle,
				       &opal_qindex);
	if (rc) {
		vp_err(vp_id, "failed to get queue %d state : %lld\n", prio, rc);
		return -EIO;
	}

	if (qtoggle)
		*qtoggle = be32_to_cpu(opal_qtoggle);
	if (qindex)
		*qindex = be32_to_cpu(opal_qindex);

	return 0;
}
EXPORT_SYMBOL_GPL(xive_native_get_queue_state);

int xive_native_set_queue_state(u32 vp_id, u32 prio, u32 qtoggle, u32 qindex)
{
	s64 rc;

	rc = opal_xive_set_queue_state(vp_id, prio, qtoggle, qindex);
	if (rc) {
		vp_err(vp_id, "failed to set queue %d state : %lld\n", prio, rc);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xive_native_set_queue_state);

bool xive_native_has_queue_state_support(void)
{
	return opal_check_token(OPAL_XIVE_GET_QUEUE_STATE) &&
		opal_check_token(OPAL_XIVE_SET_QUEUE_STATE);
}
EXPORT_SYMBOL_GPL(xive_native_has_queue_state_support);

int xive_native_get_vp_state(u32 vp_id, u64 *out_state)
{
	__be64 state;
	s64 rc;

	rc = opal_xive_get_vp_state(vp_id, &state);
	if (rc) {
		vp_err(vp_id, "failed to get vp state : %lld\n", rc);
		return -EIO;
	}

	if (out_state)
		*out_state = be64_to_cpu(state);
	return 0;
}
EXPORT_SYMBOL_GPL(xive_native_get_vp_state);

machine_arch_initcall(powernv, xive_core_debug_init);
