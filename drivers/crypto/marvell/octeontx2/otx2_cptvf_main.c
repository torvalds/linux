// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptvf.h"
#include "otx2_cptlf.h"
#include "otx2_cptvf_algs.h"
#include "cn10k_cpt.h"
#include <rvu_reg.h>

#define OTX2_CPTVF_DRV_NAME "rvu_cptvf"

static void cptvf_enable_pfvf_mbox_intrs(struct otx2_cptvf_dev *cptvf)
{
	/* Clear interrupt if any */
	otx2_cpt_write64(cptvf->reg_base, BLKADDR_RVUM, 0, OTX2_RVU_VF_INT,
			 0x1ULL);

	/* Enable PF-VF interrupt */
	otx2_cpt_write64(cptvf->reg_base, BLKADDR_RVUM, 0,
			 OTX2_RVU_VF_INT_ENA_W1S, 0x1ULL);
}

static void cptvf_disable_pfvf_mbox_intrs(struct otx2_cptvf_dev *cptvf)
{
	/* Disable PF-VF interrupt */
	otx2_cpt_write64(cptvf->reg_base, BLKADDR_RVUM, 0,
			 OTX2_RVU_VF_INT_ENA_W1C, 0x1ULL);

	/* Clear interrupt if any */
	otx2_cpt_write64(cptvf->reg_base, BLKADDR_RVUM, 0, OTX2_RVU_VF_INT,
			 0x1ULL);
}

static int cptvf_register_interrupts(struct otx2_cptvf_dev *cptvf)
{
	int ret, irq;
	int num_vec;

	num_vec = pci_msix_vec_count(cptvf->pdev);
	if (num_vec <= 0)
		return -EINVAL;

	/* Enable MSI-X */
	ret = pci_alloc_irq_vectors(cptvf->pdev, num_vec, num_vec,
				    PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&cptvf->pdev->dev,
			"Request for %d msix vectors failed\n", num_vec);
		return ret;
	}
	irq = pci_irq_vector(cptvf->pdev, OTX2_CPT_VF_INT_VEC_E_MBOX);
	/* Register VF<=>PF mailbox interrupt handler */
	ret = devm_request_irq(&cptvf->pdev->dev, irq,
			       otx2_cptvf_pfvf_mbox_intr, 0,
			       "CPTPFVF Mbox", cptvf);
	if (ret)
		return ret;
	/* Enable PF-VF mailbox interrupts */
	cptvf_enable_pfvf_mbox_intrs(cptvf);

	ret = otx2_cpt_send_ready_msg(&cptvf->pfvf_mbox, cptvf->pdev);
	if (ret) {
		dev_warn(&cptvf->pdev->dev,
			 "PF not responding to mailbox, deferring probe\n");
		cptvf_disable_pfvf_mbox_intrs(cptvf);
		return -EPROBE_DEFER;
	}
	return 0;
}

static int cptvf_pfvf_mbox_init(struct otx2_cptvf_dev *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	resource_size_t offset, size;
	int ret;

	cptvf->pfvf_mbox_wq =
		alloc_ordered_workqueue("cpt_pfvf_mailbox",
					WQ_HIGHPRI | WQ_MEM_RECLAIM);
	if (!cptvf->pfvf_mbox_wq)
		return -ENOMEM;

	if (test_bit(CN10K_MBOX, &cptvf->cap_flag)) {
		/* For cn10k platform, VF mailbox region is in its BAR2
		 * register space
		 */
		cptvf->pfvf_mbox_base = cptvf->reg_base +
					CN10K_CPT_VF_MBOX_REGION;
	} else {
		offset = pci_resource_start(pdev, PCI_MBOX_BAR_NUM);
		size = pci_resource_len(pdev, PCI_MBOX_BAR_NUM);
		/* Map PF-VF mailbox memory */
		cptvf->pfvf_mbox_base = devm_ioremap_wc(&pdev->dev, offset,
							size);
		if (!cptvf->pfvf_mbox_base) {
			dev_err(&pdev->dev, "Unable to map BAR4\n");
			ret = -ENOMEM;
			goto free_wqe;
		}
	}

	ret = otx2_mbox_init(&cptvf->pfvf_mbox, cptvf->pfvf_mbox_base,
			     pdev, cptvf->reg_base, MBOX_DIR_VFPF, 1);
	if (ret)
		goto free_wqe;

	ret = otx2_cpt_mbox_bbuf_init(cptvf, pdev);
	if (ret)
		goto destroy_mbox;

	INIT_WORK(&cptvf->pfvf_mbox_work, otx2_cptvf_pfvf_mbox_handler);
	return 0;

destroy_mbox:
	otx2_mbox_destroy(&cptvf->pfvf_mbox);
free_wqe:
	destroy_workqueue(cptvf->pfvf_mbox_wq);
	return ret;
}

