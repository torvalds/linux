// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/aer.h>
#include <linux/fs.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <uapi/linux/idxd.h>
#include <linux/dmaengine.h>
#include "../dmaengine.h"
#include "registers.h"
#include "idxd.h"

MODULE_VERSION(IDXD_DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");

#define DRV_NAME "idxd"

static struct idr idxd_idrs[IDXD_TYPE_MAX];
static struct mutex idxd_idr_lock;

static struct pci_device_id idxd_pci_tbl[] = {
	/* DSA ver 1.0 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_DSA_SPR0) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, idxd_pci_tbl);

static char *idxd_name[] = {
	"dsa",
};

const char *idxd_get_dev_name(struct idxd_device *idxd)
{
	return idxd_name[idxd->type];
}

static int idxd_setup_interrupts(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	struct msix_entry *msix;
	struct idxd_irq_entry *irq_entry;
	int i, msixcnt;
	int rc = 0;

	msixcnt = pci_msix_vec_count(pdev);
	if (msixcnt < 0) {
		dev_err(dev, "Not MSI-X interrupt capable.\n");
		goto err_no_irq;
	}

	idxd->msix_entries = devm_kzalloc(dev, sizeof(struct msix_entry) *
			msixcnt, GFP_KERNEL);
	if (!idxd->msix_entries) {
		rc = -ENOMEM;
		goto err_no_irq;
	}

	for (i = 0; i < msixcnt; i++)
		idxd->msix_entries[i].entry = i;

	rc = pci_enable_msix_exact(pdev, idxd->msix_entries, msixcnt);
	if (rc) {
		dev_err(dev, "Failed enabling %d MSIX entries.\n", msixcnt);
		goto err_no_irq;
	}
	dev_dbg(dev, "Enabled %d msix vectors\n", msixcnt);

	/*
	 * We implement 1 completion list per MSI-X entry except for
	 * entry 0, which is for errors and others.
	 */
	idxd->irq_entries = devm_kcalloc(dev, msixcnt,
					 sizeof(struct idxd_irq_entry),
					 GFP_KERNEL);
	if (!idxd->irq_entries) {
		rc = -ENOMEM;
		goto err_no_irq;
	}

	for (i = 0; i < msixcnt; i++) {
		idxd->irq_entries[i].id = i;
		idxd->irq_entries[i].idxd = idxd;
	}

	msix = &idxd->msix_entries[0];
	irq_entry = &idxd->irq_entries[0];
	rc = devm_request_threaded_irq(dev, msix->vector, idxd_irq_handler,
				       idxd_misc_thread, 0, "idxd-misc",
				       irq_entry);
	if (rc < 0) {
		dev_err(dev, "Failed to allocate misc interrupt.\n");
		goto err_no_irq;
	}

	dev_dbg(dev, "Allocated idxd-misc handler on msix vector %d\n",
		msix->vector);

	/* first MSI-X entry is not for wq interrupts */
	idxd->num_wq_irqs = msixcnt - 1;

	for (i = 1; i < msixcnt; i++) {
		msix = &idxd->msix_entries[i];
		irq_entry = &idxd->irq_entries[i];

		init_llist_head(&idxd->irq_entries[i].pending_llist);
		INIT_LIST_HEAD(&idxd->irq_entries[i].work_list);
		rc = devm_request_threaded_irq(dev, msix->vector,
					       idxd_irq_handler,
					       idxd_wq_thread, 0,
					       "idxd-portal", irq_entry);
		if (rc < 0) {
			dev_err(dev, "Failed to allocate irq %d.\n",
				msix->vector);
			goto err_no_irq;
		}
		dev_dbg(dev, "Allocated idxd-msix %d for vector %d\n",
			i, msix->vector);
	}

	idxd_unmask_error_interrupts(idxd);

	return 0;

 err_no_irq:
	/* Disable error interrupt generation */
	idxd_mask_error_interrupts(idxd);
	pci_disable_msix(pdev);
	dev_err(dev, "No usable interrupts\n");
	return rc;
}

static int idxd_setup_internals(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i;

	init_waitqueue_head(&idxd->cmd_waitq);
	idxd->groups = devm_kcalloc(dev, idxd->max_groups,
				    sizeof(struct idxd_group), GFP_KERNEL);
	if (!idxd->groups)
		return -ENOMEM;

	for (i = 0; i < idxd->max_groups; i++) {
		idxd->groups[i].idxd = idxd;
		idxd->groups[i].id = i;
		idxd->groups[i].tc_a = -1;
		idxd->groups[i].tc_b = -1;
	}

	idxd->wqs = devm_kcalloc(dev, idxd->max_wqs, sizeof(struct idxd_wq),
				 GFP_KERNEL);
	if (!idxd->wqs)
		return -ENOMEM;

	idxd->engines = devm_kcalloc(dev, idxd->max_engines,
				     sizeof(struct idxd_engine), GFP_KERNEL);
	if (!idxd->engines)
		return -ENOMEM;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

		wq->id = i;
		wq->idxd = idxd;
		mutex_init(&wq->wq_lock);
		wq->idxd_cdev.minor = -1;
		wq->max_xfer_bytes = idxd->max_xfer_bytes;
		wq->max_batch_size = idxd->max_batch_size;
		wq->wqcfg = devm_kzalloc(dev, idxd->wqcfg_size, GFP_KERNEL);
		if (!wq->wqcfg)
			return -ENOMEM;
	}

	for (i = 0; i < idxd->max_engines; i++) {
		idxd->engines[i].idxd = idxd;
		idxd->engines[i].id = i;
	}

	idxd->wq = create_workqueue(dev_name(dev));
	if (!idxd->wq)
		return -ENOMEM;

	return 0;
}

