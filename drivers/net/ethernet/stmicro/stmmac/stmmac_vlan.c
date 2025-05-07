// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Altera Corporation
 * stmmac VLAN (802.1Q) handling
 */

#include "stmmac.h"
#include "stmmac_vlan.h"

static void dwmac4_write_single_vlan(struct net_device *dev, u16 vid)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 val;

	val = readl(ioaddr + GMAC_VLAN_TAG);
	val &= ~GMAC_VLAN_TAG_VID;
	val |= GMAC_VLAN_TAG_ETV | vid;

	writel(val, ioaddr + GMAC_VLAN_TAG);
}

static int dwmac4_write_vlan_filter(struct net_device *dev,
				    struct mac_device_info *hw,
				    u8 index, u32 data)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	int ret;
	u32 val;

	if (index >= hw->num_vlan)
		return -EINVAL;

	writel(data, ioaddr + GMAC_VLAN_TAG_DATA);

	val = readl(ioaddr + GMAC_VLAN_TAG);
	val &= ~(GMAC_VLAN_TAG_CTRL_OFS_MASK |
		GMAC_VLAN_TAG_CTRL_CT |
		GMAC_VLAN_TAG_CTRL_OB);
	val |= (index << GMAC_VLAN_TAG_CTRL_OFS_SHIFT) | GMAC_VLAN_TAG_CTRL_OB;

	writel(val, ioaddr + GMAC_VLAN_TAG);

	ret = readl_poll_timeout(ioaddr + GMAC_VLAN_TAG, val,
				 !(val & GMAC_VLAN_TAG_CTRL_OB),
				 1000, 500000);
	if (ret) {
		netdev_err(dev, "Timeout accessing MAC_VLAN_Tag_Filter\n");
		return -EBUSY;
	}

	return 0;
}

static int dwmac4_add_hw_vlan_rx_fltr(struct net_device *dev,
				      struct mac_device_info *hw,
				      __be16 proto, u16 vid)
{
	int index = -1;
	u32 val = 0;
	int i, ret;

	if (vid > 4095)
		return -EINVAL;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		/* For single VLAN filter, VID 0 means VLAN promiscuous */
		if (vid == 0) {
			netdev_warn(dev, "Adding VLAN ID 0 is not supported\n");
			return -EPERM;
		}

		if (hw->vlan_filter[0] & GMAC_VLAN_TAG_VID) {
			netdev_err(dev, "Only single VLAN ID supported\n");
			return -EPERM;
		}

		hw->vlan_filter[0] = vid;
		dwmac4_write_single_vlan(dev, vid);

		return 0;
	}

	/* Extended Rx VLAN Filter Enable */
	val |= GMAC_VLAN_TAG_DATA_ETV | GMAC_VLAN_TAG_DATA_VEN | vid;

	for (i = 0; i < hw->num_vlan; i++) {
		if (hw->vlan_filter[i] == val)
			return 0;
		else if (!(hw->vlan_filter[i] & GMAC_VLAN_TAG_DATA_VEN))
			index = i;
	}

	if (index == -1) {
		netdev_err(dev, "MAC_VLAN_Tag_Filter full (size: %0u)\n",
			   hw->num_vlan);
		return -EPERM;
	}

	ret = dwmac4_write_vlan_filter(dev, hw, index, val);

	if (!ret)
		hw->vlan_filter[index] = val;

	return ret;
}

static int dwmac4_del_hw_vlan_rx_fltr(struct net_device *dev,
				      struct mac_device_info *hw,
				      __be16 proto, u16 vid)
{
	int i, ret = 0;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		if ((hw->vlan_filter[0] & GMAC_VLAN_TAG_VID) == vid) {
			hw->vlan_filter[0] = 0;
			dwmac4_write_single_vlan(dev, 0);
		}
		return 0;
	}

	/* Extended Rx VLAN Filter Enable */
	for (i = 0; i < hw->num_vlan; i++) {
		if ((hw->vlan_filter[i] & GMAC_VLAN_TAG_DATA_VID) == vid) {
			ret = dwmac4_write_vlan_filter(dev, hw, i, 0);

			if (!ret)
				hw->vlan_filter[i] = 0;
			else
				return ret;
		}
	}

	return ret;
}