static void cptvf_pfvf_mbox_destroy(struct otx2_cptvf_dev *cptvf)
{
	destroy_workqueue(cptvf->pfvf_mbox_wq);
	otx2_mbox_destroy(&cptvf->pfvf_mbox);
}

static void cptlf_work_handler(unsigned long data)
{
	otx2_cpt_post_process((struct otx2_cptlf_wqe *) data);
}

static void cleanup_tasklet_work(struct otx2_cptlfs_info *lfs)
{
	int i;

	for (i = 0; i <  lfs->lfs_num; i++) {
		if (!lfs->lf[i].wqe)
			continue;

		tasklet_kill(&lfs->lf[i].wqe->work);
		kfree(lfs->lf[i].wqe);
		lfs->lf[i].wqe = NULL;
	}
}

static int init_tasklet_work(struct otx2_cptlfs_info *lfs)
{
	struct otx2_cptlf_wqe *wqe;
	int i, ret = 0;

	for (i = 0; i < lfs->lfs_num; i++) {
		wqe = kzalloc(sizeof(struct otx2_cptlf_wqe), GFP_KERNEL);
		if (!wqe) {
			ret = -ENOMEM;
			goto cleanup_tasklet;
		}

		tasklet_init(&wqe->work, cptlf_work_handler, (u64) wqe);
		wqe->lfs = lfs;
		wqe->lf_num = i;
		lfs->lf[i].wqe = wqe;
	}
	return 0;

cleanup_tasklet:
	cleanup_tasklet_work(lfs);
	return ret;
}

static void free_pending_queues(struct otx2_cptlfs_info *lfs)
{
	int i;

	for (i = 0; i < lfs->lfs_num; i++) {
		kfree(lfs->lf[i].pqueue.head);
		lfs->lf[i].pqueue.head = NULL;
	}
}

static int alloc_pending_queues(struct otx2_cptlfs_info *lfs)
{
	int size, ret, i;

	if (!lfs->lfs_num)
		return -EINVAL;

	for (i = 0; i < lfs->lfs_num; i++) {
		lfs->lf[i].pqueue.qlen = OTX2_CPT_INST_QLEN_MSGS;
		size = lfs->lf[i].pqueue.qlen *
		       sizeof(struct otx2_cpt_pending_entry);

		lfs->lf[i].pqueue.head = kzalloc(size, GFP_KERNEL);
		if (!lfs->lf[i].pqueue.head) {
			ret = -ENOMEM;
			goto error;
		}

		/* Initialize spin lock */
		spin_lock_init(&lfs->lf[i].pqueue.lock);
	}
	return 0;

error:
	free_pending_queues(lfs);
	return ret;
}

static void lf_sw_cleanup(struct otx2_cptlfs_info *lfs)
{
	cleanup_tasklet_work(lfs);
	free_pending_queues(lfs);
}

static int lf_sw_init(struct otx2_cptlfs_info *lfs)
{
	int ret;

	ret = alloc_pending_queues(lfs);
	if (ret) {
		dev_err(&lfs->pdev->dev,
			"Allocating pending queues failed\n");
		return ret;
	}
	ret = init_tasklet_work(lfs);
	if (ret) {
		dev_err(&lfs->pdev->dev,
			"Tasklet work init failed\n");
		goto pending_queues_free;
	}
	return 0;

pending_queues_free:
	free_pending_queues(lfs);
	return ret;
}

