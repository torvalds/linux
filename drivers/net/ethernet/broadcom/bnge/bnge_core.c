// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/init.h>
#include <linux/crash_dump.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "bnge.h"
#include "bnge_devlink.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRV_SUMMARY);

char bnge_driver_name[] = DRV_NAME;

static const struct {
	char *name;
} board_info[] = {
	[BCM57708] = { "Broadcom BCM57708 50Gb/100Gb/200Gb/400Gb/800Gb Ethernet" },
};

static const struct pci_device_id bnge_pci_tbl[] = {
	{ PCI_VDEVICE(BROADCOM, 0x1780), .driver_data = BCM57708 },
	/* Required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, bnge_pci_tbl);

static void bnge_print_device_info(struct pci_dev *pdev, enum board_idx idx)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "%s found at mem %lx\n", board_info[idx].name,
		 (long)pci_resource_start(pdev, 0));

	pcie_print_link_status(pdev);
}

bool bnge_aux_registered(struct bnge_dev *bd)
{
	return false;
}

static void bnge_nvm_cfg_ver_get(struct bnge_dev *bd)
{
	struct hwrm_nvm_get_dev_info_output nvm_info;

	if (!bnge_hwrm_nvm_dev_info(bd, &nvm_info))
		snprintf(bd->nvm_cfg_ver, FW_VER_STR_LEN, "%d.%d.%d",
			 nvm_info.nvm_cfg_ver_maj, nvm_info.nvm_cfg_ver_min,
			 nvm_info.nvm_cfg_ver_upd);
}

static int bnge_func_qcaps(struct bnge_dev *bd)
{
	int rc;

	rc = bnge_hwrm_func_qcaps(bd);
	if (rc)
		return rc;

	rc = bnge_hwrm_queue_qportcfg(bd);
	if (rc) {
		dev_err(bd->dev, "query qportcfg failure rc: %d\n", rc);
		return rc;
	}

	rc = bnge_hwrm_func_resc_qcaps(bd);
	if (rc) {
		dev_err(bd->dev, "query resc caps failure rc: %d\n", rc);
		return rc;
	}

	rc = bnge_hwrm_func_qcfg(bd);
	if (rc) {
		dev_err(bd->dev, "query config failure rc: %d\n", rc);
		return rc;
	}

	rc = bnge_hwrm_vnic_qcaps(bd);
	if (rc) {
		dev_err(bd->dev, "vnic caps failure rc: %d\n", rc);
		return rc;
	}

	return 0;
}

static void bnge_fw_unregister_dev(struct bnge_dev *bd)
{
	/* ctx mem free after unrgtr only */
	bnge_hwrm_func_drv_unrgtr(bd);
	bnge_free_ctx_mem(bd);
}

static void bnge_set_dflt_rss_hash_type(struct bnge_dev *bd)
{
	bd->rss_hash_cfg = VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6;
}

static int bnge_fw_register_dev(struct bnge_dev *bd)
{
	int rc;

	bd->fw_cap = 0;
	rc = bnge_hwrm_ver_get(bd);
	if (rc) {
		dev_err(bd->dev, "Get Version command failed rc: %d\n", rc);
		return rc;
	}

	bnge_nvm_cfg_ver_get(bd);

	rc = bnge_hwrm_func_reset(bd);
	if (rc) {
		dev_err(bd->dev, "Failed to reset function rc: %d\n", rc);
		return rc;
	}

	bnge_hwrm_fw_set_time(bd);

	rc =  bnge_hwrm_func_drv_rgtr(bd);
	if (rc) {
		dev_err(bd->dev, "Failed to rgtr with firmware rc: %d\n", rc);
		return rc;
	}

	rc = bnge_alloc_ctx_mem(bd);
	if (rc) {
		dev_err(bd->dev, "Failed to allocate ctx mem rc: %d\n", rc);
		goto err_func_unrgtr;
	}

	/* Get the resources and configuration from firmware */
	rc = bnge_func_qcaps(bd);
	if (rc) {
		dev_err(bd->dev, "Failed initial configuration rc: %d\n", rc);
		rc = -ENODEV;
		goto err_func_unrgtr;
	}

	bnge_set_dflt_rss_hash_type(bd);

	return 0;

err_func_unrgtr:
	bnge_fw_unregister_dev(bd);
	return rc;
}

static void bnge_pci_disable(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
}

static int bnge_pci_enable(struct pci_dev *pdev)
{
	int rc;

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		return rc;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev,
			"Cannot find PCI device base address, aborting\n");
		rc = -ENODEV;
		goto err_pci_disable;
	}

	rc = pci_request_regions(pdev, bnge_driver_name);
	if (rc) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting\n");
		goto err_pci_disable;
	}

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	pci_set_master(pdev);

	return 0;

err_pci_disable:
	pci_disable_device(pdev);
	return rc;
}

static void bnge_unmap_bars(struct pci_dev *pdev)
{
	struct bnge_dev *bd = pci_get_drvdata(pdev);

	if (bd->bar1) {
		pci_iounmap(pdev, bd->bar1);
		bd->bar1 = NULL;
	}

	if (bd->bar0) {
		pci_iounmap(pdev, bd->bar0);
		bd->bar0 = NULL;
	}
}

