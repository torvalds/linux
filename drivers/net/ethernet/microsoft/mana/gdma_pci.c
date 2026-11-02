// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2025, Microsoft Corporation. */

#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/topology.h>
#include <linux/xarray.h>

#include <net/mana/mana.h>
#include <net/mana/hw_channel.h>

struct mana_dev_recovery {
	struct list_head list;
	struct pci_dev *pdev;
	enum gdma_eqe_type type;
};

static struct mana_dev_recovery_work {
	struct list_head dev_list;
	struct delayed_work work;

	/* Lock for dev_list above */
	spinlock_t lock;
} mana_dev_recovery_work;

static int mana_gd_suspend(struct pci_dev *pdev, pm_message_t state);
static int mana_gd_resume(struct pci_dev *pdev);

#define MANA_SERVICE_PERIOD 10

static void mana_serv_rescan(struct pci_dev *pdev)
{
	struct pci_bus *parent;

	pci_lock_rescan_remove();

	parent = pdev->bus;
	if (!parent) {
		dev_err(&pdev->dev, "MANA service: no parent bus\n");
		goto out;
	}

	pci_stop_and_remove_bus_device(pdev);
	pci_rescan_bus(parent);

out:
	pci_unlock_rescan_remove();
}

static void mana_serv_fpga(struct pci_dev *pdev)
{
	struct pci_bus *bus, *parent;

	pci_lock_rescan_remove();

	bus = pdev->bus;
	if (!bus) {
		dev_err(&pdev->dev, "MANA service: no bus\n");
		goto out;
	}

	parent = bus->parent;
	if (!parent) {
		dev_err(&pdev->dev, "MANA service: no parent bus\n");
		goto out;
	}

	pci_stop_and_remove_bus_device(bus->self);

	msleep(MANA_SERVICE_PERIOD * 1000);

	pci_rescan_bus(parent);

out:
	pci_unlock_rescan_remove();
}

static void mana_serv_reset(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct hw_channel_context *hwc;
	int ret;

	if (!gc) {
		/* Perform PCI rescan on device if GC is not set up */
		dev_err(&pdev->dev, "MANA service: GC not setup, rescanning\n");
		mana_serv_rescan(pdev);
		return;
	}

	hwc = gc->hwc.driver_data;
	if (!hwc) {
		dev_err(&pdev->dev, "MANA service: no HWC\n");
		goto out;
	}

	/* HWC is not responding in this case, so don't wait */
	hwc->hwc_timeout = 0;

	dev_info(&pdev->dev, "MANA reset cycle start\n");

	mana_gd_suspend(pdev, PMSG_SUSPEND);

	msleep(MANA_SERVICE_PERIOD * 1000);

	ret = mana_gd_resume(pdev);
	if (ret == -ETIMEDOUT || ret == -EPROTO) {
		/* Perform PCI rescan on device if we failed on HWC */
		dev_err(&pdev->dev, "MANA service: resume failed, rescanning\n");
		mana_serv_rescan(pdev);
		return;
	}

	if (ret)
		dev_info(&pdev->dev, "MANA reset cycle failed err %d\n", ret);
	else
		dev_info(&pdev->dev, "MANA reset cycle completed\n");

out:
	clear_bit(GC_IN_SERVICE, &gc->flags);
}

static void mana_do_service(enum gdma_eqe_type type, struct pci_dev *pdev)
{
	switch (type) {
	case GDMA_EQE_HWC_FPGA_RECONFIG:
		mana_serv_fpga(pdev);
		break;

	case GDMA_EQE_HWC_RESET_REQUEST:
		mana_serv_reset(pdev);
		break;

	default:
		dev_err(&pdev->dev, "MANA service: unknown type %d\n", type);
		break;
	}
}

static void mana_recovery_delayed_func(struct work_struct *w)
{
	struct mana_dev_recovery_work *work;
	struct mana_dev_recovery *dev;
	unsigned long flags;

	work = container_of(w, struct mana_dev_recovery_work, work.work);

	spin_lock_irqsave(&work->lock, flags);

	while (!list_empty(&work->dev_list)) {
		dev = list_first_entry(&work->dev_list,
				       struct mana_dev_recovery, list);
		list_del(&dev->list);
		spin_unlock_irqrestore(&work->lock, flags);

		mana_do_service(dev->type, dev->pdev);
		pci_dev_put(dev->pdev);
		kfree(dev);

		spin_lock_irqsave(&work->lock, flags);
	}

	spin_unlock_irqrestore(&work->lock, flags);
}

