// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "otx_cpt_common.h"
#include "otx_cptpf.h"

#define DRV_NAME	"octeontx-cpt"
#define DRV_VERSION	"1.0"

static void otx_cpt_disable_mbox_interrupts(struct otx_cpt_device *cpt)
{
	/* Disable mbox(0) interrupts for all VFs */
	writeq(~0ull, cpt->reg_base + OTX_CPT_PF_MBOX_ENA_W1CX(0));
}

static void otx_cpt_enable_mbox_interrupts(struct otx_cpt_device *cpt)
{
	/* Enable mbox(0) interrupts for all VFs */
	writeq(~0ull, cpt->reg_base + OTX_CPT_PF_MBOX_ENA_W1SX(0));
}

static irqreturn_t otx_cpt_mbx0_intr_handler(int __always_unused irq,
					     void *cpt)
{
	otx_cpt_mbox_intr_handler(cpt, 0);

	return IRQ_HANDLED;
}

static void otx_cpt_reset(struct otx_cpt_device *cpt)
{
	writeq(1, cpt->reg_base + OTX_CPT_PF_RESET);
}

static void otx_cpt_find_max_enabled_cores(struct otx_cpt_device *cpt)
{
	union otx_cptx_pf_constants pf_cnsts = {0};

	pf_cnsts.u = readq(cpt->reg_base + OTX_CPT_PF_CONSTANTS);
	cpt->eng_grps.avail.max_se_cnt = pf_cnsts.s.se;
	cpt->eng_grps.avail.max_ae_cnt = pf_cnsts.s.ae;
}

static u32 otx_cpt_check_bist_status(struct otx_cpt_device *cpt)
{
	union otx_cptx_pf_bist_status bist_sts = {0};

	bist_sts.u = readq(cpt->reg_base + OTX_CPT_PF_BIST_STATUS);
	return bist_sts.u;
}

static u64 otx_cpt_check_exe_bist_status(struct otx_cpt_device *cpt)
{
	union otx_cptx_pf_exe_bist_status bist_sts = {0};

	bist_sts.u = readq(cpt->reg_base + OTX_CPT_PF_EXE_BIST_STATUS);
	return bist_sts.u;
}

static int otx_cpt_device_init(struct otx_cpt_device *cpt)
{
	struct device *dev = &cpt->pdev->dev;
	u16 sdevid;
	u64 bist;

	/* Reset the PF when probed first */
	otx_cpt_reset(cpt);
	mdelay(100);

	pci_read_config_word(cpt->pdev, PCI_SUBSYSTEM_ID, &sdevid);

	/* Check BIST status */
	bist = (u64)otx_cpt_check_bist_status(cpt);
	if (bist) {
		dev_err(dev, "RAM BIST failed with code 0x%llx\n", bist);
		return -ENODEV;
	}

	bist = otx_cpt_check_exe_bist_status(cpt);
	if (bist) {
		dev_err(dev, "Engine BIST failed with code 0x%llx\n", bist);
		return -ENODEV;
	}

	/* Get max enabled cores */
	otx_cpt_find_max_enabled_cores(cpt);

	if ((sdevid == OTX_CPT_PCI_PF_SUBSYS_ID) &&
	    (cpt->eng_grps.avail.max_se_cnt == 0)) {
		cpt->pf_type = OTX_CPT_AE;
	} else if ((sdevid == OTX_CPT_PCI_PF_SUBSYS_ID) &&
		   (cpt->eng_grps.avail.max_ae_cnt == 0)) {
		cpt->pf_type = OTX_CPT_SE;
	}

	/* Get max VQs/VFs supported by the device */
	cpt->max_vfs = pci_sriov_get_totalvfs(cpt->pdev);

	/* Disable all cores */
	otx_cpt_disable_all_cores(cpt);

	return 0;
}

static int otx_cpt_register_interrupts(struct otx_cpt_device *cpt)
{
	struct device *dev = &cpt->pdev->dev;
	u32 mbox_int_idx = OTX_CPT_PF_MBOX_INT;
	u32 num_vec = OTX_CPT_PF_MSIX_VECTORS;
	int ret;

	/* Enable MSI-X */
	ret = pci_alloc_irq_vectors(cpt->pdev, num_vec, num_vec, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&cpt->pdev->dev,
			"Request for #%d msix vectors failed\n",
			num_vec);
		return ret;
	}

	/* Register mailbox interrupt handlers */
	ret = request_irq(pci_irq_vector(cpt->pdev,
				OTX_CPT_PF_INT_VEC_E_MBOXX(mbox_int_idx, 0)),
				otx_cpt_mbx0_intr_handler, 0, "CPT Mbox0", cpt);
	if (ret) {
		dev_err(dev, "Request irq failed\n");
		pci_free_irq_vectors(cpt->pdev);
		return ret;
	}
	/* Enable mailbox interrupt */
	otx_cpt_enable_mbox_interrupts(cpt);
	return 0;
}