static void bnge_set_max_func_irqs(struct bnge_dev *bd,
				   unsigned int max_irqs)
{
	bd->hw_resc.max_irqs = max_irqs;
}

static int bnge_get_max_irq(struct pci_dev *pdev)
{
	u16 ctrl;

	pci_read_config_word(pdev, pdev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	return (ctrl & PCI_MSIX_FLAGS_QSIZE) + 1;
}

static int bnge_map_db_bar(struct bnge_dev *bd)
{
	if (!bd->db_size)
		return -ENODEV;

	bd->bar1 = pci_iomap(bd->pdev, 2, bd->db_size);
	if (!bd->bar1)
		return -ENOMEM;

	return 0;
}

static int bnge_probe_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned int max_irqs;
	struct bnge_dev *bd;
	int rc;

	if (pci_is_bridge(pdev))
		return -ENODEV;

	if (!pdev->msix_cap) {
		dev_err(&pdev->dev, "MSIX capability missing, aborting\n");
		return -ENODEV;
	}

	if (is_kdump_kernel()) {
		pci_clear_master(pdev);
		pcie_flr(pdev);
	}

	rc = bnge_pci_enable(pdev);
	if (rc)
		return rc;

	bnge_print_device_info(pdev, ent->driver_data);

	bd = bnge_devlink_alloc(pdev);
	if (!bd) {
		dev_err(&pdev->dev, "Devlink allocation failed\n");
		rc = -ENOMEM;
		goto err_pci_disable;
	}

	bd->bar0 = pci_ioremap_bar(pdev, 0);
	if (!bd->bar0) {
		dev_err(&pdev->dev, "Failed mapping BAR-0, aborting\n");
		rc = -ENOMEM;
		goto err_devl_free;
	}

	rc = bnge_init_hwrm_resources(bd);
	if (rc)
		goto err_bar_unmap;

	rc = bnge_fw_register_dev(bd);
	if (rc) {
		dev_err(&pdev->dev, "Failed to register with firmware rc = %d\n", rc);
		goto err_hwrm_cleanup;
	}

	bnge_devlink_register(bd);

	max_irqs = bnge_get_max_irq(pdev);
	bnge_set_max_func_irqs(bd, max_irqs);

	bnge_aux_init_dflt_config(bd);

	rc = bnge_net_init_dflt_config(bd);
	if (rc) {
		dev_err(&pdev->dev, "Error setting up default cfg to netdev rc = %d\n",
			rc);
		goto err_fw_reg;
	}

	rc = bnge_map_db_bar(bd);
	if (rc) {
		dev_err(&pdev->dev, "Failed mapping doorbell BAR rc = %d, aborting\n",
			rc);
		goto err_config_uninit;
	}

#if BITS_PER_LONG == 32
	spin_lock_init(&bd->db_lock);
#endif

	rc = bnge_alloc_irqs(bd);
	if (rc) {
		dev_err(&pdev->dev, "Error IRQ allocation rc = %d\n", rc);
		goto err_config_uninit;
	}

	rc = bnge_netdev_alloc(bd, max_irqs);
	if (rc)
		goto err_free_irq;

	pci_save_state(pdev);

	return 0;

err_free_irq:
	bnge_free_irqs(bd);

err_config_uninit:
	bnge_net_uninit_dflt_config(bd);

err_fw_reg:
	bnge_devlink_unregister(bd);
	bnge_fw_unregister_dev(bd);

err_hwrm_cleanup:
	bnge_cleanup_hwrm_resources(bd);

err_bar_unmap:
	bnge_unmap_bars(pdev);

err_devl_free:
	bnge_devlink_free(bd);

err_pci_disable:
	bnge_pci_disable(pdev);
	return rc;
}

static void bnge_remove_one(struct pci_dev *pdev)
{
	struct bnge_dev *bd = pci_get_drvdata(pdev);

	bnge_netdev_free(bd);

	bnge_free_irqs(bd);

	bnge_net_uninit_dflt_config(bd);

	bnge_devlink_unregister(bd);

	bnge_fw_unregister_dev(bd);

	bnge_cleanup_hwrm_resources(bd);

	bnge_unmap_bars(pdev);

	bnge_devlink_free(bd);

	bnge_pci_disable(pdev);
}

static void bnge_shutdown(struct pci_dev *pdev)
{
	pci_disable_device(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, 0);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static struct pci_driver bnge_driver = {
	.name		= bnge_driver_name,
	.id_table	= bnge_pci_tbl,
	.probe		= bnge_probe_one,
	.remove		= bnge_remove_one,
	.shutdown	= bnge_shutdown,
};

static int __init bnge_init_module(void)
{
	return pci_register_driver(&bnge_driver);
}
module_init(bnge_init_module);

static void __exit bnge_exit_module(void)
{
	pci_unregister_driver(&bnge_driver);
}
module_exit(bnge_exit_module);
