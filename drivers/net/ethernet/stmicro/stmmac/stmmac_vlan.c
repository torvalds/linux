// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Altera Corporation
 * stmmac VLAN (802.1Q) handling
 */

#include "stmmac.h"
#include "stmmac_vlan.h"

static void vlan_write_single(struct net_device *dev, u16 vid)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 val;

	val = readl(ioaddr + VLAN_TAG);
	val &= ~VLAN_TAG_VID;
	val |= VLAN_TAG_ETV | vid;

	writel(val, ioaddr + VLAN_TAG);
}

static int vlan_write_filter(struct net_device *dev,
			     struct mac_device_info *hw,
			     u8 index, u32 data)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	int ret;
	u32 val;

	if (index >= hw->num_vlan)
		return -EINVAL;

	writel(data, ioaddr + VLAN_TAG_DATA);

	val = readl(ioaddr + VLAN_TAG);
	val &= ~(VLAN_TAG_CTRL_OFS_MASK |
		VLAN_TAG_CTRL_CT |
		VLAN_TAG_CTRL_OB);
	val |= (index << VLAN_TAG_CTRL_OFS_SHIFT) | VLAN_TAG_CTRL_OB;

	writel(val, ioaddr + VLAN_TAG);

	ret = readl_poll_timeout(ioaddr + VLAN_TAG, val,
				 !(val & VLAN_TAG_CTRL_OB),
				 1000, 500000);
	if (ret) {
		netdev_err(dev, "Timeout accessing MAC_VLAN_Tag_Filter\n");
		return -EBUSY;
	}

	return 0;
}

static int vlan_add_hw_rx_fltr(struct net_device *dev,
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

		if (hw->vlan_filter[0] & VLAN_TAG_VID) {
			netdev_err(dev, "Only single VLAN ID supported\n");
			return -EPERM;
		}

		hw->vlan_filter[0] = vid;
		vlan_write_single(dev, vid);

		return 0;
	}

	/* Extended Rx VLAN Filter Enable */
	val |= VLAN_TAG_DATA_ETV | VLAN_TAG_DATA_VEN | vid;

	for (i = 0; i < hw->num_vlan; i++) {
		if (hw->vlan_filter[i] == val)
			return 0;
		else if (!(hw->vlan_filter[i] & VLAN_TAG_DATA_VEN))
			index = i;
	}

	if (index == -1) {
		netdev_err(dev, "MAC_VLAN_Tag_Filter full (size: %0u)\n",
			   hw->num_vlan);
		return -EPERM;
	}

	ret = vlan_write_filter(dev, hw, index, val);

	if (!ret)
		hw->vlan_filter[index] = val;

	return ret;
}

static int vlan_del_hw_rx_fltr(struct net_device *dev,
			       struct mac_device_info *hw,
			       __be16 proto, u16 vid)
{
	int i, ret = 0;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		if ((hw->vlan_filter[0] & VLAN_TAG_VID) == vid) {
			hw->vlan_filter[0] = 0;
			vlan_write_single(dev, 0);
		}
		return 0;
	}

	/* Extended Rx VLAN Filter Enable */
	for (i = 0; i < hw->num_vlan; i++) {
		if ((hw->vlan_filter[i] & VLAN_TAG_DATA_VID) == vid) {
			ret = vlan_write_filter(dev, hw, i, 0);

			if (!ret)
				hw->vlan_filter[i] = 0;
			else
				return ret;
		}
	}

	return ret;
}

static void vlan_restore_hw_rx_fltr(struct net_device *dev,
				    struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	u32 hash;
	u32 val;
	int i;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		vlan_write_single(dev, hw->vlan_filter[0]);
		return;
	}

	/* Extended Rx VLAN Filter Enable */
	for (i = 0; i < hw->num_vlan; i++) {
		if (hw->vlan_filter[i] & VLAN_TAG_DATA_VEN) {
			val = hw->vlan_filter[i];
			vlan_write_filter(dev, hw, i, val);
		}
	}

	hash = readl(ioaddr + VLAN_HASH_TABLE);
	if (hash & VLAN_VLHT) {
		value = readl(ioaddr + VLAN_TAG);
		value |= VLAN_VTHM;
		writel(value, ioaddr + VLAN_TAG);
	}
}

static void vlan_update_hash(struct mac_device_info *hw, u32 hash,
			     u16 perfect_match, bool is_double)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	writel(hash, ioaddr + VLAN_HASH_TABLE);

	value = readl(ioaddr + VLAN_TAG);

	if (hash) {
		value |= VLAN_VTHM | VLAN_ETV;
		if (is_double) {
			value |= VLAN_EDVLP;
			value |= VLAN_ESVL;
			value |= VLAN_DOVLTC;
		}

		writel(value, ioaddr + VLAN_TAG);
	} else if (perfect_match) {
		u32 value = VLAN_ETV;

		if (is_double) {
			value |= VLAN_EDVLP;
			value |= VLAN_ESVL;
			value |= VLAN_DOVLTC;
		}

		writel(value | perfect_match, ioaddr + VLAN_TAG);
	} else {
		value &= ~(VLAN_VTHM | VLAN_ETV);
		value &= ~(VLAN_EDVLP | VLAN_ESVL);
		value &= ~VLAN_DOVLTC;
		value &= ~VLAN_VID;

		writel(value, ioaddr + VLAN_TAG);
	}
}

