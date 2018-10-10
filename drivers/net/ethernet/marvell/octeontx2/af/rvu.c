// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include "rvu.h"
#include "rvu_reg.h"

#define DRV_NAME	"octeontx2-af"
#define DRV_STRING      "Marvell OcteonTX2 RVU Admin Function Driver"
#define DRV_VERSION	"1.0"

/* Supported devices */
static const struct pci_device_id rvu_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_RVU_AF) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, rvu_id_table);

/* Poll a RVU block's register 'offset', for a 'zero'
 * or 'nonzero' at bits specified by 'mask'
 */
int rvu_poll_reg(struct rvu *rvu, u64 block, u64 offset, u64 mask, bool zero)
{
	void __iomem *reg;
	int timeout = 100;
	u64 reg_val;

	reg = rvu->afreg_base + ((block << 28) | offset);
	while (timeout) {
		reg_val = readq(reg);
		if (zero && !(reg_val & mask))
			return 0;
		if (!zero && (reg_val & mask))
			return 0;
		usleep_range(1, 2);
		timeout--;
	}
	return -EBUSY;
}

int rvu_alloc_bitmap(struct rsrc_bmap *rsrc)
{
	rsrc->bmap = kcalloc(BITS_TO_LONGS(rsrc->max),
			     sizeof(long), GFP_KERNEL);
	if (!rsrc->bmap)
		return -ENOMEM;
	return 0;
}

static void rvu_check_block_implemented(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkid;
	u64 cfg;

	/* For each block check if 'implemented' bit is set */
	for (blkid = 0; blkid < BLK_COUNT; blkid++) {
		block = &hw->block[blkid];
		cfg = rvupf_read64(rvu, RVU_PF_BLOCK_ADDRX_DISC(blkid));
		if (cfg & BIT_ULL(11))
			block->implemented = true;
	}
}

static void rvu_block_reset(struct rvu *rvu, int blkaddr, u64 rst_reg)
{
	struct rvu_block *block = &rvu->hw->block[blkaddr];

	if (!block->implemented)
		return;

	rvu_write64(rvu, blkaddr, rst_reg, BIT_ULL(0));
	rvu_poll_reg(rvu, blkaddr, rst_reg, BIT_ULL(63), true);
}

static void rvu_reset_all_blocks(struct rvu *rvu)
{
	/* Do a HW reset of all RVU blocks */
	rvu_block_reset(rvu, BLKADDR_NPA, NPA_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NIX0, NIX_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NPC, NPC_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_SSO, SSO_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_TIM, TIM_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_CPT0, CPT_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NDC0, NDC_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NDC1, NDC_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NDC2, NDC_AF_BLK_RST);
}

static void rvu_free_hw_resources(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int id;

	/* Free all bitmaps */
	for (id = 0; id < BLK_COUNT; id++) {
		block = &hw->block[id];
		kfree(block->lf.bmap);
	}
}

static int rvu_setup_hw_resources(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int err;
	u64 cfg;

	/* Get HW supported max RVU PF & VF count */
	cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_CONST);
	hw->total_pfs = (cfg >> 32) & 0xFF;
	hw->total_vfs = (cfg >> 20) & 0xFFF;
	hw->max_vfs_per_pf = (cfg >> 40) & 0xFF;

	/* Init NPA LF's bitmap */
	block = &hw->block[BLKADDR_NPA];
	if (!block->implemented)
		goto nix;
	cfg = rvu_read64(rvu, BLKADDR_NPA, NPA_AF_CONST);
	block->lf.max = (cfg >> 16) & 0xFFF;
	block->addr = BLKADDR_NPA;
	block->lfshift = 8;
	block->lookup_reg = NPA_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_NPA_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_NPA_CFG;
	block->lfcfg_reg = NPA_PRIV_LFX_CFG;
	block->msixcfg_reg = NPA_PRIV_LFX_INT_CFG;
	block->lfreset_reg = NPA_AF_LF_RST;
	sprintf(block->name, "NPA");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

nix:
	/* Init NIX LF's bitmap */
	block = &hw->block[BLKADDR_NIX0];
	if (!block->implemented)
		goto sso;
	cfg = rvu_read64(rvu, BLKADDR_NIX0, NIX_AF_CONST2);
	block->lf.max = cfg & 0xFFF;
	block->addr = BLKADDR_NIX0;
	block->lfshift = 8;
	block->lookup_reg = NIX_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_NIX_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_NIX_CFG;
	block->lfcfg_reg = NIX_PRIV_LFX_CFG;
	block->msixcfg_reg = NIX_PRIV_LFX_INT_CFG;
	block->lfreset_reg = NIX_AF_LF_RST;
	sprintf(block->name, "NIX");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

