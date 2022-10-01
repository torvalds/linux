// SPDX-License-Identifier: GPL-2.0
/* Marvell MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mcs.h"
#include "mcs_reg.h"

#define DRV_NAME	"Marvell MCS Driver"

#define PCI_CFG_REG_BAR_NUM	0

static const struct pci_device_id mcs_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CN10K_MCS) },
	{ 0, }  /* end of table */
};

static LIST_HEAD(mcs_list);

static void *alloc_mem(struct mcs *mcs, int n)
{
	return devm_kcalloc(mcs->dev, n, sizeof(u16), GFP_KERNEL);
}

static int mcs_alloc_struct_mem(struct mcs *mcs, struct mcs_rsrc_map *res)
{
	struct hwinfo *hw = mcs->hw;
	int err;

	res->flowid2pf_map = alloc_mem(mcs, hw->tcam_entries);
	if (!res->flowid2pf_map)
		return -ENOMEM;

	res->secy2pf_map = alloc_mem(mcs, hw->secy_entries);
	if (!res->secy2pf_map)
		return -ENOMEM;

	res->sc2pf_map = alloc_mem(mcs, hw->sc_entries);
	if (!res->sc2pf_map)
		return -ENOMEM;

	res->sa2pf_map = alloc_mem(mcs, hw->sa_entries);
	if (!res->sa2pf_map)
		return -ENOMEM;

	res->flowid2secy_map = alloc_mem(mcs, hw->tcam_entries);
	if (!res->flowid2secy_map)
		return -ENOMEM;

	res->flow_ids.max = hw->tcam_entries - MCS_RSRC_RSVD_CNT;
	err = rvu_alloc_bitmap(&res->flow_ids);
	if (err)
		return err;

	res->secy.max = hw->secy_entries - MCS_RSRC_RSVD_CNT;
	err = rvu_alloc_bitmap(&res->secy);
	if (err)
		return err;

	res->sc.max = hw->sc_entries;
	err = rvu_alloc_bitmap(&res->sc);
	if (err)
		return err;

	res->sa.max = hw->sa_entries;
	err = rvu_alloc_bitmap(&res->sa);
	if (err)
		return err;

	return 0;
}

int mcs_get_blkcnt(void)
{
	struct mcs *mcs;
	int idmax = -ENODEV;

	/* Check MCS block is present in hardware */
	if (!pci_dev_present(mcs_id_table))
		return 0;

	list_for_each_entry(mcs, &mcs_list, mcs_list)
		if (mcs->mcs_id > idmax)
			idmax = mcs->mcs_id;

	if (idmax < 0)
		return 0;

	return idmax + 1;
}

struct mcs *mcs_get_pdata(int mcs_id)
{
	struct mcs *mcs_dev;

	list_for_each_entry(mcs_dev, &mcs_list, mcs_list) {
		if (mcs_dev->mcs_id == mcs_id)
			return mcs_dev;
	}
	return NULL;
}

/* Set lmac to bypass/operational mode */
void mcs_set_lmac_mode(struct mcs *mcs, int lmac_id, u8 mode)
{
	u64 reg;

	reg = MCSX_MCS_TOP_SLAVE_CHANNEL_CFG(lmac_id * 2);
	mcs_reg_write(mcs, reg, (u64)mode);
}

void cn10kb_mcs_parser_cfg(struct mcs *mcs)
{
	u64 reg, val;

	/* VLAN CTag */
	val = BIT_ULL(0) | (0x8100ull & 0xFFFF) << 1 | BIT_ULL(17);
	/* RX */
	reg = MCSX_PEX_RX_SLAVE_VLAN_CFGX(0);
	mcs_reg_write(mcs, reg, val);

	/* TX */
	reg = MCSX_PEX_TX_SLAVE_VLAN_CFGX(0);
	mcs_reg_write(mcs, reg, val);

	/* VLAN STag */
	val = BIT_ULL(0) | (0x88a8ull & 0xFFFF) << 1 | BIT_ULL(18);
	/* RX */
	reg = MCSX_PEX_RX_SLAVE_VLAN_CFGX(1);
	mcs_reg_write(mcs, reg, val);

	/* TX */
	reg = MCSX_PEX_TX_SLAVE_VLAN_CFGX(1);
	mcs_reg_write(mcs, reg, val);
}