static void vlan_enable(struct mac_device_info *hw, u32 type)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + VLAN_INCL);
	value |= VLAN_VLTI;
	value &= ~VLAN_CSVL; /* Only use CVLAN */
	value &= ~VLAN_VLC;
	value |= (type << VLAN_VLC_SHIFT) & VLAN_VLC;
	writel(value, ioaddr + VLAN_INCL);
}

static void vlan_rx_hw(struct mac_device_info *hw,
		       struct dma_desc *rx_desc, struct sk_buff *skb)
{
	if (hw->desc->get_rx_vlan_valid(rx_desc)) {
		u16 vid = hw->desc->get_rx_vlan_tci(rx_desc);

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}
}

static void vlan_set_hw_mode(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + VLAN_TAG);

	value &= ~VLAN_TAG_CTRL_EVLS_MASK;

	if (hw->hw_vlan_en)
		/* Always strip VLAN on Receive */
		value |= VLAN_TAG_STRIP_ALL;
	else
		/* Do not strip VLAN on Receive */
		value |= VLAN_TAG_STRIP_NONE;

	/* Enable outer VLAN Tag in Rx DMA descriptor */
	value |= VLAN_TAG_CTRL_EVLRXS;
	writel(value, ioaddr + VLAN_TAG);
}

static void dwxgmac2_update_vlan_hash(struct mac_device_info *hw, u32 hash,
				      u16 perfect_match, bool is_double)
{
	void __iomem *ioaddr = hw->pcsr;

	writel(hash, ioaddr + VLAN_HASH_TABLE);

	if (hash) {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + VLAN_TAG);

		value |= VLAN_VTHM | VLAN_ETV;
		if (is_double) {
			value |= VLAN_EDVLP;
			value |= VLAN_ESVL;
			value |= VLAN_DOVLTC;
		} else {
			value &= ~VLAN_EDVLP;
			value &= ~VLAN_ESVL;
			value &= ~VLAN_DOVLTC;
		}

		value &= ~VLAN_VID;
		writel(value, ioaddr + VLAN_TAG);
	} else if (perfect_match) {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + VLAN_TAG);

		value &= ~VLAN_VTHM;
		value |= VLAN_ETV;
		if (is_double) {
			value |= VLAN_EDVLP;
			value |= VLAN_ESVL;
			value |= VLAN_DOVLTC;
		} else {
			value &= ~VLAN_EDVLP;
			value &= ~VLAN_ESVL;
			value &= ~VLAN_DOVLTC;
		}

		value &= ~VLAN_VID;
		writel(value | perfect_match, ioaddr + VLAN_TAG);
	} else {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value &= ~XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + VLAN_TAG);

		value &= ~(VLAN_VTHM | VLAN_ETV);
		value &= ~(VLAN_EDVLP | VLAN_ESVL);
		value &= ~VLAN_DOVLTC;
		value &= ~VLAN_VID;

		writel(value, ioaddr + VLAN_TAG);
	}
}

const struct stmmac_vlan_ops dwmac_vlan_ops = {
	.update_vlan_hash = vlan_update_hash,
	.enable_vlan = vlan_enable,
	.add_hw_vlan_rx_fltr = vlan_add_hw_rx_fltr,
	.del_hw_vlan_rx_fltr = vlan_del_hw_rx_fltr,
	.restore_hw_vlan_rx_fltr = vlan_restore_hw_rx_fltr,
	.rx_hw_vlan = vlan_rx_hw,
	.set_hw_vlan_mode = vlan_set_hw_mode,
};

const struct stmmac_vlan_ops dwxlgmac2_vlan_ops = {
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.enable_vlan = vlan_enable,
};

const struct stmmac_vlan_ops dwxgmac210_vlan_ops = {
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.enable_vlan = vlan_enable,
	.add_hw_vlan_rx_fltr = vlan_add_hw_rx_fltr,
	.del_hw_vlan_rx_fltr = vlan_del_hw_rx_fltr,
	.restore_hw_vlan_rx_fltr = vlan_restore_hw_rx_fltr,
	.rx_hw_vlan = vlan_rx_hw,
	.set_hw_vlan_mode = vlan_set_hw_mode,
};

u32 stmmac_get_num_vlan(void __iomem *ioaddr)
{
	u32 val, num_vlan;

	val = readl(ioaddr + HW_FEATURE3);
	switch (val & VLAN_HW_FEAT_NRVF) {
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
