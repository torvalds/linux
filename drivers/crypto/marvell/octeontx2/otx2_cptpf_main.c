// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include <linux/firmware.h>
#include "otx2_cpt_hw_types.h"
#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "rvu_reg.h"

#define OTX2_CPT_DRV_NAME    "octeontx2-cpt"
#define OTX2_CPT_DRV_STRING  "Marvell OcteonTX2 CPT Physical Function Driver"

static void cptpf_disable_afpf_mbox_intr(struct otx2_cptpf_dev *cptpf)
{
	/* Disable AF-PF interrupt */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT_ENA_W1C,
			 0x1ULL);
	/* Clear interrupt if any */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT, 0x1ULL);
}

static int cptpf_register_afpf_mbox_intr(struct otx2_cptpf_dev *cptpf)
{
	struct pci_dev *pdev = cptpf->pdev;
	struct device *dev = &pdev->dev;
	int ret, irq;

	irq = pci_irq_vector(pdev, RVU_PF_INT_VEC_AFPF_MBOX);
	/* Register AF-PF mailbox interrupt handler */
	ret = devm_request_irq(dev, irq, otx2_cptpf_afpf_mbox_intr, 0,
			       "CPTAFPF Mbox", cptpf);
	if (ret) {
		dev_err(dev,
			"IRQ registration failed for PFAF mbox irq\n");
		return ret;
	}
	/* Clear interrupt if any, to avoid spurious interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT, 0x1ULL);
	/* Enable AF-PF interrupt */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT_ENA_W1S,
			 0x1ULL);

	ret = otx2_cpt_send_ready_msg(&cptpf->afpf_mbox, cptpf->pdev);
	if (ret) {
		dev_warn(dev,
			 "AF not responding to mailbox, deferring probe\n");
		cptpf_disable_afpf_mbox_intr(cptpf);
		return -EPROBE_DEFER;
	}
	return 0;
}

static int cptpf_afpf_mbox_init(struct otx2_cptpf_dev *cptpf)
{
	int err;

	cptpf->afpf_mbox_wq = alloc_workqueue("cpt_afpf_mailbox",
					      WQ_UNBOUND | WQ_HIGHPRI |
					      WQ_MEM_RECLAIM, 1);
	if (!cptpf->afpf_mbox_wq)
		return -ENOMEM;

	err = otx2_mbox_init(&cptpf->afpf_mbox, cptpf->afpf_mbox_base,
			     cptpf->pdev, cptpf->reg_base, MBOX_DIR_PFAF, 1);
	if (err)
		goto error;

	INIT_WORK(&cptpf->afpf_mbox_work, otx2_cptpf_afpf_mbox_handler);
	return 0;

error:
	destroy_workqueue(cptpf->afpf_mbox_wq);
	return err;
}

static void cptpf_afpf_mbox_destroy(struct otx2_cptpf_dev *cptpf)
{
	destroy_workqueue(cptpf->afpf_mbox_wq);
	otx2_mbox_destroy(&cptpf->afpf_mbox);
}

static int cpt_is_pf_usable(struct otx2_cptpf_dev *cptpf)
{
	u64 rev;

	rev = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0,
			      RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_RVUM));
	rev = (rev >> 12) & 0xFF;
	/*
	 * Check if AF has setup revision for RVUM block, otherwise
	 * driver probe should be deferred until AF driver comes up
	 */
	if (!rev) {
		dev_warn(&cptpf->pdev->dev,
			 "AF is not initialized, deferring probe\n");
		return -EPROBE_DEFER;
	}
	return 0;
}

static int otx2_cptpf_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	resource_size_t offset, size;
	struct otx2_cptpf_dev *cptpf;
	int err;

	cptpf = devm_kzalloc(dev, sizeof(*cptpf), GFP_KERNEL);
	if (!cptpf)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto clear_drvdata;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable DMA configuration\n");
		goto clear_drvdata;
	}
	/* Map PF's configuration registers */
	err = pcim_iomap_regions_request_all(pdev, 1 << PCI_PF_REG_BAR_NUM,
					     OTX2_CPT_DRV_NAME);
	if (err) {
		dev_err(dev, "Couldn't get PCI resources 0x%x\n", err);
		goto clear_drvdata;
	}
	pci_set_master(pdev);
	pci_set_drvdata(pdev, cptpf);
	cptpf->pdev = pdev;

	cptpf->reg_base = pcim_iomap_table(pdev)[PCI_PF_REG_BAR_NUM];

	/* Check if AF driver is up, otherwise defer probe */
	err = cpt_is_pf_usable(cptpf);
	if (err)
		goto clear_drvdata;

	offset = pci_resource_start(pdev, PCI_MBOX_BAR_NUM);
	size = pci_resource_len(pdev, PCI_MBOX_BAR_NUM);
	/* Map AF-PF mailbox memory */
	cptpf->afpf_mbox_base = devm_ioremap_wc(dev, offset, size);
	if (!cptpf->afpf_mbox_base) {
		dev_err(&pdev->dev, "Unable to map BAR4\n");
		err = -ENODEV;
		goto clear_drvdata;
	}
	err = pci_alloc_irq_vectors(pdev, RVU_PF_INT_VEC_CNT,
				    RVU_PF_INT_VEC_CNT, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "Request for %d msix vectors failed\n",
			RVU_PF_INT_VEC_CNT);
		goto clear_drvdata;
	}
	/* Initialize AF-PF mailbox */
	err = cptpf_afpf_mbox_init(cptpf);
	if (err)
		goto clear_drvdata;
	/* Register mailbox interrupt */
	err = cptpf_register_afpf_mbox_intr(cptpf);
	if (err)
		goto destroy_afpf_mbox;

	return 0;

destroy_afpf_mbox:
	cptpf_afpf_mbox_destroy(cptpf);
clear_drvdata:
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void otx2_cptpf_remove(struct pci_dev *pdev)
{
	struct otx2_cptpf_dev *cptpf = pci_get_drvdata(pdev);

	if (!cptpf)
		return;
	/* Disable AF-PF mailbox interrupt */
	cptpf_disable_afpf_mbox_intr(cptpf);
	/* Destroy AF-PF mbox */
	cptpf_afpf_mbox_destroy(cptpf);
	pci_set_drvdata(pdev, NULL);
}

/* Supported devices */
static const struct pci_device_id otx2_cpt_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OTX2_CPT_PCI_PF_DEVICE_ID) },
	{ 0, }  /* end of table */
};

static struct pci_driver otx2_cpt_pci_driver = {
	.name = OTX2_CPT_DRV_NAME,
	.id_table = otx2_cpt_id_table,
	.probe = otx2_cptpf_probe,
	.remove = otx2_cptpf_remove,
};

module_pci_driver(otx2_cpt_pci_driver);

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION(OTX2_CPT_DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, otx2_cpt_id_table);