static void mcs_lmac_init(struct mcs *mcs, int lmac_id)
{
	u64 reg;

	/* Port mode 25GB */
	reg = MCSX_PAB_RX_SLAVE_PORT_CFGX(lmac_id);
	mcs_reg_write(mcs, reg, 0);

	if (mcs->hw->mcs_blks > 1) {
		reg = MCSX_PAB_RX_SLAVE_FIFO_SKID_CFGX(lmac_id);
		mcs_reg_write(mcs, reg, 0xe000e);
		return;
	}

	reg = MCSX_PAB_TX_SLAVE_PORT_CFGX(lmac_id);
	mcs_reg_write(mcs, reg, 0);
}

int mcs_set_lmac_channels(int mcs_id, u16 base)
{
	struct mcs *mcs;
	int lmac;
	u64 cfg;

	mcs = mcs_get_pdata(mcs_id);
	if (!mcs)
		return -ENODEV;
	for (lmac = 0; lmac < mcs->hw->lmac_cnt; lmac++) {
		cfg = mcs_reg_read(mcs, MCSX_LINK_LMACX_CFG(lmac));
		cfg &= ~(MCSX_LINK_LMAC_BASE_MASK | MCSX_LINK_LMAC_RANGE_MASK);
		cfg |=	FIELD_PREP(MCSX_LINK_LMAC_RANGE_MASK, ilog2(16));
		cfg |=	FIELD_PREP(MCSX_LINK_LMAC_BASE_MASK, base);
		mcs_reg_write(mcs, MCSX_LINK_LMACX_CFG(lmac), cfg);
		base += 16;
	}
	return 0;
}

static int mcs_x2p_calibration(struct mcs *mcs)
{
	unsigned long timeout = jiffies + usecs_to_jiffies(20000);
	int i, err = 0;
	u64 val;

	/* set X2P calibration */
	val = mcs_reg_read(mcs, MCSX_MIL_GLOBAL);
	val |= BIT_ULL(5);
	mcs_reg_write(mcs, MCSX_MIL_GLOBAL, val);

	/* Wait for calibration to complete */
	while (!(mcs_reg_read(mcs, MCSX_MIL_RX_GBL_STATUS) & BIT_ULL(0))) {
		if (time_before(jiffies, timeout)) {
			usleep_range(80, 100);
			continue;
		} else {
			err = -EBUSY;
			dev_err(mcs->dev, "MCS X2P calibration failed..ignoring\n");
			return err;
		}
	}

	val = mcs_reg_read(mcs, MCSX_MIL_RX_GBL_STATUS);
	for (i = 0; i < mcs->hw->mcs_x2p_intf; i++) {
		if (val & BIT_ULL(1 + i))
			continue;
		err = -EBUSY;
		dev_err(mcs->dev, "MCS:%d didn't respond to X2P calibration\n", i);
	}
	/* Clear X2P calibrate */
	mcs_reg_write(mcs, MCSX_MIL_GLOBAL, mcs_reg_read(mcs, MCSX_MIL_GLOBAL) & ~BIT_ULL(5));

	return err;
}

static void mcs_set_external_bypass(struct mcs *mcs, u8 bypass)
{
	u64 val;

	/* Set MCS to external bypass */
	val = mcs_reg_read(mcs, MCSX_MIL_GLOBAL);
	if (bypass)
		val |= BIT_ULL(6);
	else
		val &= ~BIT_ULL(6);
	mcs_reg_write(mcs, MCSX_MIL_GLOBAL, val);
}

