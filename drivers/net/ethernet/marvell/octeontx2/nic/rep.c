// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU representor driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/net_tstamp.h>

#include "otx2_common.h"
#include "cn10k.h"
#include "otx2_reg.h"
#include "rep.h"

#define DRV_NAME	"rvu_rep"
#define DRV_STRING	"Marvell RVU Representor Driver"

static const struct pci_device_id rvu_rep_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_RVU_REP) },
	{ }
};

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, rvu_rep_id_table);

static int rvu_get_rep_cnt(struct otx2_nic *priv)
{
	struct get_rep_cnt_rsp *rsp;
	struct mbox_msghdr *msghdr;
	struct msg_req *req;
	int err, rep;

	mutex_lock(&priv->mbox.lock);
	req = otx2_mbox_alloc_msg_get_rep_cnt(&priv->mbox);
	if (!req) {
		mutex_unlock(&priv->mbox.lock);
		return -ENOMEM;
	}
	err = otx2_sync_mbox_msg(&priv->mbox);
	if (err)
		goto exit;

	msghdr = otx2_mbox_get_rsp(&priv->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(msghdr)) {
		err = PTR_ERR(msghdr);
		goto exit;
	}

	rsp = (struct get_rep_cnt_rsp *)msghdr;
	priv->hw.tx_queues = rsp->rep_cnt;
	priv->hw.rx_queues = rsp->rep_cnt;
	priv->rep_cnt = rsp->rep_cnt;
	for (rep = 0; rep < priv->rep_cnt; rep++)
		priv->rep_pf_map[rep] = rsp->rep_pf_map[rep];

exit:
	mutex_unlock(&priv->mbox.lock);
	return err;
}

static int rvu_rep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct otx2_nic *priv;
	struct otx2_hw *hw;
	int err;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "DMA mask config failed, abort\n");
		goto err_release_regions;
	}

	pci_set_master(pdev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	pci_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	priv->dev = dev;
	priv->flags |= OTX2_FLAG_INTF_DOWN;
	priv->flags |= OTX2_FLAG_REP_MODE_ENABLED;

	hw = &priv->hw;
	hw->pdev = pdev;
	hw->max_queues = OTX2_MAX_CQ_CNT;
	hw->rbuf_len = OTX2_DEFAULT_RBUF_LEN;
	hw->xqe_size = 128;

	err = otx2_init_rsrc(pdev, priv);
	if (err)
		goto err_release_regions;

	priv->iommu_domain = iommu_get_domain_for_dev(dev);

	err = rvu_get_rep_cnt(priv);
	if (err)
		goto err_detach_rsrc;

	return 0;

err_detach_rsrc:
	if (priv->hw.lmt_info)
		free_percpu(priv->hw.lmt_info);
	if (test_bit(CN10K_LMTST, &priv->hw.cap_flag))
		qmem_free(priv->dev, priv->dync_lmt);
	otx2_detach_resources(&priv->mbox);
	otx2_disable_mbox_intr(priv);
	otx2_pfaf_mbox_destroy(priv);
	pci_free_irq_vectors(pdev);
err_release_regions:
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
	return err;
}

static void rvu_rep_remove(struct pci_dev *pdev)
{
	struct otx2_nic *priv = pci_get_drvdata(pdev);

	otx2_detach_resources(&priv->mbox);
	if (priv->hw.lmt_info)
		free_percpu(priv->hw.lmt_info);
	if (test_bit(CN10K_LMTST, &priv->hw.cap_flag))
		qmem_free(priv->dev, priv->dync_lmt);
	otx2_disable_mbox_intr(priv);
	otx2_pfaf_mbox_destroy(priv);
	pci_free_irq_vectors(priv->pdev);
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
}

static struct pci_driver rvu_rep_driver = {
	.name = DRV_NAME,
	.id_table = rvu_rep_id_table,
	.probe = rvu_rep_probe,
	.remove = rvu_rep_remove,
	.shutdown = rvu_rep_remove,
};

static int __init rvu_rep_init_module(void)
{
	return pci_register_driver(&rvu_rep_driver);
}

static void __exit rvu_rep_cleanup_module(void)
{
	pci_unregister_driver(&rvu_rep_driver);
}

module_init(rvu_rep_init_module);
module_exit(rvu_rep_cleanup_module);
