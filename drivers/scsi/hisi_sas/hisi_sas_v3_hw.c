/*
 * Copyright (c) 2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v3_hw"

static const struct hisi_sas_hw hisi_sas_v3_hw = {
};

static struct Scsi_Host *
hisi_sas_shost_alloc_pci(struct pci_dev *pdev)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;

	shost = scsi_host_alloc(hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost)
		goto err_out;
	hisi_hba = shost_priv(shost);

	hisi_hba->hw = &hisi_sas_v3_hw;
	hisi_hba->pci_dev = pdev;
	hisi_hba->dev = dev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	init_timer(&hisi_hba->timer);

	if (hisi_sas_get_fw_info(hisi_hba) < 0)
		goto err_out;

	if (hisi_sas_alloc(hisi_hba, shost)) {
		hisi_sas_free(hisi_hba);
		goto err_out;
	}

	return shost;
err_out:
	dev_err(dev, "shost alloc failed\n");
	return NULL;
}

static int
hisi_sas_v3_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha;
	int rc, phy_nr, port_nr, i;

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out;

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_disable_device;

	if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) != 0) ||
	    (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)) != 0)) {
		if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) ||
		   (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)) != 0)) {
			dev_err(dev, "No usable DMA addressing method\n");
			rc = -EIO;
			goto err_out_regions;
		}
	}

	shost = hisi_sas_shost_alloc_pci(pdev);
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	sha = SHOST_TO_SAS_HA(shost);
	hisi_hba = shost_priv(shost);
	dev_set_drvdata(dev, sha);

	hisi_hba->regs = pcim_iomap(pdev, 5, 0);
	if (!hisi_hba->regs) {
		dev_err(dev, "cannot map register.\n");
		rc = -ENOMEM;
		goto err_out_ha;
	}

	phy_nr = port_nr = hisi_hba->n_phy;

	arr_phy = devm_kcalloc(dev, phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = devm_kcalloc(dev, port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port) {
		rc = -ENOMEM;
		goto err_out_ha;
	}

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->core.shost = shost;
	sha->lldd_ha = hisi_hba;

	shost->transportt = hisi_sas_stt;
	shost->max_id = HISI_SAS_MAX_DEVICES;
	shost->max_lun = ~0;
	shost->max_channel = 1;
	shost->max_cmd_len = 16;
	shost->sg_tablesize = min_t(u16, SG_ALL, HISI_SAS_SGE_PAGE_CNT);
	shost->can_queue = hisi_hba->hw->max_command_entries;
	shost->cmd_per_lun = hisi_hba->hw->max_command_entries;

	sha->sas_ha_name = DRV_NAME;
	sha->dev = dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &hisi_hba->sas_addr[0];
	sha->num_phys = hisi_hba->n_phy;
	sha->core.shost = hisi_hba->shost;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		sha->sas_phy[i] = &hisi_hba->phy[i].sas_phy;
		sha->sas_port[i] = &hisi_hba->port[i].sas_port;
	}

	hisi_sas_init_add(hisi_hba);

	rc = scsi_add_host(shost, dev);
	if (rc)
		goto err_out_ha;

	rc = sas_register_ha(sha);
	if (rc)
		goto err_out_register_ha;

	rc = hisi_hba->hw->hw_init(hisi_hba);
	if (rc)
		goto err_out_register_ha;

	scsi_scan_host(shost);

	return 0;

err_out_register_ha:
	scsi_remove_host(shost);
err_out_ha:
	kfree(shost);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable_device:
	pci_disable_device(pdev);
err_out:
	return rc;
}

static void hisi_sas_v3_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sas_ha_struct *sha = dev_get_drvdata(dev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_free(hisi_hba);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

enum {
	/* instances of the controller */
	hip08,
};

static const struct pci_device_id sas_v3_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, 0xa230), hip08 },
	{}
};

static struct pci_driver sas_v3_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= sas_v3_pci_table,
	.probe		= hisi_sas_v3_probe,
	.remove		= hisi_sas_v3_remove,
};

module_pci_driver(sas_v3_pci_driver);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v3 hw driver based on pci device");
MODULE_ALIAS("platform:" DRV_NAME);