static void otx_cpt_unregister_interrupts(struct otx_cpt_device *cpt)
{
	u32 mbox_int_idx = OTX_CPT_PF_MBOX_INT;

	otx_cpt_disable_mbox_interrupts(cpt);
	free_irq(pci_irq_vector(cpt->pdev,
				OTX_CPT_PF_INT_VEC_E_MBOXX(mbox_int_idx, 0)),
				cpt);
	pci_free_irq_vectors(cpt->pdev);
}


static int otx_cpt_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	struct otx_cpt_device *cpt = pci_get_drvdata(pdev);
	int ret = 0;

	if (numvfs > cpt->max_vfs)
		numvfs = cpt->max_vfs;

	if (numvfs > 0) {
		ret = otx_cpt_try_create_default_eng_grps(cpt->pdev,
							  &cpt->eng_grps,
							  cpt->pf_type);
		if (ret)
			return ret;

		cpt->vfs_enabled = numvfs;
		ret = pci_enable_sriov(pdev, numvfs);
		if (ret) {
			cpt->vfs_enabled = 0;
			return ret;
		}
		otx_cpt_set_eng_grps_is_rdonly(&cpt->eng_grps, true);
		try_module_get(THIS_MODULE);
		ret = numvfs;
	} else {
		pci_disable_sriov(pdev);
		otx_cpt_set_eng_grps_is_rdonly(&cpt->eng_grps, false);
		module_put(THIS_MODULE);
		cpt->vfs_enabled = 0;
	}
	dev_notice(&cpt->pdev->dev, "VFs enabled: %d\n", ret);

	return ret;
}

static int otx_cpt_probe(struct pci_dev *pdev,
			 const struct pci_device_id __always_unused *ent)
{
	struct device *dev = &pdev->dev;
	struct otx_cpt_device *cpt;
	int err;

	cpt = devm_kzalloc(dev, sizeof(*cpt), GFP_KERNEL);
	if (!cpt)
		return -ENOMEM;

	pci_set_drvdata(pdev, cpt);
	cpt->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto err_clear_drvdata;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable 48-bit DMA configuration\n");
		goto err_release_regions;
	}

	/* MAP PF's configuration registers */
	cpt->reg_base = pci_iomap(pdev, OTX_CPT_PF_PCI_CFG_BAR, 0);
	if (!cpt->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	/* CPT device HW initialization */
	err = otx_cpt_device_init(cpt);
	if (err)
		goto err_unmap_region;

	/* Register interrupts */
	err = otx_cpt_register_interrupts(cpt);
	if (err)
		goto err_unmap_region;

	/* Initialize engine groups */
	err = otx_cpt_init_eng_grps(pdev, &cpt->eng_grps, cpt->pf_type);
	if (err)
		goto err_unregister_interrupts;

	return 0;

err_unregister_interrupts:
	otx_cpt_unregister_interrupts(cpt);
err_unmap_region:
	pci_iounmap(pdev, cpt->reg_base);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_clear_drvdata:
	pci_set_drvdata(pdev, NULL);

	return err;
}

static void otx_cpt_remove(struct pci_dev *pdev)
{
	struct otx_cpt_device *cpt = pci_get_drvdata(pdev);

	if (!cpt)
		return;

	/* Disable VFs */
	pci_disable_sriov(pdev);
	/* Cleanup engine groups */
	otx_cpt_cleanup_eng_grps(pdev, &cpt->eng_grps);
	/* Disable CPT PF interrupts */
	otx_cpt_unregister_interrupts(cpt);
	/* Disengage SE and AE cores from all groups */
	otx_cpt_disable_all_cores(cpt);
	pci_iounmap(pdev, cpt->reg_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

/* Supported devices */
static const struct pci_device_id otx_cpt_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OTX_CPT_PCI_PF_DEVICE_ID) },
	{ 0, }  /* end of table */
};

static struct pci_driver otx_cpt_pci_driver = {
	.name = DRV_NAME,
	.id_table = otx_cpt_id_table,
	.probe = otx_cpt_probe,
	.remove = otx_cpt_remove,
	.sriov_configure = otx_cpt_sriov_configure
};

module_pci_driver(otx_cpt_pci_driver);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell OcteonTX CPT Physical Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, otx_cpt_id_table);