static void mcs_global_cfg(struct mcs *mcs)
{
	/* Disable external bypass */
	mcs_set_external_bypass(mcs, false);

	/* Set MCS to perform standard IEEE802.1AE macsec processing */
	if (mcs->hw->mcs_blks == 1) {
		mcs_reg_write(mcs, MCSX_IP_MODE, BIT_ULL(3));
		return;
	}

	mcs_reg_write(mcs, MCSX_BBE_RX_SLAVE_CAL_ENTRY, 0xe4);
	mcs_reg_write(mcs, MCSX_BBE_RX_SLAVE_CAL_LEN, 4);
}

void cn10kb_mcs_set_hw_capabilities(struct mcs *mcs)
{
	struct hwinfo *hw = mcs->hw;

	hw->tcam_entries = 128;		/* TCAM entries */
	hw->secy_entries  = 128;	/* SecY entries */
	hw->sc_entries = 128;		/* SC CAM entries */
	hw->sa_entries = 256;		/* SA entries */
	hw->lmac_cnt = 20;		/* lmacs/ports per mcs block */
	hw->mcs_x2p_intf = 5;		/* x2p clabration intf */
	hw->mcs_blks = 1;		/* MCS blocks */
}

static struct mcs_ops cn10kb_mcs_ops = {
	.mcs_set_hw_capabilities	= cn10kb_mcs_set_hw_capabilities,
	.mcs_parser_cfg			= cn10kb_mcs_parser_cfg,
};

static int mcs_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	int lmac, err = 0;
	struct mcs *mcs;

	mcs = devm_kzalloc(dev, sizeof(*mcs), GFP_KERNEL);
	if (!mcs)
		return -ENOMEM;

	mcs->hw = devm_kzalloc(dev, sizeof(struct hwinfo), GFP_KERNEL);
	if (!mcs->hw)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto exit;
	}

	mcs->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!mcs->reg_base) {
		dev_err(dev, "mcs: Cannot map CSR memory space, aborting\n");
		err = -ENOMEM;
		goto exit;
	}

	pci_set_drvdata(pdev, mcs);
	mcs->pdev = pdev;
	mcs->dev = &pdev->dev;

	if (pdev->subsystem_device == PCI_SUBSYS_DEVID_CN10K_B)
		mcs->mcs_ops = &cn10kb_mcs_ops;
	else
		mcs->mcs_ops = cnf10kb_get_mac_ops();

	/* Set hardware capabilities */
	mcs->mcs_ops->mcs_set_hw_capabilities(mcs);

	mcs_global_cfg(mcs);

	/* Perform X2P clibration */
	err = mcs_x2p_calibration(mcs);
	if (err)
		goto err_x2p;

	mcs->mcs_id = (pci_resource_start(pdev, PCI_CFG_REG_BAR_NUM) >> 24)
			& MCS_ID_MASK;

	/* Set mcs tx side resources */
	err = mcs_alloc_struct_mem(mcs, &mcs->tx);
	if (err)
		goto err_x2p;

	/* Set mcs rx side resources */
	err = mcs_alloc_struct_mem(mcs, &mcs->rx);
	if (err)
		goto err_x2p;

	/* per port config */
	for (lmac = 0; lmac < mcs->hw->lmac_cnt; lmac++)
		mcs_lmac_init(mcs, lmac);

	/* Parser configuration */
	mcs->mcs_ops->mcs_parser_cfg(mcs);

	list_add(&mcs->mcs_list, &mcs_list);

	return 0;

err_x2p:
	/* Enable external bypass */
	mcs_set_external_bypass(mcs, true);
exit:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void mcs_remove(struct pci_dev *pdev)
{
	struct mcs *mcs = pci_get_drvdata(pdev);

	/* Set MCS to external bypass */
	mcs_set_external_bypass(mcs, true);
	pci_free_irq_vectors(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

struct pci_driver mcs_driver = {
	.name = DRV_NAME,
	.id_table = mcs_id_table,
	.probe = mcs_probe,
	.remove = mcs_remove,
};
