// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2025, Microsoft Corporation. */
#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/cdx/cdx_bus.h>
#include <linux/msi.h>
#include <linux/xarray.h>

#include <net/mana/mana.h>

#define MANA_CDX_GDMA_REGS_SIZE	(GDMA_REG_SHM_OFFSET + sizeof(u64))

static int mana_cdx_msix_virq(struct gdma_context *gc, int msi)
{
	unsigned int virq = msi_get_virq(gc->dev, msi);

	/* CDX allocates its whole vector pool in mana_cdx_setup_hwc_irqs(),
	 * so a vector with no Linux IRQ was never part of that pool.
	 */
	return virq ? virq : -EINVAL;
}

static int mana_cdx_dev_reset(struct gdma_context *gc)
{
	return cdx_dev_reset(gc->dev);
}

static int mana_cdx_setup_hwc_irqs(struct gdma_context *gc)
{
	struct cdx_device *cdx_dev = to_cdx_device(gc->dev);
	struct gdma_irq_context *gic;
	unsigned int max_irqs;
	int nvec;
	int err, i = 0, j;

	max_irqs = cdx_dev->num_msi;

	/* Need 1 IRQ for HWC and at least 1 for the data path queues */
	if (max_irqs < 2) {
		dev_err(&cdx_dev->dev, "Not enough MSI vectors: %u\n",
			max_irqs);
		return -ENOSPC;
	}

	/* Constrain the device to 32-bit DMA addresses, for both the MSI
	 * doorbells and the data path buffers, and never widen it afterwards.
	 *
	 * The MSI doorbell address has to land within the low 4GB. The data
	 * path carries a second constraint: the device does not perform
	 * writes to the final page of the 48-bit IOVA window, and
	 * iommu_dma_alloc_iova() allocates downwards from the top of that
	 * window, so the first queue created lands on the unusable page and
	 * its event queue stalls permanently. PCI devices are shielded from
	 * this by the low-4GB first attempt in iommu_dma_alloc_iova(), but
	 * that attempt is gated on dev_is_pci() and CDX devices do not
	 * qualify.
	 */
	err = dma_set_mask_and_coherent(gc->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&cdx_dev->dev, "Failed to set 32-bit DMA mask\n");
		return err;
	}

	/* The PCI path also calls dma_set_max_seg_size(), but that is
	 * deliberately omitted here: the CDX core does not attach a
	 * dma_parms to the device, so the helper would only WARN, and MANA
	 * never builds scatter-gather lists, so the limit is never read.
	 */

	err = cdx_enable_msi(cdx_dev);
	if (err) {
		dev_err(&cdx_dev->dev, "Failed to enable MSI\n");
		return err;
	}

	err = msi_domain_alloc_irqs(gc->dev, MSI_DEFAULT_DOMAIN, max_irqs);
	if (err) {
		dev_err(&cdx_dev->dev, "Failed to alloc MSI IRQs\n");
		cdx_disable_msi(cdx_dev);
		return err;
	}

	nvec = max_irqs;

	xa_init(&gc->irq_contexts);

	/* No IRQ affinity hint is applied here. The PCI transport spreads its
	 * vectors over the NUMA node local to the device; the CDX platforms
	 * this driver targets are single-node, so there is nothing to spread
	 * over. Affinity management can be added when a multi-node CDX host
	 * exists to validate it against.
	 */
	for (i = 0; i < nvec; i++) {
		int msi = i;

		gic = mana_gd_get_gic(gc, false, &msi);
		if (IS_ERR(gic)) {
			err = PTR_ERR(gic);
			goto free_irq;
		}
	}

	gc->max_num_msix = nvec;
	gc->num_msix_usable = nvec;

	/* CDX sizes its MSI pool at probe time and cannot grow it later, so
	 * never share vectors: vector 0 is reserved for the HWC and each of
	 * the remaining nvec - 1 vectors backs exactly one EQ. That bounds
	 * the queue count at nvec - 1 and guarantees the HWC vector is never
	 * handed out to a data queue.
	 */
	gc->msi_sharing = false;

	gc->msi_bitmap = bitmap_zalloc(nvec, GFP_KERNEL);
	if (!gc->msi_bitmap) {
		err = -ENOMEM;
		goto free_irq;
	}

	/* Reserve the HWC vector */
	set_bit(0, gc->msi_bitmap);

	return 0;