static void idxd_read_table_offsets(struct idxd_device *idxd)
{
	union offsets_reg offsets;
	struct device *dev = &idxd->pdev->dev;

	offsets.bits[0] = ioread64(idxd->reg_base + IDXD_TABLE_OFFSET);
	offsets.bits[1] = ioread64(idxd->reg_base + IDXD_TABLE_OFFSET
			+ sizeof(u64));
	idxd->grpcfg_offset = offsets.grpcfg * 0x100;
	dev_dbg(dev, "IDXD Group Config Offset: %#x\n", idxd->grpcfg_offset);
	idxd->wqcfg_offset = offsets.wqcfg * 0x100;
	dev_dbg(dev, "IDXD Work Queue Config Offset: %#x\n",
		idxd->wqcfg_offset);
	idxd->msix_perm_offset = offsets.msix_perm * 0x100;
	dev_dbg(dev, "IDXD MSIX Permission Offset: %#x\n",
		idxd->msix_perm_offset);
	idxd->perfmon_offset = offsets.perfmon * 0x100;
	dev_dbg(dev, "IDXD Perfmon Offset: %#x\n", idxd->perfmon_offset);
}

static void idxd_read_caps(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i;

	/* reading generic capabilities */
	idxd->hw.gen_cap.bits = ioread64(idxd->reg_base + IDXD_GENCAP_OFFSET);
	dev_dbg(dev, "gen_cap: %#llx\n", idxd->hw.gen_cap.bits);
	idxd->max_xfer_bytes = 1ULL << idxd->hw.gen_cap.max_xfer_shift;
	dev_dbg(dev, "max xfer size: %llu bytes\n", idxd->max_xfer_bytes);
	idxd->max_batch_size = 1U << idxd->hw.gen_cap.max_batch_shift;
	dev_dbg(dev, "max batch size: %u\n", idxd->max_batch_size);
	if (idxd->hw.gen_cap.config_en)
		set_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags);

	/* reading group capabilities */
	idxd->hw.group_cap.bits =
		ioread64(idxd->reg_base + IDXD_GRPCAP_OFFSET);
	dev_dbg(dev, "group_cap: %#llx\n", idxd->hw.group_cap.bits);
	idxd->max_groups = idxd->hw.group_cap.num_groups;
	dev_dbg(dev, "max groups: %u\n", idxd->max_groups);
	idxd->max_tokens = idxd->hw.group_cap.total_tokens;
	dev_dbg(dev, "max tokens: %u\n", idxd->max_tokens);
	idxd->nr_tokens = idxd->max_tokens;

	/* read engine capabilities */
	idxd->hw.engine_cap.bits =
		ioread64(idxd->reg_base + IDXD_ENGCAP_OFFSET);
	dev_dbg(dev, "engine_cap: %#llx\n", idxd->hw.engine_cap.bits);
	idxd->max_engines = idxd->hw.engine_cap.num_engines;
	dev_dbg(dev, "max engines: %u\n", idxd->max_engines);

	/* read workqueue capabilities */
	idxd->hw.wq_cap.bits = ioread64(idxd->reg_base + IDXD_WQCAP_OFFSET);
	dev_dbg(dev, "wq_cap: %#llx\n", idxd->hw.wq_cap.bits);
	idxd->max_wq_size = idxd->hw.wq_cap.total_wq_size;
	dev_dbg(dev, "total workqueue size: %u\n", idxd->max_wq_size);
	idxd->max_wqs = idxd->hw.wq_cap.num_wqs;
	dev_dbg(dev, "max workqueues: %u\n", idxd->max_wqs);
	idxd->wqcfg_size = 1 << (idxd->hw.wq_cap.wqcfg_size + IDXD_WQCFG_MIN);
	dev_dbg(dev, "wqcfg size: %u\n", idxd->wqcfg_size);

	/* reading operation capabilities */
	for (i = 0; i < 4; i++) {
		idxd->hw.opcap.bits[i] = ioread64(idxd->reg_base +
				IDXD_OPCAP_OFFSET + i * sizeof(u64));
		dev_dbg(dev, "opcap[%d]: %#llx\n", i, idxd->hw.opcap.bits[i]);
	}
}