static void mana_serv_func(struct work_struct *w)
{
	struct mana_serv_work *mns_wk;
	struct pci_dev *pdev;

	mns_wk = container_of(w, struct mana_serv_work, serv_work);
	pdev = mns_wk->pdev;

	if (pdev)
		mana_do_service(mns_wk->type, pdev);

	pci_dev_put(pdev);
	kfree(mns_wk);
	module_put(THIS_MODULE);
}

static int mana_pci_schedule_serv_work(struct gdma_context *gc,
				       enum gdma_eqe_type type)
{
	struct mana_serv_work *mns_wk;

	if (test_and_set_bit(GC_IN_SERVICE, &gc->flags)) {
		dev_info(gc->dev, "Already in service\n");
		return -EBUSY;
	}

	if (!try_module_get(THIS_MODULE)) {
		dev_info(gc->dev, "Module is unloading\n");
		clear_bit(GC_IN_SERVICE, &gc->flags);
		return -ENODEV;
	}

	mns_wk = kzalloc(sizeof(*mns_wk), GFP_ATOMIC);
	if (!mns_wk) {
		module_put(THIS_MODULE);
		clear_bit(GC_IN_SERVICE, &gc->flags);
		return -ENOMEM;
	}

	dev_info(gc->dev, "Start MANA service type:%d\n", type);
	mns_wk->pdev = to_pci_dev(gc->dev);
	mns_wk->type = type;
	pci_dev_get(mns_wk->pdev);
	INIT_WORK(&mns_wk->serv_work, mana_serv_func);
	schedule_work(&mns_wk->serv_work);
	return 0;
}

static bool mana_pci_msix_can_alloc_dyn(struct gdma_context *gc)
{
	return pci_msix_can_alloc_dyn(to_pci_dev(gc->dev));
}

static int mana_pci_msix_virq(struct gdma_context *gc, int msi)
{
	return pci_irq_vector(to_pci_dev(gc->dev), msi);
}

static int mana_pci_msix_alloc_at(struct gdma_context *gc, int *msi)
{
	struct msi_map irq_map;

	irq_map = pci_msix_alloc_irq_at(to_pci_dev(gc->dev), *msi, NULL);
	if (!irq_map.virq)
		return irq_map.index;

	*msi = irq_map.index;
	return irq_map.virq;
}

static void mana_pci_msix_free(struct gdma_context *gc, int msi, int irq)
{
	struct msi_map irq_map = { .virq = irq, .index = msi };

	pci_msix_free_irq(to_pci_dev(gc->dev), irq_map);
}

static int mana_pci_msix_vec_count(struct gdma_context *gc)
{
	return pci_msix_vec_count(to_pci_dev(gc->dev));
}

static int mana_pci_dev_reset(struct gdma_context *gc)
{
	return pcie_flr(to_pci_dev(gc->dev));
}

/*
 * Spread on CPUs with the following heuristics:
 *
 * 1. No more than one IRQ per CPU, if possible;
 * 2. NUMA locality is the second priority;
 * 3. Sibling dislocality is the last priority.
 *
 * Let's consider this topology:
 *
 * Node            0               1
 * Core        0       1       2       3
 * CPU       0   1   2   3   4   5   6   7
 *
 * The most performant IRQ distribution based on the above topology
 * and heuristics may look like this:
 *
 * IRQ     Nodes   Cores   CPUs
 * 0       1       0       0-1
 * 1       1       1       2-3
 * 2       1       0       0-1
 * 3       1       1       2-3
 * 4       2       2       4-5
 * 5       2       3       6-7
 * 6       2       2       4-5
 * 7       2       3       6-7
 *
 * The heuristics is implemented as follows.
 *
 * The outer for_each() loop resets the 'weight' to the actual number
 * of CPUs in the hop. Then inner for_each() loop decrements it by the
 * number of sibling groups (cores) while assigning first set of IRQs
 * to each group. IRQs 0 and 1 above are distributed this way.
 *
 * Now, because NUMA locality is more important, we should walk the
 * same set of siblings and assign 2nd set of IRQs (2 and 3), and it's
 * implemented by the medium while() loop. We do like this unless the
 * number of IRQs assigned on this hop will not become equal to number
 * of CPUs in the hop (weight == 0). Then we switch to the next hop and
 * do the same thing.
 */

