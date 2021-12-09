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
#include <linux/intel-svm.h>
#include <linux/iommu.h>
#include <uapi/linux/idxd.h>
#include <linux/dmaengine.h>
#include "../dmaengine.h"
#include "registers.h"
#include "idxd.h"
#include "perfmon.h"

MODULE_VERSION(IDXD_DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
MODULE_IMPORT_NS(IDXD);

static bool sva = true;
module_param(sva, bool, 0644);
MODULE_PARM_DESC(sva, "Toggle SVA support on/off");

bool tc_override;
module_param(tc_override, bool, 0644);
MODULE_PARM_DESC(tc_override, "Override traffic class defaults");

#define DRV_NAME "idxd"

bool support_enqcmd;
DEFINE_IDA(idxd_ida);

static struct idxd_driver_data idxd_driver_data[] = {
	[IDXD_TYPE_DSA] = {
		.name_prefix = "dsa",
		.type = IDXD_TYPE_DSA,
		.compl_size = sizeof(struct dsa_completion_record),
		.align = 32,
		.dev_type = &dsa_device_type,
	},
	[IDXD_TYPE_IAX] = {
		.name_prefix = "iax",
		.type = IDXD_TYPE_IAX,
		.compl_size = sizeof(struct iax_completion_record),
		.align = 64,
		.dev_type = &iax_device_type,
	},
};

static struct pci_device_id idxd_pci_tbl[] = {
	/* DSA ver 1.0 platforms */
	{ PCI_DEVICE_DATA(INTEL, DSA_SPR0, &idxd_driver_data[IDXD_TYPE_DSA]) },

	/* IAX ver 1.0 platforms */
	{ PCI_DEVICE_DATA(INTEL, IAX_SPR0, &idxd_driver_data[IDXD_TYPE_IAX]) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, idxd_pci_tbl);

static int idxd_setup_interrupts(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	struct idxd_irq_entry *irq_entry;
	int i, msixcnt;
	int rc = 0;

	msixcnt = pci_msix_vec_count(pdev);
	if (msixcnt < 0) {
		dev_err(dev, "Not MSI-X interrupt capable.\n");
		return -ENOSPC;
	}

	rc = pci_alloc_irq_vectors(pdev, msixcnt, msixcnt, PCI_IRQ_MSIX);
	if (rc != msixcnt) {
		dev_err(dev, "Failed enabling %d MSIX entries: %d\n", msixcnt, rc);
		return -ENOSPC;
	}
	dev_dbg(dev, "Enabled %d msix vectors\n", msixcnt);

	/*
	 * We implement 1 completion list per MSI-X entry except for
	 * entry 0, which is for errors and others.
	 */
	idxd->irq_entries = kcalloc_node(msixcnt, sizeof(struct idxd_irq_entry),
					 GFP_KERNEL, dev_to_node(dev));
	if (!idxd->irq_entries) {
		rc = -ENOMEM;
		goto err_irq_entries;
	}

	for (i = 0; i < msixcnt; i++) {
		idxd->irq_entries[i].id = i;
		idxd->irq_entries[i].idxd = idxd;
		idxd->irq_entries[i].vector = pci_irq_vector(pdev, i);
		spin_lock_init(&idxd->irq_entries[i].list_lock);
	}

	idxd_msix_perm_setup(idxd);

	irq_entry = &idxd->irq_entries[0];
	rc = request_threaded_irq(irq_entry->vector, NULL, idxd_misc_thread,
				  0, "idxd-misc", irq_entry);
	if (rc < 0) {
		dev_err(dev, "Failed to allocate misc interrupt.\n");
		goto err_misc_irq;
	}

	dev_dbg(dev, "Allocated idxd-misc handler on msix vector %d\n", irq_entry->vector);

	/* first MSI-X entry is not for wq interrupts */
	idxd->num_wq_irqs = msixcnt - 1;

	for (i = 1; i < msixcnt; i++) {
		irq_entry = &idxd->irq_entries[i];

		init_llist_head(&idxd->irq_entries[i].pending_llist);
		INIT_LIST_HEAD(&idxd->irq_entries[i].work_list);
		rc = request_threaded_irq(irq_entry->vector, NULL,
					  idxd_wq_thread, 0, "idxd-portal", irq_entry);
		if (rc < 0) {
			dev_err(dev, "Failed to allocate irq %d.\n", irq_entry->vector);
			goto err_wq_irqs;
		}

		dev_dbg(dev, "Allocated idxd-msix %d for vector %d\n", i, irq_entry->vector);
		if (idxd->hw.cmd_cap & BIT(IDXD_CMD_REQUEST_INT_HANDLE)) {
			/*
			 * The MSIX vector enumeration starts at 1 with vector 0 being the
			 * misc interrupt that handles non I/O completion events. The
			 * interrupt handles are for IMS enumeration on guest. The misc
			 * interrupt vector does not require a handle and therefore we start
			 * the int_handles at index 0. Since 'i' starts at 1, the first
			 * int_handles index will be 0.
			 */
			rc = idxd_device_request_int_handle(idxd, i, &idxd->int_handles[i - 1],
							    IDXD_IRQ_MSIX);
			if (rc < 0) {
				free_irq(irq_entry->vector, irq_entry);
				goto err_wq_irqs;
			}
			dev_dbg(dev, "int handle requested: %u\n", idxd->int_handles[i - 1]);
		}
	}

	idxd_unmask_error_interrupts(idxd);
	return 0;

 err_wq_irqs:
	while (--i >= 0) {
		irq_entry = &idxd->irq_entries[i];
		free_irq(irq_entry->vector, irq_entry);
		if (i != 0)
			idxd_device_release_int_handle(idxd,
						       idxd->int_handles[i], IDXD_IRQ_MSIX);
	}
 err_misc_irq:
	/* Disable error interrupt generation */
	idxd_mask_error_interrupts(idxd);
	idxd_msix_perm_clear(idxd);
 err_irq_entries:
	pci_free_irq_vectors(pdev);
	dev_err(dev, "No usable interrupts\n");
	return rc;
}

static void idxd_cleanup_interrupts(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	struct idxd_irq_entry *irq_entry;
	int i, msixcnt;

	msixcnt = pci_msix_vec_count(pdev);
	if (msixcnt <= 0)
		return;

	irq_entry = &idxd->irq_entries[0];
	free_irq(irq_entry->vector, irq_entry);

	for (i = 1; i < msixcnt; i++) {

		irq_entry = &idxd->irq_entries[i];
		if (idxd->hw.cmd_cap & BIT(IDXD_CMD_RELEASE_INT_HANDLE))
			idxd_device_release_int_handle(idxd, idxd->int_handles[i],
						       IDXD_IRQ_MSIX);
		free_irq(irq_entry->vector, irq_entry);
	}

	idxd_mask_error_interrupts(idxd);
	pci_free_irq_vectors(pdev);
}

static int idxd_setup_wqs(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	struct idxd_wq *wq;
	struct device *conf_dev;
	int i, rc;

	idxd->wqs = kcalloc_node(idxd->max_wqs, sizeof(struct idxd_wq *),
				 GFP_KERNEL, dev_to_node(dev));
	if (!idxd->wqs)
		return -ENOMEM;

	for (i = 0; i < idxd->max_wqs; i++) {
		wq = kzalloc_node(sizeof(*wq), GFP_KERNEL, dev_to_node(dev));
		if (!wq) {
			rc = -ENOMEM;
			goto err;
		}

		idxd_dev_set_type(&wq->idxd_dev, IDXD_DEV_WQ);
		conf_dev = wq_confdev(wq);
		wq->id = i;
		wq->idxd = idxd;
		device_initialize(wq_confdev(wq));
		conf_dev->parent = idxd_confdev(idxd);
		conf_dev->bus = &dsa_bus_type;
		conf_dev->type = &idxd_wq_device_type;
		rc = dev_set_name(conf_dev, "wq%d.%d", idxd->id, wq->id);
		if (rc < 0) {
			put_device(conf_dev);
			goto err;
		}

		mutex_init(&wq->wq_lock);
		init_waitqueue_head(&wq->err_queue);
		init_completion(&wq->wq_dead);
		wq->max_xfer_bytes = idxd->max_xfer_bytes;
		wq->max_batch_size = idxd->max_batch_size;
		wq->wqcfg = kzalloc_node(idxd->wqcfg_size, GFP_KERNEL, dev_to_node(dev));
		if (!wq->wqcfg) {
			put_device(conf_dev);
			rc = -ENOMEM;
			goto err;
		}
		idxd->wqs[i] = wq;
	}

	return 0;

 err:
	while (--i >= 0) {
		wq = idxd->wqs[i];
		conf_dev = wq_confdev(wq);
		put_device(conf_dev);
	}
	return rc;
}

static int idxd_setup_engines(struct idxd_device *idxd)
{
	struct idxd_engine *engine;
	struct device *dev = &idxd->pdev->dev;
	struct device *conf_dev;
	int i, rc;

	idxd->engines = kcalloc_node(idxd->max_engines, sizeof(struct idxd_engine *),
				     GFP_KERNEL, dev_to_node(dev));
	if (!idxd->engines)
		return -ENOMEM;

	for (i = 0; i < idxd->max_engines; i++) {
		engine = kzalloc_node(sizeof(*engine), GFP_KERNEL, dev_to_node(dev));
		if (!engine) {
			rc = -ENOMEM;
			goto err;
		}

		idxd_dev_set_type(&engine->idxd_dev, IDXD_DEV_ENGINE);
		conf_dev = engine_confdev(engine);
		engine->id = i;
		engine->idxd = idxd;
		device_initialize(conf_dev);
		conf_dev->parent = idxd_confdev(idxd);
		conf_dev->bus = &dsa_bus_type;
		conf_dev->type = &idxd_engine_device_type;
		rc = dev_set_name(conf_dev, "engine%d.%d", idxd->id, engine->id);
		if (rc < 0) {
			put_device(conf_dev);
			goto err;
		}

		idxd->engines[i] = engine;
	}

	return 0;

 err:
	while (--i >= 0) {
		engine = idxd->engines[i];
		conf_dev = engine_confdev(engine);
		put_device(conf_dev);
	}
	return rc;
}

static int idxd_setup_groups(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	struct device *conf_dev;
	struct idxd_group *group;
	int i, rc;

	idxd->groups = kcalloc_node(idxd->max_groups, sizeof(struct idxd_group *),
				    GFP_KERNEL, dev_to_node(dev));
	if (!idxd->groups)
		return -ENOMEM;

	for (i = 0; i < idxd->max_groups; i++) {
		group = kzalloc_node(sizeof(*group), GFP_KERNEL, dev_to_node(dev));
		if (!group) {
			rc = -ENOMEM;
			goto err;
		}

		idxd_dev_set_type(&group->idxd_dev, IDXD_DEV_GROUP);
		conf_dev = group_confdev(group);
		group->id = i;
		group->idxd = idxd;
		device_initialize(conf_dev);
		conf_dev->parent = idxd_confdev(idxd);
		conf_dev->bus = &dsa_bus_type;
		conf_dev->type = &idxd_group_device_type;
		rc = dev_set_name(conf_dev, "group%d.%d", idxd->id, group->id);
		if (rc < 0) {
			put_device(conf_dev);
			goto err;
		}

		idxd->groups[i] = group;
		if (idxd->hw.version < DEVICE_VERSION_2 && !tc_override) {
			group->tc_a = 1;
			group->tc_b = 1;
		} else {
			group->tc_a = -1;
			group->tc_b = -1;
		}
	}

	return 0;

 err:
	while (--i >= 0) {
		group = idxd->groups[i];
		put_device(group_confdev(group));
	}
	return rc;
}

static void idxd_cleanup_internals(struct idxd_device *idxd)
{
	int i;

	for (i = 0; i < idxd->max_groups; i++)
		put_device(group_confdev(idxd->groups[i]));
	for (i = 0; i < idxd->max_engines; i++)
		put_device(engine_confdev(idxd->engines[i]));
	for (i = 0; i < idxd->max_wqs; i++)
		put_device(wq_confdev(idxd->wqs[i]));
	destroy_workqueue(idxd->wq);
}

static int idxd_setup_internals(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int rc, i;

	init_waitqueue_head(&idxd->cmd_waitq);

	if (idxd->hw.cmd_cap & BIT(IDXD_CMD_REQUEST_INT_HANDLE)) {
		idxd->int_handles = kcalloc_node(idxd->max_wqs, sizeof(int), GFP_KERNEL,
						 dev_to_node(dev));
		if (!idxd->int_handles)
			return -ENOMEM;
	}

	rc = idxd_setup_wqs(idxd);
	if (rc < 0)
		goto err_wqs;

	rc = idxd_setup_engines(idxd);
	if (rc < 0)
		goto err_engine;

	rc = idxd_setup_groups(idxd);
	if (rc < 0)
		goto err_group;

	idxd->wq = create_workqueue(dev_name(dev));
	if (!idxd->wq) {
		rc = -ENOMEM;
		goto err_wkq_create;
	}

	return 0;

 err_wkq_create:
	for (i = 0; i < idxd->max_groups; i++)
		put_device(group_confdev(idxd->groups[i]));
 err_group:
	for (i = 0; i < idxd->max_engines; i++)
		put_device(engine_confdev(idxd->engines[i]));
 err_engine:
	for (i = 0; i < idxd->max_wqs; i++)
		put_device(wq_confdev(idxd->wqs[i]));
 err_wqs:
	kfree(idxd->int_handles);
	return rc;
}

static void idxd_read_table_offsets(struct idxd_device *idxd)
{
	union offsets_reg offsets;
	struct device *dev = &idxd->pdev->dev;

	offsets.bits[0] = ioread64(idxd->reg_base + IDXD_TABLE_OFFSET);
	offsets.bits[1] = ioread64(idxd->reg_base + IDXD_TABLE_OFFSET + sizeof(u64));
	idxd->grpcfg_offset = offsets.grpcfg * IDXD_TABLE_MULT;
	dev_dbg(dev, "IDXD Group Config Offset: %#x\n", idxd->grpcfg_offset);
	idxd->wqcfg_offset = offsets.wqcfg * IDXD_TABLE_MULT;
	dev_dbg(dev, "IDXD Work Queue Config Offset: %#x\n", idxd->wqcfg_offset);
	idxd->msix_perm_offset = offsets.msix_perm * IDXD_TABLE_MULT;
	dev_dbg(dev, "IDXD MSIX Permission Offset: %#x\n", idxd->msix_perm_offset);
	idxd->perfmon_offset = offsets.perfmon * IDXD_TABLE_MULT;
	dev_dbg(dev, "IDXD Perfmon Offset: %#x\n", idxd->perfmon_offset);
}

static void idxd_read_caps(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i;

	/* reading generic capabilities */
	idxd->hw.gen_cap.bits = ioread64(idxd->reg_base + IDXD_GENCAP_OFFSET);
	dev_dbg(dev, "gen_cap: %#llx\n", idxd->hw.gen_cap.bits);

	if (idxd->hw.gen_cap.cmd_cap) {
		idxd->hw.cmd_cap = ioread32(idxd->reg_base + IDXD_CMDCAP_OFFSET);
		dev_dbg(dev, "cmd_cap: %#x\n", idxd->hw.cmd_cap);
	}

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

static struct idxd_device *idxd_alloc(struct pci_dev *pdev, struct idxd_driver_data *data)
{
	struct device *dev = &pdev->dev;
	struct device *conf_dev;
	struct idxd_device *idxd;
	int rc;

	idxd = kzalloc_node(sizeof(*idxd), GFP_KERNEL, dev_to_node(dev));
	if (!idxd)
		return NULL;

	conf_dev = idxd_confdev(idxd);
	idxd->pdev = pdev;
	idxd->data = data;
	idxd_dev_set_type(&idxd->idxd_dev, idxd->data->type);
	idxd->id = ida_alloc(&idxd_ida, GFP_KERNEL);
	if (idxd->id < 0)
		return NULL;

	device_initialize(conf_dev);
	conf_dev->parent = dev;
	conf_dev->bus = &dsa_bus_type;
	conf_dev->type = idxd->data->dev_type;
	rc = dev_set_name(conf_dev, "%s%d", idxd->data->name_prefix, idxd->id);
	if (rc < 0) {
		put_device(conf_dev);
		return NULL;
	}

	spin_lock_init(&idxd->dev_lock);
	spin_lock_init(&idxd->cmd_lock);

	return idxd;
}

static int idxd_enable_system_pasid(struct idxd_device *idxd)
{
	int flags;
	unsigned int pasid;
	struct iommu_sva *sva;

	flags = SVM_FLAG_SUPERVISOR_MODE;

	sva = iommu_sva_bind_device(&idxd->pdev->dev, NULL, &flags);
	if (IS_ERR(sva)) {
		dev_warn(&idxd->pdev->dev,
			 "iommu sva bind failed: %ld\n", PTR_ERR(sva));
		return PTR_ERR(sva);
	}

	pasid = iommu_sva_get_pasid(sva);
	if (pasid == IOMMU_PASID_INVALID) {
		iommu_sva_unbind_device(sva);
		return -ENODEV;
	}

	idxd->sva = sva;
	idxd->pasid = pasid;
	dev_dbg(&idxd->pdev->dev, "system pasid: %u\n", pasid);
	return 0;
}

static void idxd_disable_system_pasid(struct idxd_device *idxd)
{

	iommu_sva_unbind_device(idxd->sva);
	idxd->sva = NULL;
}

static int idxd_probe(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	int rc;

	dev_dbg(dev, "%s entered and resetting device\n", __func__);
	rc = idxd_device_init_reset(idxd);
	if (rc < 0)
		return rc;

	dev_dbg(dev, "IDXD reset complete\n");

	if (IS_ENABLED(CONFIG_INTEL_IDXD_SVM) && sva) {
		rc = iommu_dev_enable_feature(dev, IOMMU_DEV_FEAT_SVA);
		if (rc == 0) {
			rc = idxd_enable_system_pasid(idxd);
			if (rc < 0) {
				iommu_dev_disable_feature(dev, IOMMU_DEV_FEAT_SVA);
				dev_warn(dev, "Failed to enable PASID. No SVA support: %d\n", rc);
			} else {
				set_bit(IDXD_FLAG_PASID_ENABLED, &idxd->flags);
			}
		} else {
			dev_warn(dev, "Unable to turn on SVA feature.\n");
		}
	} else if (!sva) {
		dev_warn(dev, "User forced SVA off via module param.\n");
	}

	idxd_read_caps(idxd);
	idxd_read_table_offsets(idxd);

	rc = idxd_setup_internals(idxd);
	if (rc)
		goto err;

	/* If the configs are readonly, then load them from device */
	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags)) {
		dev_dbg(dev, "Loading RO device config\n");
		rc = idxd_device_load_config(idxd);
		if (rc < 0)
			goto err_config;
	}

	rc = idxd_setup_interrupts(idxd);
	if (rc)
		goto err_config;

	dev_dbg(dev, "IDXD interrupt setup complete.\n");

	idxd->major = idxd_cdev_get_major(idxd);

	rc = perfmon_pmu_init(idxd);
	if (rc < 0)
		dev_warn(dev, "Failed to initialize perfmon. No PMU support: %d\n", rc);

	dev_dbg(dev, "IDXD device %d probed successfully\n", idxd->id);
	return 0;

 err_config:
	idxd_cleanup_internals(idxd);
 err:
	if (device_pasid_enabled(idxd))
		idxd_disable_system_pasid(idxd);
	iommu_dev_disable_feature(dev, IOMMU_DEV_FEAT_SVA);
	return rc;
}

static void idxd_cleanup(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;

	perfmon_pmu_remove(idxd);
	idxd_cleanup_interrupts(idxd);
	idxd_cleanup_internals(idxd);
	if (device_pasid_enabled(idxd))
		idxd_disable_system_pasid(idxd);
	iommu_dev_disable_feature(dev, IOMMU_DEV_FEAT_SVA);
}

static int idxd_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct idxd_device *idxd;
	struct idxd_driver_data *data = (struct idxd_driver_data *)id->driver_data;
	int rc;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	dev_dbg(dev, "Alloc IDXD context\n");
	idxd = idxd_alloc(pdev, data);
	if (!idxd) {
		rc = -ENOMEM;
		goto err_idxd_alloc;
	}

	dev_dbg(dev, "Mapping BARs\n");
	idxd->reg_base = pci_iomap(pdev, IDXD_MMIO_BAR, 0);
	if (!idxd->reg_base) {
		rc = -ENOMEM;
		goto err_iomap;
	}

	dev_dbg(dev, "Set DMA masks\n");
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		goto err;

	dev_dbg(dev, "Set PCI master\n");
	pci_set_master(pdev);
	pci_set_drvdata(pdev, idxd);

	idxd->hw.version = ioread32(idxd->reg_base + IDXD_VER_OFFSET);
	rc = idxd_probe(idxd);
	if (rc) {
		dev_err(dev, "Intel(R) IDXD DMA Engine init failed\n");
		goto err;
	}

	rc = idxd_register_devices(idxd);
	if (rc) {
		dev_err(dev, "IDXD sysfs setup failed\n");
		goto err_dev_register;
	}

	dev_info(&pdev->dev, "Intel(R) Accelerator Device (v%x)\n",
		 idxd->hw.version);

	return 0;

 err_dev_register:
	idxd_cleanup(idxd);
 err:
	pci_iounmap(pdev, idxd->reg_base);
 err_iomap:
	put_device(idxd_confdev(idxd));
 err_idxd_alloc:
	pci_disable_device(pdev);
	return rc;
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

void idxd_wqs_quiesce(struct idxd_device *idxd)
{
	struct idxd_wq *wq;
	int i;

	for (i = 0; i < idxd->max_wqs; i++) {
		wq = idxd->wqs[i];
		if (wq->state == IDXD_WQ_ENABLED && wq->type == IDXD_WQT_KERNEL)
			idxd_wq_quiesce(wq);
	}
}

static void idxd_release_int_handles(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i, rc;

	for (i = 0; i < idxd->num_wq_irqs; i++) {
		if (idxd->hw.cmd_cap & BIT(IDXD_CMD_RELEASE_INT_HANDLE)) {
			rc = idxd_device_release_int_handle(idxd, idxd->int_handles[i],
							    IDXD_IRQ_MSIX);
			if (rc < 0)
				dev_warn(dev, "irq handle %d release failed\n",
					 idxd->int_handles[i]);
			else
				dev_dbg(dev, "int handle requested: %u\n", idxd->int_handles[i]);
		}
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
		synchronize_irq(irq_entry->vector);
		if (i == 0)
			continue;
		idxd_flush_pending_llist(irq_entry);
		idxd_flush_work_list(irq_entry);
	}
	flush_workqueue(idxd->wq);
}

static void idxd_remove(struct pci_dev *pdev)
{
	struct idxd_device *idxd = pci_get_drvdata(pdev);
	struct idxd_irq_entry *irq_entry;
	int msixcnt = pci_msix_vec_count(pdev);
	int i;

	idxd_unregister_devices(idxd);
	/*
	 * When ->release() is called for the idxd->conf_dev, it frees all the memory related
	 * to the idxd context. The driver still needs those bits in order to do the rest of
	 * the cleanup. However, we do need to unbound the idxd sub-driver. So take a ref
	 * on the device here to hold off the freeing while allowing the idxd sub-driver
	 * to unbind.
	 */
	get_device(idxd_confdev(idxd));
	device_unregister(idxd_confdev(idxd));
	idxd_shutdown(pdev);
	if (device_pasid_enabled(idxd))
		idxd_disable_system_pasid(idxd);

	for (i = 0; i < msixcnt; i++) {
		irq_entry = &idxd->irq_entries[i];
		free_irq(irq_entry->vector, irq_entry);
	}
	idxd_msix_perm_clear(idxd);
	idxd_release_int_handles(idxd);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, idxd->reg_base);
	iommu_dev_disable_feature(&pdev->dev, IOMMU_DEV_FEAT_SVA);
	pci_disable_device(pdev);
	destroy_workqueue(idxd->wq);
	perfmon_pmu_remove(idxd);
	put_device(idxd_confdev(idxd));
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
	int err;

	/*
	 * If the CPU does not support MOVDIR64B or ENQCMDS, there's no point in
	 * enumerating the device. We can not utilize it.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_MOVDIR64B)) {
		pr_warn("idxd driver failed to load without MOVDIR64B.\n");
		return -ENODEV;
	}

	if (!cpu_feature_enabled(X86_FEATURE_ENQCMD))
		pr_warn("Platform does not have ENQCMD(S) support.\n");
	else
		support_enqcmd = true;

	perfmon_init();

	err = idxd_driver_register(&idxd_drv);
	if (err < 0)
		goto err_idxd_driver_register;

	err = idxd_driver_register(&idxd_dmaengine_drv);
	if (err < 0)
		goto err_idxd_dmaengine_driver_register;

	err = idxd_driver_register(&idxd_user_drv);
	if (err < 0)
		goto err_idxd_user_driver_register;

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
	idxd_driver_unregister(&idxd_user_drv);
err_idxd_user_driver_register:
	idxd_driver_unregister(&idxd_dmaengine_drv);
err_idxd_dmaengine_driver_register:
	idxd_driver_unregister(&idxd_drv);
err_idxd_driver_register:
	return err;
}
module_init(idxd_init_module);

static void __exit idxd_exit_module(void)
{
	idxd_driver_unregister(&idxd_user_drv);
	idxd_driver_unregister(&idxd_dmaengine_drv);
	idxd_driver_unregister(&idxd_drv);
	pci_unregister_driver(&idxd_pci_driver);
	idxd_cdev_remove();
	perfmon_exit();
}
module_exit(idxd_exit_module);
