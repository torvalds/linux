// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac HW Interface Handling
 */

#include "common.h"
#include "stmmac.h"

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

static const struct stmmac_hwif_entry {
	bool gmac;
	bool gmac4;
	u32 min_id;
	const void *desc;
	const void *dma;
	const void *mac;
	const void *hwtimestamp;
	const void *mode;
	const void *tc;
	int (*setup)(struct stmmac_priv *priv);
	int (*quirks)(struct stmmac_priv *priv);
} stmmac_hw[] = {
	/* NOTE: New HW versions shall go to the end of this table */
	{
		.gmac = false,
		.gmac4 = false,
		.min_id = 0,
		.desc = NULL,
		.dma = &dwmac100_dma_ops,
		.mac = &dwmac100_ops,
		.hwtimestamp = &stmmac_ptp,
		.mode = NULL,
		.tc = NULL,
		.setup = dwmac100_setup,
		.quirks = stmmac_dwmac1_quirks,
	}, {
		.gmac = true,
		.gmac4 = false,
		.min_id = 0,
		.desc = NULL,
		.dma = &dwmac1000_dma_ops,
		.mac = &dwmac1000_ops,
		.hwtimestamp = &stmmac_ptp,
		.mode = NULL,
		.tc = NULL,
		.setup = dwmac1000_setup,
		.quirks = stmmac_dwmac1_quirks,
	}, {
		.gmac = false,
		.gmac4 = true,
		.min_id = 0,
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac4_ops,
		.hwtimestamp = &stmmac_ptp,
		.mode = NULL,
		.tc = NULL,
		.setup = dwmac4_setup,
		.quirks = stmmac_dwmac4_quirks,
	}, {
		.gmac = false,
		.gmac4 = true,
		.min_id = DWMAC_CORE_4_00,
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac410_ops,
		.hwtimestamp = &stmmac_ptp,
		.mode = &dwmac4_ring_mode_ops,
		.tc = NULL,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = true,
		.min_id = DWMAC_CORE_4_10,
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac410_dma_ops,
		.mac = &dwmac410_ops,
		.hwtimestamp = &stmmac_ptp,
		.mode = &dwmac4_ring_mode_ops,
		.tc = NULL,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}, {
		.gmac = false,
		.gmac4 = true,
		.min_id = DWMAC_CORE_5_10,
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac410_dma_ops,
		.mac = &dwmac510_ops,
		.hwtimestamp = &stmmac_ptp,
		.mode = &dwmac4_ring_mode_ops,
		.tc = &dwmac510_tc_ops,
		.setup = dwmac4_setup,
		.quirks = NULL,
	}
};

int stmmac_hwif_init(struct stmmac_priv *priv)
{
	bool needs_gmac4 = priv->plat->has_gmac4;
	bool needs_gmac = priv->plat->has_gmac;
	const struct stmmac_hwif_entry *entry;
	struct mac_device_info *mac;
	int i, ret;
	u32 id;

	if (needs_gmac) {
		id = stmmac_get_id(priv, GMAC_VERSION);
	} else {
		id = stmmac_get_id(priv, GMAC4_VERSION);
	}

	/* Save ID for later use */
	priv->synopsys_id = id;

	/* Check for HW specific setup first */
	if (priv->plat->setup) {
		priv->hw = priv->plat->setup(priv);
		if (!priv->hw)
			return -ENOMEM;
		return 0;
	}

	mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return -ENOMEM;

	/* Fallback to generic HW */
	for (i = ARRAY_SIZE(stmmac_hw) - 1; i >= 0; i--) {
		entry = &stmmac_hw[i];

		if (needs_gmac ^ entry->gmac)
			continue;
		if (needs_gmac4 ^ entry->gmac4)
			continue;
		if (id < entry->min_id)
			continue;

		mac->desc = entry->desc;
		mac->dma = entry->dma;
		mac->mac = entry->mac;
		mac->ptp = entry->hwtimestamp;
		mac->mode = entry->mode;
		mac->tc = entry->tc;

		priv->hw = mac;

		/* Entry found */
		ret = entry->setup(priv);
		if (ret)
			return ret;

		/* Run quirks, if needed */
		if (entry->quirks) {
			ret = entry->quirks(priv);
			if (ret)
				return ret;
		}

		return 0;
	}

	dev_err(priv->device, "Failed to find HW IF (id=0x%x, gmac=%d/%d)\n",
			id, needs_gmac, needs_gmac4);
	return -EINVAL;
}
