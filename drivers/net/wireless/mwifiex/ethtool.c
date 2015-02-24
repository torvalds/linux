/*
 * Marvell Wireless LAN device driver: ethtool
 *
 * Copyright (C) 2013-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "main.h"

static void mwifiex_ethtool_get_wol(struct net_device *dev,
				    struct ethtool_wolinfo *wol)
{
	struct mwifiex_private *priv = mwifiex_netdev_get_priv(dev);
	u32 conditions = le32_to_cpu(priv->adapter->hs_cfg.conditions);

	wol->supported = WAKE_UCAST|WAKE_MCAST|WAKE_BCAST|WAKE_PHY;

	if (conditions == HS_CFG_COND_DEF)
		return;

	if (conditions & HS_CFG_COND_UNICAST_DATA)
		wol->wolopts |= WAKE_UCAST;
	if (conditions & HS_CFG_COND_MULTICAST_DATA)
		wol->wolopts |= WAKE_MCAST;
	if (conditions & HS_CFG_COND_BROADCAST_DATA)
		wol->wolopts |= WAKE_BCAST;
	if (conditions & HS_CFG_COND_MAC_EVENT)
		wol->wolopts |= WAKE_PHY;
}

static int mwifiex_ethtool_set_wol(struct net_device *dev,
				   struct ethtool_wolinfo *wol)
{
	struct mwifiex_private *priv = mwifiex_netdev_get_priv(dev);
	u32 conditions = 0;

	if (wol->wolopts & ~(WAKE_UCAST|WAKE_MCAST|WAKE_BCAST|WAKE_PHY))
		return -EOPNOTSUPP;

	if (wol->wolopts & WAKE_UCAST)
		conditions |= HS_CFG_COND_UNICAST_DATA;
	if (wol->wolopts & WAKE_MCAST)
		conditions |= HS_CFG_COND_MULTICAST_DATA;
	if (wol->wolopts & WAKE_BCAST)
		conditions |= HS_CFG_COND_BROADCAST_DATA;
	if (wol->wolopts & WAKE_PHY)
		conditions |= HS_CFG_COND_MAC_EVENT;
	if (wol->wolopts == 0)
		conditions |= HS_CFG_COND_DEF;
	priv->adapter->hs_cfg.conditions = cpu_to_le32(conditions);

	return 0;
}

static int
mwifiex_get_dump_flag(struct net_device *dev, struct ethtool_dump *dump)
{
	struct mwifiex_private *priv = mwifiex_netdev_get_priv(dev);
	struct mwifiex_adapter *adapter = priv->adapter;
	struct memory_type_mapping *entry;

	if (!adapter->if_ops.fw_dump)
		return -ENOTSUPP;

	dump->flag = adapter->curr_mem_idx;
	dump->version = 1;
	if (adapter->curr_mem_idx == MWIFIEX_DRV_INFO_IDX) {
		dump->len = adapter->drv_info_size;
	} else if (adapter->curr_mem_idx != MWIFIEX_FW_DUMP_IDX) {
		entry = &adapter->mem_type_mapping_tbl[adapter->curr_mem_idx];
		dump->len = entry->mem_size;
	} else {
		dump->len = 0;
	}

	return 0;
}

static int
mwifiex_get_dump_data(struct net_device *dev, struct ethtool_dump *dump,
		      void *buffer)
{
	u8 *p = buffer;
	struct mwifiex_private *priv = mwifiex_netdev_get_priv(dev);
	struct mwifiex_adapter *adapter = priv->adapter;
	struct memory_type_mapping *entry;

	if (!adapter->if_ops.fw_dump)
		return -ENOTSUPP;

	if (adapter->curr_mem_idx == MWIFIEX_DRV_INFO_IDX) {
		if (!adapter->drv_info_dump)
			return -EFAULT;
		memcpy(p, adapter->drv_info_dump, adapter->drv_info_size);
		return 0;
	}

	if (adapter->curr_mem_idx == MWIFIEX_FW_DUMP_IDX) {
		dev_err(adapter->dev, "firmware dump in progress!!\n");
		return -EBUSY;
	}

	entry = &adapter->mem_type_mapping_tbl[adapter->curr_mem_idx];

	if (!entry->mem_ptr)
		return -EFAULT;

	memcpy(p, entry->mem_ptr, entry->mem_size);

	entry->mem_size = 0;
	vfree(entry->mem_ptr);
	entry->mem_ptr = NULL;

	return 0;
}

static int mwifiex_set_dump(struct net_device *dev, struct ethtool_dump *val)
{
	struct mwifiex_private *priv = mwifiex_netdev_get_priv(dev);
	struct mwifiex_adapter *adapter = priv->adapter;

	if (!adapter->if_ops.fw_dump)
		return -ENOTSUPP;

	if (val->flag == MWIFIEX_DRV_INFO_IDX) {
		adapter->curr_mem_idx = MWIFIEX_DRV_INFO_IDX;
		return 0;
	}

	if (adapter->curr_mem_idx == MWIFIEX_FW_DUMP_IDX) {
		dev_err(adapter->dev, "firmware dump in progress!!\n");
		return -EBUSY;
	}

	if (val->flag == MWIFIEX_FW_DUMP_IDX) {
		adapter->curr_mem_idx = val->flag;
		adapter->if_ops.fw_dump(adapter);
		return 0;
	}

	if (val->flag < 0 || val->flag >= adapter->num_mem_types)
		return -EINVAL;

	adapter->curr_mem_idx = val->flag;

	return 0;
}

const struct ethtool_ops mwifiex_ethtool_ops = {
	.get_wol = mwifiex_ethtool_get_wol,
	.set_wol = mwifiex_ethtool_set_wol,
	.get_dump_flag = mwifiex_get_dump_flag,
	.get_dump_data = mwifiex_get_dump_data,
	.set_dump = mwifiex_set_dump,
};
