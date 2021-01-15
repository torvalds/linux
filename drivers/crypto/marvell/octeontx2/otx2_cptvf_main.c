// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptvf.h"
#include <rvu_reg.h>

#define OTX2_CPTVF_DRV_NAME "octeontx2-cptvf"

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
	u32 num_vec;

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
	int ret;

	cptvf->pfvf_mbox_wq = alloc_workqueue("cpt_pfvf_mailbox",
					      WQ_UNBOUND | WQ_HIGHPRI |
					      WQ_MEM_RECLAIM, 1);
	if (!cptvf->pfvf_mbox_wq)
		return -ENOMEM;

	ret = otx2_mbox_init(&cptvf->pfvf_mbox, cptvf->pfvf_mbox_base,
			     cptvf->pdev, cptvf->reg_base, MBOX_DIR_VFPF, 1);
	if (ret)
		goto free_wqe;

	INIT_WORK(&cptvf->pfvf_mbox_work, otx2_cptvf_pfvf_mbox_handler);
	return 0;

free_wqe:
	destroy_workqueue(cptvf->pfvf_mbox_wq);
	return ret;
}

static void cptvf_pfvf_mbox_destroy(struct otx2_cptvf_dev *cptvf)
{
	destroy_workqueue(cptvf->pfvf_mbox_wq);
	otx2_mbox_destroy(&cptvf->pfvf_mbox);
}

static int otx2_cptvf_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	resource_size_t offset, size;
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

	offset = pci_resource_start(pdev, PCI_MBOX_BAR_NUM);
	size = pci_resource_len(pdev, PCI_MBOX_BAR_NUM);
	/* Map PF-VF mailbox memory */
	cptvf->pfvf_mbox_base = devm_ioremap_wc(dev, offset, size);
	if (!cptvf->pfvf_mbox_base) {
		dev_err(&pdev->dev, "Unable to map BAR4\n");
		ret = -ENODEV;
		goto clear_drvdata;
	}
	/* Initialize PF<=>VF mailbox */
	ret = cptvf_pfvf_mbox_init(cptvf);
	if (ret)
		goto clear_drvdata;

	/* Register interrupts */
	ret = cptvf_register_interrupts(cptvf);
	if (ret)
		goto destroy_pfvf_mbox;

	return 0;

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
	/* Disable PF-VF mailbox interrupt */
	cptvf_disable_pfvf_mbox_intrs(cptvf);
	/* Destroy PF-VF mbox */
	cptvf_pfvf_mbox_destroy(cptvf);
	pci_set_drvdata(pdev, NULL);
}

/* Supported devices */
static const struct pci_device_id otx2_cptvf_id_table[] = {
	{PCI_VDEVICE(CAVIUM, OTX2_CPT_PCI_VF_DEVICE_ID), 0},
	{ 0, }  /* end of table */
};

static struct pci_driver otx2_cptvf_pci_driver = {
	.name = OTX2_CPTVF_DRV_NAME,
	.id_table = otx2_cptvf_id_table,
	.probe = otx2_cptvf_probe,
	.remove = otx2_cptvf_remove,
};

module_pci_driver(otx2_cptvf_pci_driver);

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell OcteonTX2 CPT Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, otx2_cptvf_id_table);