static int mana_irq_setup_numa_aware(unsigned int *irqs, unsigned int len,
				     int node, bool skip_first_cpu)
{
	const struct cpumask *next, *prev = cpu_none_mask;
	cpumask_var_t cpus __free(free_cpumask_var);
	int cpu, weight;

	if (!alloc_cpumask_var(&cpus, GFP_KERNEL))
		return -ENOMEM;

	rcu_read_lock();
	for_each_numa_hop_mask(next, node) {
		weight = cpumask_weight_andnot(next, prev);
		while (weight > 0) {
			cpumask_andnot(cpus, next, prev);
			for_each_cpu(cpu, cpus) {
				cpumask_andnot(cpus, cpus, topology_sibling_cpumask(cpu));
				--weight;

				if (unlikely(skip_first_cpu)) {
					skip_first_cpu = false;
					continue;
				}

				if (len-- == 0)
					goto done;

				irq_set_affinity_and_hint(*irqs++, topology_sibling_cpumask(cpu));
			}
		}
		prev = next;
	}
done:
	rcu_read_unlock();
	return 0;
}

/* must be called with cpus_read_lock() held */
static void mana_irq_setup_linear(unsigned int *irqs, unsigned int len)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (len == 0)
			break;

		irq_set_affinity_and_hint(*irqs++, cpumask_of(cpu));
		len--;
	}
}

static int mana_gd_setup_dyn_irqs(struct pci_dev *pdev, int nvec)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_irq_context *gic;
	int *irqs, err, i, msi;

	irqs = kmalloc_objs(int, nvec);
	if (!irqs)
		return -ENOMEM;

	/*
	 * In this function, num_msix_usable = HWC IRQ + Queue IRQ.
	 * nvec is only Queue IRQ (HWC already setup).
	 * While processing the next pci irq vector, we start with index 1,
	 * as IRQ vector at index 0 is already processed for HWC.
	 * However, the population of irqs array starts with index 0, to be
	 * further used in mana_irq_setup_numa_aware()
	 */
	for (i = 1; i <= nvec; i++) {
		msi = i;
		gic = mana_gd_get_gic(gc, false, &msi);
		if (IS_ERR(gic)) {
			err = PTR_ERR(gic);
			goto free_irq;
		}

		irqs[i - 1] = gic->irq;
	}

	/*
	 * When calling mana_irq_setup_numa_aware() for dynamically added IRQs,
	 * if number of CPUs is more than or equal to allocated MSI-X, we need to
	 * skip the first CPU sibling group since they are already affinitized to
	 * HWC IRQ
	 */
	cpus_read_lock();
	if (gc->num_msix_usable <= num_online_cpus()) {
		err = mana_irq_setup_numa_aware(irqs, nvec, gc->numa_node,
						true);
		if (err) {
			cpus_read_unlock();
			goto free_irq;
		}
	} else {
		/*
		 * When num_msix_usable are more than num_online_cpus, our
		 * queue IRQs should be equal to num of online vCPUs.
		 * We try to make sure queue IRQs spread across all vCPUs.
		 * In such a case NUMA or CPU core affinity does not matter.
		 * Note: in this case the total mana IRQ should always be
		 * num_online_cpus + 1. The first HWC IRQ is already handled
		 * in HWC setup calls
		 * However, if CPUs went offline since num_msix_usable was
		 * computed, queue IRQs will be more than num_online_cpus().
		 * In such cases remaining extra IRQs will retain their default
		 * affinity.
		 */
		int first_unassigned = num_online_cpus();

		if (nvec > first_unassigned) {
			char buf[32];

			if (first_unassigned == nvec - 1)
				snprintf(buf, sizeof(buf), "%d",
					 first_unassigned);
			else
				snprintf(buf, sizeof(buf), "%d-%d",
					 first_unassigned, nvec - 1);

			dev_dbg(&pdev->dev,
				"MANA IRQ indices #%s will retain the default CPU affinity\n",
				buf);
		}

		mana_irq_setup_linear(irqs, nvec);
	}

	cpus_read_unlock();
	kfree(irqs);
	return 0;

free_irq:
	for (i -= 1; i > 0; i--)
		mana_gd_put_gic(gc, false, i);
	kfree(irqs);
	return err;
}