static struct idxd_device *idxd_alloc(struct pci_dev *pdev,
				      void __iomem * const *iomap)
{
	struct device *dev = &pdev->dev;
	struct idxd_device *idxd;

	idxd = devm_kzalloc(dev, sizeof(struct idxd_device), GFP_KERNEL);
	if (!idxd)
		return NULL;

	idxd->pdev = pdev;
	idxd->reg_base = iomap[IDXD_MMIO_BAR];
	spin_lock_init(&idxd->dev_lock);

	return idxd;
}

static int idxd_probe(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	int rc;

	dev_dbg(dev, "%s entered and resetting device\n", __func__);
	idxd_device_init_reset(idxd);
	dev_dbg(dev, "IDXD reset complete\n");

	idxd_read_caps(idxd);
	idxd_read_table_offsets(idxd);

	rc = idxd_setup_internals(idxd);
	if (rc)
		goto err_setup;

	rc = idxd_setup_interrupts(idxd);
	if (rc)
		goto err_setup;

	dev_dbg(dev, "IDXD interrupt setup complete.\n");

	mutex_lock(&idxd_idr_lock);
	idxd->id = idr_alloc(&idxd_idrs[idxd->type], idxd, 0, 0, GFP_KERNEL);
	mutex_unlock(&idxd_idr_lock);
	if (idxd->id < 0) {
		rc = -ENOMEM;
		goto err_idr_fail;
	}

	idxd->major = idxd_cdev_get_major(idxd);

	dev_dbg(dev, "IDXD device %d probed successfully\n", idxd->id);
	return 0;

 err_idr_fail:
	idxd_mask_error_interrupts(idxd);
	idxd_mask_msix_vectors(idxd);
 err_setup:
	return rc;
}

static int idxd_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem * const *iomap;
	struct device *dev = &pdev->dev;
	struct idxd_device *idxd;
	int rc;
	unsigned int mask;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	dev_dbg(dev, "Mapping BARs\n");
	mask = (1 << IDXD_MMIO_BAR);
	rc = pcim_iomap_regions(pdev, mask, DRV_NAME);
	if (rc)
		return rc;

	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return -ENOMEM;

	dev_dbg(dev, "Set DMA masks\n");
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc)
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc)
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	dev_dbg(dev, "Alloc IDXD context\n");
	idxd = idxd_alloc(pdev, iomap);
	if (!idxd)
		return -ENOMEM;

	idxd_set_type(idxd);

	dev_dbg(dev, "Set PCI master\n");
	pci_set_master(pdev);
	pci_set_drvdata(pdev, idxd);

	idxd->hw.version = ioread32(idxd->reg_base + IDXD_VER_OFFSET);
	rc = idxd_probe(idxd);
	if (rc) {
		dev_err(dev, "Intel(R) IDXD DMA Engine init failed\n");
		return -ENODEV;
	}

	rc = idxd_setup_sysfs(idxd);
	if (rc) {
		dev_err(dev, "IDXD sysfs setup failed\n");
		return -ENODEV;
	}

	idxd->state = IDXD_DEV_CONF_READY;

	dev_info(&pdev->dev, "Intel(R) Accelerator Device (v%x)\n",
		 idxd->hw.version);

	return 0;
}