static void dwmac4_restore_hw_vlan_rx_fltr(struct net_device *dev,
					   struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	u32 hash;
	u32 val;
	int i;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		dwmac4_write_single_vlan(dev, hw->vlan_filter[0]);
		return;
	}

	/* Extended Rx VLAN Filter Enable */
	for (i = 0; i < hw->num_vlan; i++) {
		if (hw->vlan_filter[i] & GMAC_VLAN_TAG_DATA_VEN) {
			val = hw->vlan_filter[i];
			dwmac4_write_vlan_filter(dev, hw, i, val);
		}
	}

	hash = readl(ioaddr + GMAC_VLAN_HASH_TABLE);
	if (hash & GMAC_VLAN_VLHT) {
		value = readl(ioaddr + GMAC_VLAN_TAG);
		value |= GMAC_VLAN_VTHM;
		writel(value, ioaddr + GMAC_VLAN_TAG);
	}
}

static void dwmac4_update_vlan_hash(struct mac_device_info *hw, u32 hash,
				    u16 perfect_match, bool is_double)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	writel(hash, ioaddr + GMAC_VLAN_HASH_TABLE);

	value = readl(ioaddr + GMAC_VLAN_TAG);

	if (hash) {
		value |= GMAC_VLAN_VTHM | GMAC_VLAN_ETV;
		if (is_double) {
			value |= GMAC_VLAN_EDVLP;
			value |= GMAC_VLAN_ESVL;
			value |= GMAC_VLAN_DOVLTC;
		}

		writel(value, ioaddr + GMAC_VLAN_TAG);
	} else if (perfect_match) {
		u32 value = GMAC_VLAN_ETV;

		if (is_double) {
			value |= GMAC_VLAN_EDVLP;
			value |= GMAC_VLAN_ESVL;
			value |= GMAC_VLAN_DOVLTC;
		}

		writel(value | perfect_match, ioaddr + GMAC_VLAN_TAG);
	} else {
		value &= ~(GMAC_VLAN_VTHM | GMAC_VLAN_ETV);
		value &= ~(GMAC_VLAN_EDVLP | GMAC_VLAN_ESVL);
		value &= ~GMAC_VLAN_DOVLTC;
		value &= ~GMAC_VLAN_VID;

		writel(value, ioaddr + GMAC_VLAN_TAG);
	}
}

static void dwmac4_enable_vlan(struct mac_device_info *hw, u32 type)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + GMAC_VLAN_INCL);
	value |= GMAC_VLAN_VLTI;
	value |= GMAC_VLAN_CSVL; /* Only use SVLAN */
	value &= ~GMAC_VLAN_VLC;
	value |= (type << GMAC_VLAN_VLC_SHIFT) & GMAC_VLAN_VLC;
	writel(value, ioaddr + GMAC_VLAN_INCL);
}

static void dwmac4_rx_hw_vlan(struct mac_device_info *hw,
			      struct dma_desc *rx_desc, struct sk_buff *skb)
{
	if (hw->desc->get_rx_vlan_valid(rx_desc)) {
		u16 vid = hw->desc->get_rx_vlan_tci(rx_desc);

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}
}

static void dwmac4_set_hw_vlan_mode(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_VLAN_TAG);

	value &= ~GMAC_VLAN_TAG_CTRL_EVLS_MASK;

	if (hw->hw_vlan_en)
		/* Always strip VLAN on Receive */
		value |= GMAC_VLAN_TAG_STRIP_ALL;
	else
		/* Do not strip VLAN on Receive */
		value |= GMAC_VLAN_TAG_STRIP_NONE;

	/* Enable outer VLAN Tag in Rx DMA descriptor */
	value |= GMAC_VLAN_TAG_CTRL_EVLRXS;
	writel(value, ioaddr + GMAC_VLAN_TAG);
}