static int mana_gd_setup_irqs(struct pci_dev *pdev, int nvec)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_irq_context *gic;
	int *irqs, *start_irqs;
	unsigned int cpu;
	int err, i, msi;

	irqs = kmalloc_objs(int, nvec);
	if (!irqs)
		return -ENOMEM;

	start_irqs = irqs;

	for (i = 0; i < nvec; i++) {
		msi = i;
		gic = mana_gd_get_gic(gc, false, &msi);
		if (IS_ERR(gic)) {
			err = PTR_ERR(gic);
			goto free_irq;
		}

		irqs[i] = gic->irq;
	}

	/* If number of IRQ is one extra than number of online CPUs,
	 * then we need to assign IRQ0 (hwc irq) and IRQ1 to
	 * same CPU.
	 * Else we will use different CPUs for IRQ0 and IRQ1.
	 * Also we are using cpumask_local_spread instead of
	 * cpumask_first for the node, because the node can be
	 * mem only.
	 */
	cpus_read_lock();
	if (nvec > num_online_cpus()) {
		cpu = cpumask_local_spread(0, gc->numa_node);
		irq_set_affinity_and_hint(irqs[0], cpumask_of(cpu));
		irqs++;
		nvec -= 1;
	}

	err = mana_irq_setup_numa_aware(irqs, nvec, gc->numa_node, false);
	if (err) {
		cpus_read_unlock();
		goto free_irq;
	}

	cpus_read_unlock();
	kfree(start_irqs);
	return 0;

free_irq:
	for (i -= 1; i >= 0; i--)
		mana_gd_put_gic(gc, false, i);

	kfree(start_irqs);
	return err;
}

static int mana_gd_setup_hwc_irqs(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	unsigned int max_irqs, min_irqs;
	int nvec, err;

	if (pci_msix_can_alloc_dyn(pdev)) {
		max_irqs = 1;
		min_irqs = 1;
	} else {
		/* Need 1 interrupt for HWC */
		max_irqs = min(num_online_cpus(), MANA_MAX_NUM_QUEUES) + 1;
		min_irqs = 2;
		gc->msi_sharing = true;
	}

	nvec = pci_alloc_irq_vectors(pdev, min_irqs, max_irqs, PCI_IRQ_MSIX);
	if (nvec < 0)
		return nvec;

	err = mana_gd_setup_irqs(pdev, nvec);
	if (err) {
		pci_free_irq_vectors(pdev);
		return err;
	}

	gc->num_msix_usable = nvec;
	gc->max_num_msix = nvec;

	return 0;
}

static int mana_gd_setup_remaining_irqs(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct msi_map irq_map;
	int max_irqs, i, err;

	if (!pci_msix_can_alloc_dyn(pdev))
		/* remain irqs are already allocated with HWC IRQ */
		return 0;

	/* allocate only remaining IRQs*/
	max_irqs = gc->num_msix_usable - 1;

	for (i = 1; i <= max_irqs; i++) {
		irq_map = pci_msix_alloc_irq_at(pdev, i, NULL);
		if (!irq_map.virq) {
			err = irq_map.index;
			/* caller will handle cleaning up all allocated
			 * irqs, after HWC is destroyed
			 */
			return err;
		}
	}

	err = mana_gd_setup_dyn_irqs(pdev, max_irqs);
	if (err)
		return err;

	gc->max_num_msix = gc->max_num_msix + max_irqs;

	return 0;
}

static void mana_gd_remove_irqs(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	int i;

	if (gc->max_num_msix < 1)
		return;

	for (i = 0; i < gc->max_num_msix; i++) {
		if (!xa_load(&gc->irq_contexts, i))
			continue;

		mana_gd_put_gic(gc, false, i);
	}

	WARN_ON(!xa_empty(&gc->irq_contexts));

	pci_free_irq_vectors(pdev);

	bitmap_free(gc->msi_bitmap);
	gc->msi_bitmap = NULL;
	gc->max_num_msix = 0;
	gc->num_msix_usable = 0;
}

static int mana_pci_setup_hwc_irqs(struct gdma_context *gc)
{
	return mana_gd_setup_hwc_irqs(to_pci_dev(gc->dev));
}

static int mana_pci_setup_remaining_irqs(struct gdma_context *gc)
{
	int err;

	err = mana_gd_setup_remaining_irqs(to_pci_dev(gc->dev));
	if (err) {
		dev_err(gc->dev, "Failed to setup remaining IRQs: %d", err);
		return err;
	}

	if (!gc->msi_sharing) {
		gc->msi_bitmap = bitmap_zalloc(gc->num_msix_usable, GFP_KERNEL);
		if (!gc->msi_bitmap)
			return -ENOMEM;
		/* Set bit for HWC */
		set_bit(0, gc->msi_bitmap);
	}

	return 0;
}

