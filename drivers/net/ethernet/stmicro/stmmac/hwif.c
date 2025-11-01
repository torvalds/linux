// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac HW Interface Handling
 */

#include "common.h"
#include "stmmac.h"
#include "stmmac_fpe.h"
#include "stmmac_ptp.h"
#include "stmmac_est.h"
#include "stmmac_vlan.h"
#include "dwmac4_descs.h"
#include "dwxgmac2.h"

static u32 stmmac_get_id(struct stmmac_priv *priv, u32 id_reg)
{
	u32 reg = readl(priv->ioaddr + id_reg);

	if (!reg) {
		dev_info(priv->device, "Version ID not available\n");
		return 0x0;
	}

	dev_info(priv->device, "User ID: 0x%x, Synopsys ID: 0x%x\n",
			(unsigned int)(reg & GENMASK(15, 8)) >> 8,
			(unsigned int)(reg & GENMASK(7, 0)));
	return reg & GENMASK(7, 0);
}

static u32 stmmac_get_dev_id(struct stmmac_priv *priv, u32 id_reg)
{
	u32 reg = readl(priv->ioaddr + id_reg);

	if (!reg) {
		dev_info(priv->device, "Version ID not available\n");
		return 0x0;
	}

	return (reg & GENMASK(15, 8)) >> 8;
}

static void stmmac_dwmac_mode_quirk(struct stmmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	if (priv->chain_mode) {
		dev_info(priv->device, "Chain mode enabled\n");
		priv->mode = STMMAC_CHAIN_MODE;
		mac->mode = &chain_mode_ops;
	} else {
		dev_info(priv->device, "Ring mode enabled\n");
		priv->mode = STMMAC_RING_MODE;
		mac->mode = &ring_mode_ops;
	}
}

static int stmmac_dwmac1_quirks(struct stmmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	if (priv->plat->enh_desc) {
		dev_info(priv->device, "Enhanced/Alternate descriptors\n");

		/* GMAC older than 3.50 has no extended descriptors */
		if (priv->synopsys_id >= DWMAC_CORE_3_50) {
			dev_info(priv->device, "Enabled extended descriptors\n");
			priv->extend_desc = 1;
		} else {
			dev_warn(priv->device, "Extended descriptors not supported\n");
		}

		mac->desc = &enh_desc_ops;
	} else {
		dev_info(priv->device, "Normal descriptors\n");
		mac->desc = &ndesc_ops;
	}

	stmmac_dwmac_mode_quirk(priv);
	return 0;
}

static int stmmac_dwmac4_quirks(struct stmmac_priv *priv)
{
	stmmac_dwmac_mode_quirk(priv);
	return 0;
}

static int stmmac_dwxlgmac_quirks(struct stmmac_priv *priv)
{
	priv->hw->xlgmac = true;
	return 0;
}

int stmmac_reset(struct stmmac_priv *priv, void __iomem *ioaddr)
{
	struct plat_stmmacenet_data *plat = priv ? priv->plat : NULL;

	if (!priv)
		return -EINVAL;

	if (plat && plat->fix_soc_reset)
		return plat->fix_soc_reset(priv, ioaddr);

	return stmmac_do_callback(priv, dma, reset, ioaddr);
}