static void cptvf_lf_shutdown(struct otx2_cptlfs_info *lfs)
{
	atomic_set(&lfs->state, OTX2_CPTLF_IN_RESET);

	/* Remove interrupts affinity */
	otx2_cptlf_free_irqs_affinity(lfs);
	/* Disable instruction queue */
	otx2_cptlf_disable_iqueues(lfs);
	/* Unregister crypto algorithms */
	otx2_cpt_crypto_exit(lfs->pdev, THIS_MODULE);
	/* Unregister LFs interrupts */
	otx2_cptlf_unregister_misc_interrupts(lfs);
	otx2_cptlf_unregister_done_interrupts(lfs);
	/* Cleanup LFs software side */
	lf_sw_cleanup(lfs);
	/* Free instruction queues */
	otx2_cpt_free_instruction_queues(lfs);
	/* Send request to detach LFs */
	otx2_cpt_detach_rsrcs_msg(lfs);
	lfs->lfs_num = 0;
}

static int cptvf_lf_init(struct otx2_cptvf_dev *cptvf)
{
	struct otx2_cptlfs_info *lfs = &cptvf->lfs;
	struct device *dev = &cptvf->pdev->dev;
	int ret, lfs_num;
	u8 eng_grp_msk;

	/* Get engine group number for symmetric crypto */
	cptvf->lfs.kcrypto_eng_grp_num = OTX2_CPT_INVALID_CRYPTO_ENG_GRP;
	ret = otx2_cptvf_send_eng_grp_num_msg(cptvf, OTX2_CPT_SE_TYPES);
	if (ret)
		return ret;

	if (cptvf->lfs.kcrypto_eng_grp_num == OTX2_CPT_INVALID_CRYPTO_ENG_GRP) {
		dev_err(dev, "Engine group for kernel crypto not available\n");
		ret = -ENOENT;
		return ret;
	}
	eng_grp_msk = 1 << cptvf->lfs.kcrypto_eng_grp_num;

	ret = otx2_cptvf_send_kvf_limits_msg(cptvf);
	if (ret)
		return ret;

	lfs_num = cptvf->lfs.kvf_limits;

	otx2_cptlf_set_dev_info(lfs, cptvf->pdev, cptvf->reg_base,
				&cptvf->pfvf_mbox, cptvf->blkaddr);
	ret = otx2_cptlf_init(lfs, eng_grp_msk, OTX2_CPT_QUEUE_HI_PRIO,
			      lfs_num);
	if (ret)
		return ret;

	/* Get msix offsets for attached LFs */
	ret = otx2_cpt_msix_offset_msg(lfs);
	if (ret)
		goto cleanup_lf;

	/* Initialize LFs software side */
	ret = lf_sw_init(lfs);
	if (ret)
		goto cleanup_lf;

	/* Register LFs interrupts */
	ret = otx2_cptlf_register_misc_interrupts(lfs);
	if (ret)
		goto cleanup_lf_sw;

	ret = otx2_cptlf_register_done_interrupts(lfs);
	if (ret)
		goto cleanup_lf_sw;

	/* Set interrupts affinity */
	ret = otx2_cptlf_set_irqs_affinity(lfs);
	if (ret)
		goto unregister_intr;

	atomic_set(&lfs->state, OTX2_CPTLF_STARTED);
	/* Register crypto algorithms */
	ret = otx2_cpt_crypto_init(lfs->pdev, THIS_MODULE, lfs_num, 1);
	if (ret) {
		dev_err(&lfs->pdev->dev, "algorithms registration failed\n");
		goto disable_irqs;
	}
	return 0;

disable_irqs:
	otx2_cptlf_free_irqs_affinity(lfs);
unregister_intr:
	otx2_cptlf_unregister_misc_interrupts(lfs);
	otx2_cptlf_unregister_done_interrupts(lfs);
cleanup_lf_sw:
	lf_sw_cleanup(lfs);
cleanup_lf:
	otx2_cptlf_shutdown(lfs);

	return ret;
}

