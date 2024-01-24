/*
 * TC956X ethernet driver.
 *
 * hwif.c
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

#include "common.h"
#include "tc956xmac.h"
#include "tc956xmac_ptp.h"
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
#include "tc956x_xpcs.h"
#include "tc956x_pma.h"
#endif
#endif

static u32 tc956xmac_get_id(struct tc956xmac_priv *priv, u32 id_reg)
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

#ifndef TC956X
static void tc956xmac_dwmac_mode_quirk(struct tc956xmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	if (priv->chain_mode) {
		dev_info(priv->device, "Chain mode enabled\n");
		priv->mode = TC956XMAC_CHAIN_MODE;
		mac->mode = &chain_mode_ops;
	} else {
		dev_info(priv->device, "Ring mode enabled\n");
		priv->mode = TC956XMAC_RING_MODE;
		mac->mode = &ring_mode_ops;
	}
}

static int tc956xmac_dwmac1_quirks(struct tc956xmac_priv *priv)
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

	tc956xmac_dwmac_mode_quirk(priv);
	return 0;
}

static int tc956xmac_dwmac4_quirks(struct tc956xmac_priv *priv)
{
	tc956xmac_dwmac_mode_quirk(priv);
	return 0;
}
#endif

static const struct tc956xmac_hwif_entry {
	bool gmac;
	bool gmac4;
	bool xgmac;
	u32 min_id;
	const struct tc956xmac_regs_off regs;
	const void *desc;
	const void *dma;
	const void *mac;
	const void *hwtimestamp;
	const void *mode;
	const void *tc;
	const void *mmc;
	const void *pma;
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
	const void *msi;
	const void *rsc;
	const void *mbx;
	const void *mbx_wrapper;
#endif
	int (*setup)(struct tc956xmac_priv *priv);
	int (*quirks)(struct tc956xmac_priv *priv);
} tc956xmac_hw[] = {
	/* NOTE: New HW versions shall go to the end of this table */
#ifndef TC956X
	{
		.gmac = false,
		.gmac4 = false,
		.xgmac = false,
		.min_id = 0,
		.regs = {
			.ptp_off = PTP_GMAC3_X_OFFSET_BASE,
			.mmc_off = MMC_GMAC3_X_OFFSET_BASE,
		},
		.desc = NULL,
		.dma = &dwmac100_dma_ops,
		.mac = &dwmac100_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = NULL,
		.tc = NULL,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac100_setup,
		.quirks = tc956xmac_dwmac1_quirks,
	}, {
		.gmac = true,
		.gmac4 = false,
		.xgmac = false,
		.min_id = 0,
		.regs = {
			.ptp_off = PTP_GMAC3_X_OFFSET_BASE,
			.mmc_off = MMC_GMAC3_X_OFFSET_BASE,
		},
		.desc = NULL,
		.dma = &dwmac1000_dma_ops,
		.mac = &dwmac1000_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = NULL,
		.tc = NULL,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac1000_setup,
		.quirks = tc956xmac_dwmac1_quirks,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = 0,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET_BASE,
			.mmc_off = MMC_GMAC4_OFFSET_BASE,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac4_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = NULL,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac4_setup,
		.quirks = tc956xmac_dwmac4_quirks,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = DWMAC_CORE_4_00,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET_BASE,
			.mmc_off = MMC_GMAC4_OFFSET_BASE,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac410_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = DWMAC_CORE_4_10,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET_BASE,
			.mmc_off = MMC_GMAC4_OFFSET_BASE,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac410_dma_ops,
		.mac = &dwmac410_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = true,
		.xgmac = false,
		.min_id = DWMAC_CORE_5_10,
		.regs = {
			.ptp_off = PTP_GMAC4_OFFSET_BASE,
			.mmc_off = MMC_GMAC4_OFFSET_BASE,
		},
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac410_dma_ops,
		.mac = &dwmac510_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwmac_mmc_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
#endif
	{
		.gmac = false,
		.gmac4 = false,
		.xgmac = true,
		.min_id = DWXGMAC_CORE_3_01,
		.regs = {
			.ptp_off = PTP_XGMAC_OFFSET_BASE,
			.mmc_off = MMC_XGMAC_OFFSET_BASE,
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
			.xpcs_off = XPCS_XGMAC_OFFSET,
			.pma_off = PMA_XGMAC_OFFSET,
#endif
#endif
		},
		.desc = &dwxgmac210_desc_ops,
		.dma = &dwxgmac210_dma_ops,
		.mac = &dwxgmac210_ops,
		.hwtimestamp = &tc956xmac_ptp,
		.mode = NULL,
		.tc = &dwmac510_tc_ops,
		.mmc = &dwxgmac_mmc_ops,
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
		.pma = &tc956x_pma_ops,
#endif
#endif
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
		.msi = &tc956x_msigen_ops,
		.rsc = &tc956xmac_rsc_mng_ops,
#endif

#if (defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)) | defined(TC956X_SRIOV_VF)
		.mbx = &tc956xmac_mbx_ops,
		.mbx_wrapper = &tc956xmac_mbx_wrapper_ops,
#endif
		.setup = dwxgmac2_setup,
		.quirks = NULL,
	},
};