static const struct stmmac_hwif_entry {
	bool gmac;
	bool gmac4;
	bool xgmac;
	u32 min_id;
	u32 dev_id;
	const struct stmmac_regs_off regs;
	const void *desc;
	const void *dma;
	const void *mac;
	const void *hwtimestamp;
	const void *ptp;
	const void *mode;
	const void *tc;
	const void *mmc;
	const void *est;
	const void *vlan;
	int (*setup)(struct stmmac_priv *priv);
	int (*quirks)(struct stmmac_priv *priv);
} stmmac_hw[] = {
	/* NOTE: New HW versions shall go to the end of this table */
	{
		.gmac = false,
		.gmac4 = false,
		.xgmac = false,
		.min_id = 0,
		.regs = {
			.ptp_off = PTP_GMAC3_X_OFFSET,
			.mmc_off = MMC_GMAC3_X_OFFSET,
		},
		.desc = NULL,
		.dma = &dwmac100_dma_ops,
		.mac = &dwmac100_ops,
		.hwtimestamp = &dwmac1000_ptp,
		.ptp = &dwmac1000_ptp_clock_ops,
		.mode = NULL,
		.tc = NULL,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac100_setup,
		.quirks = stmmac_dwmac1_quirks,
	}, {
		.gmac = true,
		.gmac4 = false,
		.xgmac = false,
		.min_id = 0,
		.regs = {
			.ptp_off = PTP_GMAC3_X_OFFSET,
			.mmc_off = MMC_GMAC3_X_OFFSET,
		},
		.desc = NULL,
		.dma = &dwmac1000_dma_ops,
		.mac = &dwmac1000_ops,
		.hwtimestamp = &dwmac1000_ptp,
		.ptp = &dwmac1000_ptp_clock_ops,
		.mode = NULL,
		.tc = NULL,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac1000_setup,
		.quirks = stmmac_dwmac1_quirks,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = 0,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET,
			.mmc_off = MMC_GMAC4_OFFSET,
			.est_off = EST_GMAC4_OFFSET,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac4_ops,
		.vlan = &dwmac_vlan_ops,
		.hwtimestamp = &stmmac_ptp,
		.ptp = &stmmac_ptp_clock_ops,
		.mode = NULL,
		.tc = &dwmac4_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.est = &dwmac510_est_ops,
		.setup = dwmac4_setup,
		.quirks = stmmac_dwmac4_quirks,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = DWMAC_CORE_4_00,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET,
			.mmc_off = MMC_GMAC4_OFFSET,
			.est_off = EST_GMAC4_OFFSET,
			.fpe_reg = &dwmac5_fpe_reg,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac410_ops,
		.vlan = &dwmac_vlan_ops,
		.hwtimestamp = &stmmac_ptp,
		.ptp = &stmmac_ptp_clock_ops,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.est = &dwmac510_est_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = DWMAC_CORE_4_10,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET,
			.mmc_off = MMC_GMAC4_OFFSET,
			.est_off = EST_GMAC4_OFFSET,
			.fpe_reg = &dwmac5_fpe_reg,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac410_dma_ops,
		.mac = &dwmac410_ops,
		.vlan = &dwmac_vlan_ops,
		.hwtimestamp = &stmmac_ptp,
		.ptp = &stmmac_ptp_clock_ops,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.est = &dwmac510_est_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = DWMAC_CORE_5_10,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET,
			.mmc_off = MMC_GMAC4_OFFSET,
			.est_off = EST_GMAC4_OFFSET,
			.fpe_reg = &dwmac5_fpe_reg,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac410_dma_ops,
		.mac = &dwmac510_ops,
		.vlan = &dwmac_vlan_ops,
		.hwtimestamp = &stmmac_ptp,
		.ptp = &stmmac_ptp_clock_ops,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.est = &dwmac510_est_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = false,
		.xgmac = true,
		.min_id = DWXGMAC_CORE_2_10,
		.dev_id = DWXGMAC_ID,
		.regs = {
			.ptp_off = PTP_XGMAC_OFFSET,
			.mmc_off = MMC_XGMAC_OFFSET,
			.est_off = EST_XGMAC_OFFSET,
			.fpe_reg = &dwxgmac3_fpe_reg,
		},
		.desc = &dwxgmac210_desc_ops,
		.dma = &dwxgmac210_dma_ops,
		.mac = &dwxgmac210_ops,
		.vlan = &dwxgmac210_vlan_ops,
		.hwtimestamp = &stmmac_ptp,
		.ptp = &stmmac_ptp_clock_ops,
		.mode = NULL,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwxgmac_mmc_ops,
		.est = &dwmac510_est_ops,
		.setup = dwxgmac2_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = false,
		.xgmac = true,
		.min_id = DWXLGMAC_CORE_2_00,
		.dev_id = DWXLGMAC_ID,
		.regs = {
			.ptp_off = PTP_XGMAC_OFFSET,
			.mmc_off = MMC_XGMAC_OFFSET,
			.est_off = EST_XGMAC_OFFSET,
			.fpe_reg = &dwxgmac3_fpe_reg,
		},
		.desc = &dwxgmac210_desc_ops,
		.dma = &dwxgmac210_dma_ops,
		.mac = &dwxlgmac2_ops,
		.vlan = &dwxlgmac2_vlan_ops,
		.hwtimestamp = &stmmac_ptp,
		.ptp = &stmmac_ptp_clock_ops,
		.mode = NULL,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwxgmac_mmc_ops,
		.est = &dwmac510_est_ops,
		.setup = dwxlgmac2_setup,
		.quirks = stmmac_dwxlgmac_quirks,
	},
};

