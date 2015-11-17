/*
 * Copyright (c) 2015 Linaro Ltd.
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas"

static struct scsi_transport_template *hisi_sas_stt;

static struct scsi_host_template hisi_sas_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= sas_slave_configure,
	.change_queue_depth	= sas_change_queue_depth,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
};

static struct sas_domain_function_template hisi_sas_transport_ops = {
};

static int hisi_sas_alloc(struct hisi_hba *hisi_hba, struct Scsi_Host *shost)
{
	int i, s;
	struct platform_device *pdev = hisi_hba->pdev;
	struct device *dev = &pdev->dev;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		/* Delivery queue */
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->cmd_hdr[i] = dma_alloc_coherent(dev, s,
					&hisi_hba->cmd_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->cmd_hdr[i])
			goto err_out;
		memset(hisi_hba->cmd_hdr[i], 0, s);

		/* Completion queue */
		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->complete_hdr[i] = dma_alloc_coherent(dev, s,
				&hisi_hba->complete_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->complete_hdr[i])
			goto err_out;
		memset(hisi_hba->complete_hdr[i], 0, s);
	}

	s = HISI_SAS_STATUS_BUF_SZ;
	hisi_hba->status_buffer_pool = dma_pool_create("status_buffer",
						       dev, s, 16, 0);
	if (!hisi_hba->status_buffer_pool)
		goto err_out;

	s = HISI_SAS_COMMAND_TABLE_SZ;
	hisi_hba->command_table_pool = dma_pool_create("command_table",
						       dev, s, 16, 0);
	if (!hisi_hba->command_table_pool)
		goto err_out;

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	hisi_hba->itct = dma_alloc_coherent(dev, s, &hisi_hba->itct_dma,
					    GFP_KERNEL);
	if (!hisi_hba->itct)
		goto err_out;

	memset(hisi_hba->itct, 0, s);

	hisi_hba->slot_info = devm_kcalloc(dev, HISI_SAS_COMMAND_ENTRIES,
					   sizeof(struct hisi_sas_slot),
					   GFP_KERNEL);
	if (!hisi_hba->slot_info)
		goto err_out;

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_iost);
	hisi_hba->iost = dma_alloc_coherent(dev, s, &hisi_hba->iost_dma,
					    GFP_KERNEL);
	if (!hisi_hba->iost)
		goto err_out;

	memset(hisi_hba->iost, 0, s);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint);
	hisi_hba->breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->breakpoint)
		goto err_out;

	memset(hisi_hba->breakpoint, 0, s);

	hisi_hba->sge_page_pool = dma_pool_create("status_sge", dev,
				sizeof(struct hisi_sas_sge_page), 16, 0);
	if (!hisi_hba->sge_page_pool)
		goto err_out;

	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	hisi_hba->initial_fis = dma_alloc_coherent(dev, s,
				&hisi_hba->initial_fis_dma, GFP_KERNEL);
	if (!hisi_hba->initial_fis)
		goto err_out;
	memset(hisi_hba->initial_fis, 0, s);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint) * 2;
	hisi_hba->sata_breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->sata_breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->sata_breakpoint)
		goto err_out;
	memset(hisi_hba->sata_breakpoint, 0, s);

	return 0;
err_out:
	return -ENOMEM;
}

static void hisi_sas_free(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	int i, s;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		if (hisi_hba->cmd_hdr[i])
			dma_free_coherent(dev, s,
					  hisi_hba->cmd_hdr[i],
					  hisi_hba->cmd_hdr_dma[i]);

		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		if (hisi_hba->complete_hdr[i])
			dma_free_coherent(dev, s,
					  hisi_hba->complete_hdr[i],
					  hisi_hba->complete_hdr_dma[i]);
	}

	dma_pool_destroy(hisi_hba->status_buffer_pool);
	dma_pool_destroy(hisi_hba->command_table_pool);
	dma_pool_destroy(hisi_hba->sge_page_pool);

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	if (hisi_hba->itct)
		dma_free_coherent(dev, s,
				  hisi_hba->itct, hisi_hba->itct_dma);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_iost);
	if (hisi_hba->iost)
		dma_free_coherent(dev, s,
				  hisi_hba->iost, hisi_hba->iost_dma);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint);
	if (hisi_hba->breakpoint)
		dma_free_coherent(dev, s,
				  hisi_hba->breakpoint,
				  hisi_hba->breakpoint_dma);


	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	if (hisi_hba->initial_fis)
		dma_free_coherent(dev, s,
				  hisi_hba->initial_fis,
				  hisi_hba->initial_fis_dma);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint) * 2;
	if (hisi_hba->sata_breakpoint)
		dma_free_coherent(dev, s,
				  hisi_hba->sata_breakpoint,
				  hisi_hba->sata_breakpoint_dma);

}