static int otx2_cptvf_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct otx2_cptvf_dev *cptvf;
	int ret;

	cptvf = devm_kzalloc(dev, sizeof(*cptvf), GFP_KERNEL);
	if (!cptvf)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto clear_drvdata;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret) {
		dev_err(dev, "Unable to get usable DMA configuration\n");
		goto clear_drvdata;
	}
	/* Map VF's configuration registers */
	ret = pcim_iomap_regions_request_all(pdev, 1 << PCI_PF_REG_BAR_NUM,
					     OTX2_CPTVF_DRV_NAME);
	if (ret) {
		dev_err(dev, "Couldn't get PCI resources 0x%x\n", ret);
		goto clear_drvdata;
	}
	pci_set_master(pdev);
	pci_set_drvdata(pdev, cptvf);
	cptvf->pdev = pdev;

	cptvf->reg_base = pcim_iomap_table(pdev)[PCI_PF_REG_BAR_NUM];

	otx2_cpt_set_hw_caps(pdev, &cptvf->cap_flag);

	ret = cn10k_cptvf_lmtst_init(cptvf);
	if (ret)
		goto clear_drvdata;

	/* Initialize PF<=>VF mailbox */
	ret = cptvf_pfvf_mbox_init(cptvf);
	if (ret)
		goto clear_drvdata;

	/* Register interrupts */
	ret = cptvf_register_interrupts(cptvf);
	if (ret)
		goto destroy_pfvf_mbox;

	cptvf->blkaddr = BLKADDR_CPT0;

	cptvf_hw_ops_get(cptvf);

	ret = otx2_cptvf_send_caps_msg(cptvf);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't get CPT engine capabilities.\n");
		goto unregister_interrupts;
	}
	if (cptvf->eng_caps[OTX2_CPT_SE_TYPES] & BIT_ULL(35))
		cptvf->lfs.ops->cpt_sg_info_create = cn10k_sgv2_info_create;

	/* Initialize CPT LFs */
	ret = cptvf_lf_init(cptvf);
	if (ret)
		goto unregister_interrupts;

	return 0;

unregister_interrupts:
	cptvf_disable_pfvf_mbox_intrs(cptvf);
destroy_pfvf_mbox:
	cptvf_pfvf_mbox_destroy(cptvf);
clear_drvdata:
	pci_set_drvdata(pdev, NULL);

	return ret;
}

static void otx2_cptvf_remove(struct pci_dev *pdev)
{
	struct otx2_cptvf_dev *cptvf = pci_get_drvdata(pdev);

	if (!cptvf) {
		dev_err(&pdev->dev, "Invalid CPT VF device.\n");
		return;
	}
	cptvf_lf_shutdown(&cptvf->lfs);
	/* Disable PF-VF mailbox interrupt */
	cptvf_disable_pfvf_mbox_intrs(cptvf);
	/* Destroy PF-VF mbox */
	cptvf_pfvf_mbox_destroy(cptvf);
	pci_set_drvdata(pdev, NULL);
}

/* Supported devices */
static const struct pci_device_id otx2_cptvf_id_table[] = {
	{PCI_VDEVICE(CAVIUM, OTX2_CPT_PCI_VF_DEVICE_ID), 0},
	{PCI_VDEVICE(CAVIUM, CN10K_CPT_PCI_VF_DEVICE_ID), 0},
	{ 0, }  /* end of table */
};

static struct pci_driver otx2_cptvf_pci_driver = {
	.name = OTX2_CPTVF_DRV_NAME,
	.id_table = otx2_cptvf_id_table,
	.probe = otx2_cptvf_probe,
	.remove = otx2_cptvf_remove,
};

module_pci_driver(otx2_cptvf_pci_driver);

MODULE_IMPORT_NS(CRYPTO_DEV_OCTEONTX2_CPT);

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell RVU CPT Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, otx2_cptvf_id_table);
