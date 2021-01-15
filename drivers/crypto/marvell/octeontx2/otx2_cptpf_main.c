// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include <linux/firmware.h>
#include "otx2_cpt_hw_types.h"
#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "rvu_reg.h"

#define OTX2_CPT_DRV_NAME    "octeontx2-cpt"
#define OTX2_CPT_DRV_STRING  "Marvell OcteonTX2 CPT Physical Function Driver"

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

	return 0;

clear_drvdata:
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void otx2_cptpf_remove(struct pci_dev *pdev)
{
	struct otx2_cptpf_dev *cptpf = pci_get_drvdata(pdev);

	if (!cptpf)
		return;

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