static void mana_pci_remove_irqs(struct gdma_context *gc)
{
	mana_gd_remove_irqs(to_pci_dev(gc->dev));
}

static const struct gdma_bus_ops mana_pci_bus_ops = {
	.bus_name		= "pci",
	.adev_prefix		= "mana",
	.msix_can_alloc_dyn	= mana_pci_msix_can_alloc_dyn,
	.msix_virq		= mana_pci_msix_virq,
	.msix_alloc_at		= mana_pci_msix_alloc_at,
	.msix_free		= mana_pci_msix_free,
	.msix_vec_count		= mana_pci_msix_vec_count,
	.setup_hwc_irqs		= mana_pci_setup_hwc_irqs,
	.setup_remaining_irqs	= mana_pci_setup_remaining_irqs,
	.remove_irqs		= mana_pci_remove_irqs,
	.dev_reset		= mana_pci_dev_reset,
	.schedule_serv_work	= mana_pci_schedule_serv_work,
	.drv_cap_flags1		= GDMA_DRV_CAP_FLAGS1_PCI,
};

static bool mana_is_pf(unsigned short dev_id)
{
	return dev_id == MANA_PF_DEVICE_ID || dev_id == MANA_PF2_DEVICE_ID;
}

static int mana_gd_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct gdma_context *gc;
	void __iomem *bar0_va;
	int bar = 0;
	int err;

	/* Each port has 2 CQs, each CQ has at most 1 EQE at a time */
	BUILD_BUG_ON(2 * MAX_PORTS_IN_MANA_DEV * GDMA_EQE_SIZE > EQ_SIZE);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable pci device (err=%d)\n", err);
		return -ENXIO;
	}

	pci_set_master(pdev);

	err = pci_request_regions(pdev, "mana");
	if (err)
		goto disable_dev;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "DMA set mask failed: %d\n", err);
		goto release_region;
	}
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	err = -ENOMEM;
	gc = vzalloc(sizeof(*gc));
	if (!gc)
		goto release_region;

	mutex_init(&gc->eq_test_event_mutex);
	mutex_init(&gc->gic_mutex);
	pci_set_drvdata(pdev, gc);
	gc->bar0_pa = pci_resource_start(pdev, 0);
	gc->bar0_size = pci_resource_len(pdev, 0);

	bar0_va = pci_iomap(pdev, bar, 0);
	if (!bar0_va)
		goto free_gc;

	gc->numa_node = dev_to_node(&pdev->dev);
	gc->is_pf = mana_is_pf(pdev->device);
	gc->is_pf2 = (pdev->device == MANA_PF2_DEVICE_ID);

	gc->bar0_va = bar0_va;
	gc->dev = &pdev->dev;
	gc->bus_ops = &mana_pci_bus_ops;
	xa_init(&gc->irq_contexts);

	err = mana_gd_setup(gc);
	if (err)
		goto unmap_bar;

	err = mana_probe(&gc->mana, false);
	if (err)
		goto cleanup_gd;

	err = mana_rdma_probe(&gc->mana_ib);
	if (err)
		goto cleanup_mana;

	/*
	 * If a hardware reset event has occurred over HWC during probe,
	 * rollback and perform hardware reset procedure.
	 */
	if (test_and_set_bit(GC_PROBE_SUCCEEDED, &gc->flags)) {
		err = -EPROTO;
		goto cleanup_mana_rdma;
	}

	return 0;

cleanup_mana_rdma:
	mana_rdma_remove(&gc->mana_ib);
cleanup_mana:
	mana_remove(&gc->mana, false);
cleanup_gd:
	mana_gd_cleanup(gc);
unmap_bar:
	xa_destroy(&gc->irq_contexts);
	pci_iounmap(pdev, bar0_va);
free_gc:
	pci_set_drvdata(pdev, NULL);
	vfree(gc);
release_region:
	pci_release_regions(pdev);