static struct Scsi_Host *hisi_sas_shost_alloc(struct platform_device *pdev,
					      const struct hisi_sas_hw *hw)
{
	struct resource *res;
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct property *sas_addr_prop;
	int num;

	shost = scsi_host_alloc(&hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost)
		goto err_out;
	hisi_hba = shost_priv(shost);

	hisi_hba->hw = hw;
	hisi_hba->pdev = pdev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	sas_addr_prop = of_find_property(np, "sas-addr", NULL);
	if (!sas_addr_prop || (sas_addr_prop->length != SAS_ADDR_SIZE))
		goto err_out;
	memcpy(hisi_hba->sas_addr, sas_addr_prop->value, SAS_ADDR_SIZE);

	if (of_property_read_u32(np, "ctrl-reset-reg",
				 &hisi_hba->ctrl_reset_reg))
		goto err_out;

	if (of_property_read_u32(np, "ctrl-reset-sts-reg",
				 &hisi_hba->ctrl_reset_sts_reg))
		goto err_out;

	if (of_property_read_u32(np, "ctrl-clock-ena-reg",
				 &hisi_hba->ctrl_clock_ena_reg))
		goto err_out;

	if (of_property_read_u32(np, "phy-count", &hisi_hba->n_phy))
		goto err_out;

	if (of_property_read_u32(np, "queue-count", &hisi_hba->queue_count))
		goto err_out;

	num = of_irq_count(np);
	hisi_hba->int_names = devm_kcalloc(dev, num,
					   HISI_SAS_NAME_LEN,
					   GFP_KERNEL);
	if (!hisi_hba->int_names)
		goto err_out;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hisi_hba->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hisi_hba->regs))
		goto err_out;

	hisi_hba->ctrl = syscon_regmap_lookup_by_phandle(
				np, "hisilicon,sas-syscon");
	if (IS_ERR(hisi_hba->ctrl))
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

int hisi_sas_probe(struct platform_device *pdev,
			 const struct hisi_sas_hw *hw)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha;
	int rc, phy_nr, port_nr, i;

	shost = hisi_sas_shost_alloc(pdev, hw);
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_ha;
	}

	sha = SHOST_TO_SAS_HA(shost);
	hisi_hba = shost_priv(shost);
	platform_set_drvdata(pdev, sha);
	phy_nr = port_nr = hisi_hba->n_phy;

	arr_phy = devm_kcalloc(dev, phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = devm_kcalloc(dev, port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port)
		return -ENOMEM;

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
	shost->can_queue = HISI_SAS_COMMAND_ENTRIES;
	shost->cmd_per_lun = HISI_SAS_COMMAND_ENTRIES;

	sha->sas_ha_name = DRV_NAME;
	sha->dev = &hisi_hba->pdev->dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &hisi_hba->sas_addr[0];
	sha->num_phys = hisi_hba->n_phy;
	sha->core.shost = hisi_hba->shost;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		sha->sas_phy[i] = &hisi_hba->phy[i].sas_phy;
		sha->sas_port[i] = &hisi_hba->port[i].sas_port;
	}

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc)
		goto err_out_ha;

	rc = sas_register_ha(sha);
	if (rc)
		goto err_out_register_ha;

	scsi_scan_host(shost);

	return 0;

err_out_register_ha:
	scsi_remove_host(shost);
err_out_ha:
	kfree(shost);
	return rc;
}
EXPORT_SYMBOL_GPL(hisi_sas_probe);

int hisi_sas_remove(struct platform_device *pdev)
{
	struct sas_ha_struct *sha = platform_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	scsi_remove_host(sha->core.shost);
	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_free(hisi_hba);
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_remove);

static __init int hisi_sas_init(void)
{
	pr_info("hisi_sas: driver version %s\n", DRV_VERSION);

	hisi_sas_stt = sas_domain_attach_transport(&hisi_sas_transport_ops);
	if (!hisi_sas_stt)
		return -ENOMEM;

	return 0;
}

static __exit void hisi_sas_exit(void)
{
	sas_release_transport(hisi_sas_stt);
}

module_init(hisi_sas_init);
module_exit(hisi_sas_exit);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