sso:
	/* Init SSO group's bitmap */
	block = &hw->block[BLKADDR_SSO];
	if (!block->implemented)
		goto ssow;
	cfg = rvu_read64(rvu, BLKADDR_SSO, SSO_AF_CONST);
	block->lf.max = cfg & 0xFFFF;
	block->addr = BLKADDR_SSO;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = SSO_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_SSO_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_SSO_CFG;
	block->lfcfg_reg = SSO_PRIV_LFX_HWGRP_CFG;
	block->msixcfg_reg = SSO_PRIV_LFX_HWGRP_INT_CFG;
	block->lfreset_reg = SSO_AF_LF_HWGRP_RST;
	sprintf(block->name, "SSO GROUP");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

ssow:
	/* Init SSO workslot's bitmap */
	block = &hw->block[BLKADDR_SSOW];
	if (!block->implemented)
		goto tim;
	block->lf.max = (cfg >> 56) & 0xFF;
	block->addr = BLKADDR_SSOW;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = SSOW_AF_RVU_LF_HWS_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_SSOW_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_SSOW_CFG;
	block->lfcfg_reg = SSOW_PRIV_LFX_HWS_CFG;
	block->msixcfg_reg = SSOW_PRIV_LFX_HWS_INT_CFG;
	block->lfreset_reg = SSOW_AF_LF_HWS_RST;
	sprintf(block->name, "SSOWS");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

tim:
	/* Init TIM LF's bitmap */
	block = &hw->block[BLKADDR_TIM];
	if (!block->implemented)
		goto cpt;
	cfg = rvu_read64(rvu, BLKADDR_TIM, TIM_AF_CONST);
	block->lf.max = cfg & 0xFFFF;
	block->addr = BLKADDR_TIM;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = TIM_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_TIM_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_TIM_CFG;
	block->lfcfg_reg = TIM_PRIV_LFX_CFG;
	block->msixcfg_reg = TIM_PRIV_LFX_INT_CFG;
	block->lfreset_reg = TIM_AF_LF_RST;
	sprintf(block->name, "TIM");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

cpt:
	/* Init CPT LF's bitmap */
	block = &hw->block[BLKADDR_CPT0];
	if (!block->implemented)
		return 0;
	cfg = rvu_read64(rvu, BLKADDR_CPT0, CPT_AF_CONSTANTS0);
	block->lf.max = cfg & 0xFF;
	block->addr = BLKADDR_CPT0;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = CPT_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_CPT_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_CPT_CFG;
	block->lfcfg_reg = CPT_PRIV_LFX_CFG;
	block->msixcfg_reg = CPT_PRIV_LFX_INT_CFG;
	block->lfreset_reg = CPT_AF_LF_RST;
	sprintf(block->name, "CPT");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

	return 0;
}

static int rvu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct rvu *rvu;
	int    err;

	rvu = devm_kzalloc(dev, sizeof(*rvu), GFP_KERNEL);
	if (!rvu)
		return -ENOMEM;

	rvu->hw = devm_kzalloc(dev, sizeof(struct rvu_hwinfo), GFP_KERNEL);
	if (!rvu->hw) {
		devm_kfree(dev, rvu);
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, rvu);
	rvu->pdev = pdev;
	rvu->dev = &pdev->dev;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto err_freemem;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to set DMA mask\n");
		goto err_release_regions;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to set consistent DMA mask\n");
		goto err_release_regions;
	}

	/* Map Admin function CSRs */
	rvu->afreg_base = pcim_iomap(pdev, PCI_AF_REG_BAR_NUM, 0);
	rvu->pfreg_base = pcim_iomap(pdev, PCI_PF_REG_BAR_NUM, 0);
	if (!rvu->afreg_base || !rvu->pfreg_base) {
		dev_err(dev, "Unable to map admin function CSRs, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	/* Check which blocks the HW supports */
	rvu_check_block_implemented(rvu);

	rvu_reset_all_blocks(rvu);

	err = rvu_setup_hw_resources(rvu);
	if (err)
		goto err_release_regions;

	return 0;

err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_freemem:
	pci_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, rvu->hw);
	devm_kfree(dev, rvu);
	return err;
}

static void rvu_remove(struct pci_dev *pdev)
{
	struct rvu *rvu = pci_get_drvdata(pdev);

	rvu_reset_all_blocks(rvu);
	rvu_free_hw_resources(rvu);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	devm_kfree(&pdev->dev, rvu->hw);
	devm_kfree(&pdev->dev, rvu);
}

static struct pci_driver rvu_driver = {
	.name = DRV_NAME,
	.id_table = rvu_id_table,
	.probe = rvu_probe,
	.remove = rvu_remove,
};

static int __init rvu_init_module(void)
{
	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	return pci_register_driver(&rvu_driver);
}

static void __exit rvu_cleanup_module(void)
{
	pci_unregister_driver(&rvu_driver);
}

module_init(rvu_init_module);
module_exit(rvu_cleanup_module);