int tc956xmac_hwif_init(struct tc956xmac_priv *priv)
{
	bool needs_xgmac = priv->plat->has_xgmac;
	bool needs_gmac4 = priv->plat->has_gmac4;
	bool needs_gmac = priv->plat->has_gmac;
	const struct tc956xmac_hwif_entry *entry;
	struct mac_device_info *mac;
	bool needs_setup = true;
	int i, ret;
	u32 id;
	u32 mac_offset_base = priv->port_num == RM_PF0_ID ?
					MAC0_BASE_OFFSET : MAC1_BASE_OFFSET;

	if (needs_gmac)
		id = tc956xmac_get_id(priv, GMAC_VERSION);
	else if (needs_gmac4 || needs_xgmac)
		id = tc956xmac_get_id(priv, GMAC4_VERSION);
	else
		id = 0;

	/* Save ID for later use */
	priv->synopsys_id = id;

	/* Lets assume some safe values first */
	priv->ptpaddr = priv->ioaddr +
		(needs_gmac4 ? PTP_GMAC4_OFFSET : PTP_GMAC3_X_OFFSET);
	priv->mmcaddr = priv->ioaddr +
		(needs_gmac4 ? MMC_GMAC4_OFFSET : MMC_GMAC3_X_OFFSET);
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
	priv->xpcsaddr = priv->ioaddr + XPCS_XGMAC_OFFSET;
	priv->pmaaddr = priv->ioaddr + PMA_XGMAC_OFFSET;
#endif
#endif

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
	for (i = ARRAY_SIZE(tc956xmac_hw) - 1; i >= 0; i--) {
		entry = &tc956xmac_hw[i];

		if (needs_gmac ^ entry->gmac)
			continue;
		if (needs_gmac4 ^ entry->gmac4)
			continue;
		if (needs_xgmac ^ entry->xgmac)
			continue;
		/* Use synopsys_id var because some setups can override this */
		if (priv->synopsys_id < entry->min_id)
			continue;

		/* Only use generic HW helpers if needed */
		mac->desc = mac->desc ? : entry->desc;
		mac->dma = mac->dma ? : entry->dma;
		mac->mac = mac->mac ? : entry->mac;
		mac->ptp = mac->ptp ? : entry->hwtimestamp;
		mac->mode = mac->mode ? : entry->mode;
		mac->tc = mac->tc ? : entry->tc;
		mac->mmc = mac->mmc ? : entry->mmc;
#ifndef TC956X_SRIOV_VF
		mac->pma = mac->pma ? : entry->pma;
#endif
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
		mac->msi = mac->msi ? : entry->msi;
		mac->rsc = mac->rsc ? : entry->rsc;
		mac->mbx = mac->mbx ? : entry->mbx;
		mac->mbx_wrapper = mac->mbx_wrapper ? : entry->mbx_wrapper;
#endif
		priv->hw = mac;
		priv->ptpaddr = priv->ioaddr + mac_offset_base + entry->regs.ptp_off;
		priv->mmcaddr = priv->ioaddr + mac_offset_base + entry->regs.mmc_off;
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
		priv->xpcsaddr = priv->ioaddr + mac_offset_base + entry->regs.xpcs_off;
		priv->pmaaddr = priv->ioaddr + mac_offset_base + entry->regs.pma_off;
#endif
#endif
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