static void idxd_flush_pending_llist(struct idxd_irq_entry *ie)
{
	struct idxd_desc *desc, *itr;
	struct llist_node *head;

	head = llist_del_all(&ie->pending_llist);
	if (!head)
		return;

	llist_for_each_entry_safe(desc, itr, head, llnode) {
		idxd_dma_complete_txd(desc, IDXD_COMPLETE_ABORT);
		idxd_free_desc(desc->wq, desc);
	}
}

static void idxd_flush_work_list(struct idxd_irq_entry *ie)
{
	struct idxd_desc *desc, *iter;

	list_for_each_entry_safe(desc, iter, &ie->work_list, list) {
		list_del(&desc->list);
		idxd_dma_complete_txd(desc, IDXD_COMPLETE_ABORT);
		idxd_free_desc(desc->wq, desc);
	}
}

static void idxd_shutdown(struct pci_dev *pdev)
{
	struct idxd_device *idxd = pci_get_drvdata(pdev);
	int rc, i;
	struct idxd_irq_entry *irq_entry;
	int msixcnt = pci_msix_vec_count(pdev);

	rc = idxd_device_disable(idxd);
	if (rc)
		dev_err(&pdev->dev, "Disabling device failed\n");

	dev_dbg(&pdev->dev, "%s called\n", __func__);
	idxd_mask_msix_vectors(idxd);
	idxd_mask_error_interrupts(idxd);

	for (i = 0; i < msixcnt; i++) {
		irq_entry = &idxd->irq_entries[i];
		synchronize_irq(idxd->msix_entries[i].vector);
		if (i == 0)
			continue;
		idxd_flush_pending_llist(irq_entry);
		idxd_flush_work_list(irq_entry);
	}

	destroy_workqueue(idxd->wq);
}

static void idxd_remove(struct pci_dev *pdev)
{
	struct idxd_device *idxd = pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s called\n", __func__);
	idxd_cleanup_sysfs(idxd);
	idxd_shutdown(pdev);
	mutex_lock(&idxd_idr_lock);
	idr_remove(&idxd_idrs[idxd->type], idxd->id);
	mutex_unlock(&idxd_idr_lock);
}

static struct pci_driver idxd_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= idxd_pci_tbl,
	.probe		= idxd_pci_probe,
	.remove		= idxd_remove,
	.shutdown	= idxd_shutdown,
};

static int __init idxd_init_module(void)
{
	int err, i;

	/*
	 * If the CPU does not support write512, there's no point in
	 * enumerating the device. We can not utilize it.
	 */
	if (!boot_cpu_has(X86_FEATURE_MOVDIR64B)) {
		pr_warn("idxd driver failed to load without MOVDIR64B.\n");
		return -ENODEV;
	}

	pr_info("%s: Intel(R) Accelerator Devices Driver %s\n",
		DRV_NAME, IDXD_DRIVER_VERSION);

	mutex_init(&idxd_idr_lock);
	for (i = 0; i < IDXD_TYPE_MAX; i++)
		idr_init(&idxd_idrs[i]);

	err = idxd_register_bus_type();
	if (err < 0)
		return err;

	err = idxd_register_driver();
	if (err < 0)
		goto err_idxd_driver_register;

	err = idxd_cdev_register();
	if (err)
		goto err_cdev_register;

	err = pci_register_driver(&idxd_pci_driver);
	if (err)
		goto err_pci_register;

	return 0;

err_pci_register:
	idxd_cdev_remove();
err_cdev_register:
	idxd_unregister_driver();
err_idxd_driver_register:
	idxd_unregister_bus_type();
	return err;
}
module_init(idxd_init_module);

static void __exit idxd_exit_module(void)
{
	pci_unregister_driver(&idxd_pci_driver);
	idxd_cdev_remove();
	idxd_unregister_bus_type();
}
module_exit(idxd_exit_module);