free_irq:
	for (j = i - 1; j >= 0; j--)
		mana_gd_put_gic(gc, false, j);

	xa_destroy(&gc->irq_contexts);
	msi_domain_free_irqs_all(gc->dev, MSI_DEFAULT_DOMAIN);
	cdx_disable_msi(cdx_dev);
	return err;
}

static void mana_cdx_remove_irqs(struct gdma_context *gc)
{
	struct cdx_device *cdx_dev = to_cdx_device(gc->dev);
	int i;

	if (gc->max_num_msix < 1)
		return;

	for (i = 0; i < gc->max_num_msix; i++) {
		if (!xa_load(&gc->irq_contexts, i))
			continue;

		mana_gd_put_gic(gc, false, i);
	}

	WARN_ON(!xa_empty(&gc->irq_contexts));

	xa_destroy(&gc->irq_contexts);
	msi_domain_free_irqs_all(gc->dev, MSI_DEFAULT_DOMAIN);
	cdx_disable_msi(cdx_dev);

	bitmap_free(gc->msi_bitmap);
	gc->msi_bitmap = NULL;
	gc->max_num_msix = 0;
	gc->num_msix_usable = 0;
}

static int mana_cdx_setup_remaining_irqs(struct gdma_context *gc)
{
	unsigned int max_queues_vport;

	/* mana_gd_query_max_resources() may turn MSI sharing back on and round
	 * the per-vPort queue count up to MANA_DEF_NUM_QUEUES. CDX sizes its
	 * vector pool at probe time and cannot grow it later, and the HWC
	 * vector must stay private, so give every vPort a private slice of the
	 * remaining vectors instead. gc->max_num_queues is already capped at
	 * num_msix_usable - 1 by the core.
	 */
	gc->msi_sharing = false;
	max_queues_vport = (gc->num_msix_usable - 1) / gc->num_ports;
	if (!max_queues_vport) {
		dev_err(gc->dev, "%u MSI vectors cannot serve %u vPorts\n",
			gc->num_msix_usable, gc->num_ports);
		return -ENOSPC;
	}

	gc->max_num_queues_vport = min(gc->max_num_queues, max_queues_vport);

	dev_dbg(gc->dev, "%u MSI vectors, %u vPorts, %u queues, no MSI sharing\n",
		gc->num_msix_usable, gc->num_ports, gc->max_num_queues_vport);

	return 0;
}

static const struct gdma_bus_ops mana_cdx_bus_ops = {
	.bus_name		= "cdx",
	.adev_prefix		= "mana_cdx",
	.msix_virq		= mana_cdx_msix_virq,
	.setup_hwc_irqs		= mana_cdx_setup_hwc_irqs,
	.setup_remaining_irqs	= mana_cdx_setup_remaining_irqs,
	.remove_irqs		= mana_cdx_remove_irqs,
	.dev_reset		= mana_cdx_dev_reset,
};