int stmmac_hwif_init(struct stmmac_priv *priv)
{
	bool needs_xgmac = priv->plat->has_xgmac;
	bool needs_gmac4 = priv->plat->has_gmac4;
	bool needs_gmac = priv->plat->has_gmac;
	const struct stmmac_hwif_entry *entry;
	struct mac_device_info *mac;
	bool needs_setup = true;
	u32 id, dev_id = 0;
	int i, ret;

	if (needs_gmac) {
		id = stmmac_get_id(priv, GMAC_VERSION);
	} else if (needs_gmac4 || needs_xgmac) {
		id = stmmac_get_id(priv, GMAC4_VERSION);
		if (needs_xgmac)
			dev_id = stmmac_get_dev_id(priv, GMAC4_VERSION);
	} else {
		id = 0;
	}

	/* Save ID for later use */
	priv->synopsys_id = id;

	/* Lets assume some safe values first */
	priv->ptpaddr = priv->ioaddr +
		(needs_gmac4 ? PTP_GMAC4_OFFSET : PTP_GMAC3_X_OFFSET);
	priv->mmcaddr = priv->ioaddr +
		(needs_gmac4 ? MMC_GMAC4_OFFSET : MMC_GMAC3_X_OFFSET);
	if (needs_gmac4)
		priv->estaddr = priv->ioaddr + EST_GMAC4_OFFSET;
	else if (needs_xgmac)
		priv->estaddr = priv->ioaddr + EST_XGMAC_OFFSET;

	/* Check for HW specific setup first */
	if (priv->plat->setup) {
		mac = priv->plat->setup(priv);
		needs_setup = false;
	} else {
		mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);
	}

	if (!mac)
		return -ENOMEM;

	/* Fallback to generic HW */
	for (i = ARRAY_SIZE(stmmac_hw) - 1; i >= 0; i--) {
		entry = &stmmac_hw[i];

		if (needs_gmac ^ entry->gmac)
			continue;
		if (needs_gmac4 ^ entry->gmac4)
			continue;
		if (needs_xgmac ^ entry->xgmac)
			continue;
		/* Use synopsys_id var because some setups can override this */
		if (priv->synopsys_id < entry->min_id)
			continue;
		if (needs_xgmac && (dev_id ^ entry->dev_id))
			continue;

		/* Only use generic HW helpers if needed */
		mac->desc = mac->desc ? : entry->desc;
		mac->dma = mac->dma ? : entry->dma;
		mac->mac = mac->mac ? : entry->mac;
		mac->ptp = mac->ptp ? : entry->hwtimestamp;
		mac->mode = mac->mode ? : entry->mode;
		mac->tc = mac->tc ? : entry->tc;
		mac->mmc = mac->mmc ? : entry->mmc;
		mac->est = mac->est ? : entry->est;
		mac->vlan = mac->vlan ? : entry->vlan;

		priv->hw = mac;
		priv->fpe_cfg.reg = entry->regs.fpe_reg;
		priv->ptpaddr = priv->ioaddr + entry->regs.ptp_off;
		priv->mmcaddr = priv->ioaddr + entry->regs.mmc_off;
		memcpy(&priv->ptp_clock_ops, entry->ptp,
		       sizeof(struct ptp_clock_info));
		if (entry->est)
			priv->estaddr = priv->ioaddr + entry->regs.est_off;

		/* Entry found */
		if (needs_setup) {
			ret = entry->setup(priv);
			if (ret)
				return ret;
		}

		/* Save quirks, if needed for posterior use */
		priv->hwif_quirks = entry->quirks;
		return 0;
	}

	dev_err(priv->device, "Failed to find HW IF (id=0x%x, gmac=%d/%d)\n",
			id, needs_gmac, needs_gmac4);
	return -EINVAL;
}