disable_dev:
	pci_disable_device(pdev);
	dev_err(&pdev->dev, "gdma probe failed: err = %d\n", err);

	/*
	 * Hardware could be in recovery mode and the HWC returns TIMEDOUT or
	 * EPROTO from mana_gd_setup(), mana_probe() or mana_rdma_probe(), or
	 * we received a hardware reset event over HWC interrupt. In this case,
	 * perform the device recovery procedure after MANA_SERVICE_PERIOD
	 * seconds.
	 */
	if (err == -ETIMEDOUT || err == -EPROTO) {
		struct mana_dev_recovery *dev;
		unsigned long flags;

		dev_info(&pdev->dev, "Start MANA recovery mode\n");

		dev = kzalloc_obj(*dev);
		if (!dev)
			return err;

		dev->pdev = pci_dev_get(pdev);
		dev->type = GDMA_EQE_HWC_RESET_REQUEST;

		spin_lock_irqsave(&mana_dev_recovery_work.lock, flags);
		list_add_tail(&dev->list, &mana_dev_recovery_work.dev_list);
		spin_unlock_irqrestore(&mana_dev_recovery_work.lock, flags);

		schedule_delayed_work(&mana_dev_recovery_work.work,
				      secs_to_jiffies(MANA_SERVICE_PERIOD));
	}

	return err;
}

static void mana_gd_remove(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	pci_disable_sriov(pdev);

	mana_rdma_remove(&gc->mana_ib);
	mana_remove(&gc->mana, false);

	mana_gd_cleanup(gc);

	xa_destroy(&gc->irq_contexts);

	pci_iounmap(pdev, gc->bar0_va);

	vfree(gc);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	dev_dbg(&pdev->dev, "mana gdma remove successful\n");
}

/* The 'state' parameter is not used. */
static int mana_gd_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	mana_rdma_remove(&gc->mana_ib);
	mana_remove(&gc->mana, true);

	mana_gd_cleanup(gc);

	return 0;
}

static int mana_gd_resume(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	int err;

	err = mana_gd_setup(gc);
	if (err)
		return err;

	err = mana_probe(&gc->mana, true);
	if (err)
		goto cleanup_gd;

	err = mana_rdma_probe(&gc->mana_ib);
	if (err)
		mana_rdma_remove(&gc->mana_ib);

	return err;

cleanup_gd:
	mana_gd_cleanup(gc);
	return err;
}

/* Quiesce the device for kexec. This is also called upon reboot/shutdown. */
static void mana_gd_shutdown(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "Shutdown was called\n");

	mana_rdma_remove(&gc->mana_ib);
	mana_remove(&gc->mana, true);

	mana_gd_cleanup(gc);

	pci_disable_device(pdev);
}

static int mana_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	int err = 0;

	dev_info(&pdev->dev, "Requested num VFs: %d\n", numvfs);

	if (numvfs > 0) {
		err = pci_enable_sriov(pdev, numvfs);
	} else {
		if (pci_vfs_assigned(pdev)) {
			dev_warn(&pdev->dev,
				 "Cannot disable SR-IOV while VFs are assigned\n");
			return -EPERM;
		}

		pci_disable_sriov(pdev);
	}

	return err ? err : numvfs;
}

static const struct pci_device_id mana_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MICROSOFT, MANA_PF_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICROSOFT, MANA_PF2_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MICROSOFT, MANA_VF_DEVICE_ID) },
	{ }
};

static struct pci_driver mana_driver = {
	.name		= "mana",
	.id_table	= mana_id_table,
	.probe		= mana_gd_probe,
	.remove		= mana_gd_remove,
	.suspend	= mana_gd_suspend,
	.resume		= mana_gd_resume,
	.shutdown	= mana_gd_shutdown,
	.sriov_configure = mana_sriov_configure,
};

MODULE_DEVICE_TABLE(pci, mana_id_table);

static int __init mana_driver_init(void)
{
	INIT_LIST_HEAD(&mana_dev_recovery_work.dev_list);
	spin_lock_init(&mana_dev_recovery_work.lock);
	INIT_DELAYED_WORK(&mana_dev_recovery_work.work,
			  mana_recovery_delayed_func);

	return pci_register_driver(&mana_driver);
}

static void __exit mana_driver_exit(void)
{
	struct mana_dev_recovery *dev;
	unsigned long flags;

	disable_delayed_work_sync(&mana_dev_recovery_work.work);

	spin_lock_irqsave(&mana_dev_recovery_work.lock, flags);
	while (!list_empty(&mana_dev_recovery_work.dev_list)) {
		dev = list_first_entry(&mana_dev_recovery_work.dev_list,
				       struct mana_dev_recovery, list);
		list_del(&dev->list);
		pci_dev_put(dev->pdev);
		kfree(dev);
	}
	spin_unlock_irqrestore(&mana_dev_recovery_work.lock, flags);

	pci_unregister_driver(&mana_driver);
}

module_init(mana_driver_init);
module_exit(mana_driver_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Microsoft Azure Network Adapter driver");
MODULE_IMPORT_NS("NET_MANA");