static void dwxgmac2_update_vlan_hash(struct mac_device_info *hw, u32 hash,
				      u16 perfect_match, bool is_double)
{
	void __iomem *ioaddr = hw->pcsr;

	writel(hash, ioaddr + XGMAC_VLAN_HASH_TABLE);

	if (hash) {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + XGMAC_VLAN_TAG);

		value |= XGMAC_VLAN_VTHM | XGMAC_VLAN_ETV;
		if (is_double) {
			value |= XGMAC_VLAN_EDVLP;
			value |= XGMAC_VLAN_ESVL;
			value |= XGMAC_VLAN_DOVLTC;
		} else {
			value &= ~XGMAC_VLAN_EDVLP;
			value &= ~XGMAC_VLAN_ESVL;
			value &= ~XGMAC_VLAN_DOVLTC;
		}

		value &= ~XGMAC_VLAN_VID;
		writel(value, ioaddr + XGMAC_VLAN_TAG);
	} else if (perfect_match) {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + XGMAC_VLAN_TAG);

		value &= ~XGMAC_VLAN_VTHM;
		value |= XGMAC_VLAN_ETV;
		if (is_double) {
			value |= XGMAC_VLAN_EDVLP;
			value |= XGMAC_VLAN_ESVL;
			value |= XGMAC_VLAN_DOVLTC;
		} else {
			value &= ~XGMAC_VLAN_EDVLP;
			value &= ~XGMAC_VLAN_ESVL;
			value &= ~XGMAC_VLAN_DOVLTC;
		}

		value &= ~XGMAC_VLAN_VID;
		writel(value | perfect_match, ioaddr + XGMAC_VLAN_TAG);
	} else {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value &= ~XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + XGMAC_VLAN_TAG);

		value &= ~(XGMAC_VLAN_VTHM | XGMAC_VLAN_ETV);
		value &= ~(XGMAC_VLAN_EDVLP | XGMAC_VLAN_ESVL);
		value &= ~XGMAC_VLAN_DOVLTC;
		value &= ~XGMAC_VLAN_VID;

		writel(value, ioaddr + XGMAC_VLAN_TAG);
	}
}

static void dwxgmac2_enable_vlan(struct mac_device_info *hw, u32 type)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_VLAN_INCL);
	value |= XGMAC_VLAN_VLTI;
	value |= XGMAC_VLAN_CSVL; /* Only use SVLAN */
	value &= ~XGMAC_VLAN_VLC;
	value |= (type << XGMAC_VLAN_VLC_SHIFT) & XGMAC_VLAN_VLC;
	writel(value, ioaddr + XGMAC_VLAN_INCL);
}

const struct stmmac_vlan_ops dwmac4_vlan_ops = {
	.update_vlan_hash = dwmac4_update_vlan_hash,
	.enable_vlan = dwmac4_enable_vlan,
	.add_hw_vlan_rx_fltr = dwmac4_add_hw_vlan_rx_fltr,
	.del_hw_vlan_rx_fltr = dwmac4_del_hw_vlan_rx_fltr,
	.restore_hw_vlan_rx_fltr = dwmac4_restore_hw_vlan_rx_fltr,
	.rx_hw_vlan = dwmac4_rx_hw_vlan,
	.set_hw_vlan_mode = dwmac4_set_hw_vlan_mode,
};

const struct stmmac_vlan_ops dwmac410_vlan_ops = {
	.update_vlan_hash = dwmac4_update_vlan_hash,
	.enable_vlan = dwmac4_enable_vlan,
	.add_hw_vlan_rx_fltr = dwmac4_add_hw_vlan_rx_fltr,
	.del_hw_vlan_rx_fltr = dwmac4_del_hw_vlan_rx_fltr,
	.restore_hw_vlan_rx_fltr = dwmac4_restore_hw_vlan_rx_fltr,
	.rx_hw_vlan = dwmac4_rx_hw_vlan,
	.set_hw_vlan_mode = dwmac4_set_hw_vlan_mode,
};

const struct stmmac_vlan_ops dwmac510_vlan_ops = {
	.update_vlan_hash = dwmac4_update_vlan_hash,
	.enable_vlan = dwmac4_enable_vlan,
	.add_hw_vlan_rx_fltr = dwmac4_add_hw_vlan_rx_fltr,
	.del_hw_vlan_rx_fltr = dwmac4_del_hw_vlan_rx_fltr,
	.restore_hw_vlan_rx_fltr = dwmac4_restore_hw_vlan_rx_fltr,
	.rx_hw_vlan = dwmac4_rx_hw_vlan,
	.set_hw_vlan_mode = dwmac4_set_hw_vlan_mode,
};

const struct stmmac_vlan_ops dwxgmac210_vlan_ops = {
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.enable_vlan = dwxgmac2_enable_vlan,
};

const struct stmmac_vlan_ops dwxlgmac2_vlan_ops = {
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.enable_vlan = dwxgmac2_enable_vlan,
};

u32 dwmac4_get_num_vlan(void __iomem *ioaddr)
{
	u32 val, num_vlan;

	val = readl(ioaddr + GMAC_HW_FEATURE3);
	switch (val & GMAC_HW_FEAT_NRVF) {
	case 0:
		num_vlan = 1;
		break;
	case 1:
		num_vlan = 4;
		break;
	case 2:
		num_vlan = 8;
		break;
	case 3:
		num_vlan = 16;
		break;
	case 4:
		num_vlan = 24;
		break;
	case 5:
		num_vlan = 32;
		break;
	default:
		num_vlan = 1;
	}

	return num_vlan;
}