static int mana_cdx_gd_probe(struct cdx_device *cdx_dev)
{
	struct gdma_context *gc;
	void __iomem *bar0_va;
	int err;

	/* Each port has 2 CQs, each CQ has at most 1 EQE at a time */
	BUILD_BUG_ON(2 * MAX_PORTS_IN_MANA_DEV * GDMA_EQE_SIZE > EQ_SIZE);

	err = cdx_dev_reset(&cdx_dev->dev);
	if (err)
		return err;

	err = cdx_set_master(cdx_dev);
	if (err)
		return err;

	gc = vzalloc(sizeof(*gc));
	if (!gc) {
		err = -ENOMEM;
		goto clear_master;
	}

	mutex_init(&gc->eq_test_event_mutex);
	mutex_init(&gc->gic_mutex);
	dev_set_drvdata(&cdx_dev->dev, gc);
	gc->bar0_pa = cdx_resource_start(cdx_dev, 0);
	gc->bar0_size = cdx_resource_len(cdx_dev, 0);
	if (gc->bar0_size < MANA_CDX_GDMA_REGS_SIZE) {
		dev_err(&cdx_dev->dev, "BAR0 size %#llx is too small\n",
			(u64)gc->bar0_size);
		err = -EINVAL;
		goto free_gc;
	}

	bar0_va = ioremap(gc->bar0_pa, gc->bar0_size);
	if (!bar0_va) {
		err = -ENOMEM;
		goto free_gc;
	}

	gc->numa_node = dev_to_node(&cdx_dev->dev);
	gc->bar0_va = bar0_va;
	gc->dev = &cdx_dev->dev;
	gc->bus_ops = &mana_cdx_bus_ops;

	/* gc->is_pf is intentionally left clear: this device uses the VF
	 * register layout, and the vPort and MAC filter registration that
	 * is_pf selects is not required on this bus.
	 */

	err = mana_gd_setup(gc);
	if (err)
		goto unmap_bar;

	err = mana_probe(&gc->mana, false);
	if (err)
		goto cleanup_gd;

	err = mana_rdma_probe(&gc->mana_ib);
	if (err)
		goto remove_mana;

	/* If a hardware reset event arrived over HWC during probe, roll back:
	 * the device state the rest of probe observed is no longer valid.
	 */
	if (test_and_set_bit(GC_PROBE_SUCCEEDED, &gc->flags)) {
		err = -EPROTO;
		goto remove_rdma;
	}

	return 0;

remove_rdma:
	mana_rdma_remove(&gc->mana_ib);
remove_mana:
	mana_remove(&gc->mana, false);
cleanup_gd:
	mana_gd_cleanup(gc);
unmap_bar:
	iounmap(bar0_va);
free_gc:
	dev_set_drvdata(&cdx_dev->dev, NULL);
	vfree(gc);
clear_master:
	cdx_clear_master(cdx_dev);
	return err;
}

static int mana_cdx_gd_remove(struct cdx_device *cdx_dev)
{
	struct gdma_context *gc = dev_get_drvdata(&cdx_dev->dev);

	mana_rdma_remove(&gc->mana_ib);
	mana_remove(&gc->mana, false);

	mana_gd_cleanup(gc);
	cdx_clear_master(cdx_dev);

	iounmap(gc->bar0_va);

	dev_set_drvdata(&cdx_dev->dev, NULL);
	vfree(gc);
	return 0;
}

/* Quiesce the device for kexec. This is also called upon reboot/shutdown. */
static void mana_cdx_gd_shutdown(struct cdx_device *cdx_dev)
{
	struct gdma_context *gc = dev_get_drvdata(&cdx_dev->dev);

	mana_rdma_remove(&gc->mana_ib);
	mana_remove(&gc->mana, true);

	mana_gd_cleanup(gc);
	cdx_clear_master(cdx_dev);
}

#define CDX_VENDOR_ID_MICROSOFT 0x1414
static const struct cdx_device_id mana_cdx_table[] = {
	{ CDX_DEVICE(CDX_VENDOR_ID_MICROSOFT, MANA_CDX_DEVICE_ID) },
	{}
};

MODULE_DEVICE_TABLE(cdx, mana_cdx_table);

static struct cdx_driver gdma_cdx_driver = {
	.match_id_table	= mana_cdx_table,
	.probe		= mana_cdx_gd_probe,
	.remove		= mana_cdx_gd_remove,
	.shutdown	= mana_cdx_gd_shutdown,
	.driver	= {
		.name	= "mana_cdx",
	},
};

module_driver(gdma_cdx_driver, cdx_driver_register, cdx_driver_unregister);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Microsoft Azure Network Adapter driver for the CDX bus");
MODULE_IMPORT_NS("CDX_BUS");
MODULE_IMPORT_NS("NET_MANA");
