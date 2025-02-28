// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>
#include "hbg_common.h"
#include "hbg_err.h"
#include "hbg_ethtool.h"
#include "hbg_hw.h"

enum hbg_reg_dump_type {
	HBG_DUMP_REG_TYPE_SPEC = 0,
	HBG_DUMP_REG_TYPE_MDIO,
	HBG_DUMP_REG_TYPE_GMAC,
	HBG_DUMP_REG_TYPE_PCU,
};

struct hbg_reg_info {
	u32 type;
	u32 offset;
	u32 val;
};

#define HBG_DUMP_SPEC_I(offset) {HBG_DUMP_REG_TYPE_SPEC, offset, 0}
#define HBG_DUMP_MDIO_I(offset) {HBG_DUMP_REG_TYPE_MDIO, offset, 0}
#define HBG_DUMP_GMAC_I(offset) {HBG_DUMP_REG_TYPE_GMAC, offset, 0}
#define HBG_DUMP_PCU_I(offset) {HBG_DUMP_REG_TYPE_PCU, offset, 0}

static const struct hbg_reg_info hbg_dump_reg_infos[] = {
	/* dev specs */
	HBG_DUMP_SPEC_I(HBG_REG_SPEC_VALID_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_EVENT_REQ_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAC_ID_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_PHY_ID_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAC_ADDR_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAC_ADDR_HIGH_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_UC_MAC_NUM_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MDIO_FREQ_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MAX_MTU_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_MIN_MTU_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_TX_FIFO_NUM_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_RX_FIFO_NUM_ADDR),
	HBG_DUMP_SPEC_I(HBG_REG_VLAN_LAYERS_ADDR),

	/* mdio */
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_COMMAND_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_ADDR_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_WDATA_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_RDATA_ADDR),
	HBG_DUMP_MDIO_I(HBG_REG_MDIO_STA_ADDR),

	/* gmac */
	HBG_DUMP_GMAC_I(HBG_REG_DUPLEX_TYPE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FD_FC_TYPE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FC_TX_TIMER_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FD_FC_ADDR_LOW_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_FD_FC_ADDR_HIGH_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_MAX_FRAME_SIZE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_PORT_MODE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_PORT_ENABLE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_PAUSE_ENABLE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_AN_NEG_STATE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_TRANSMIT_CTRL_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_REC_FILT_CTRL_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_LINE_LOOP_BACK_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_CF_CRC_STRIP_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_MODE_CHANGE_EN_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_LOOP_REG_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_RECV_CTRL_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_VLAN_CODE_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_0_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_0_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_1_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_1_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_2_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_2_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_3_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_3_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_4_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_4_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_LOW_5_ADDR),
	HBG_DUMP_GMAC_I(HBG_REG_STATION_ADDR_HIGH_5_ADDR),

	/* pcu */
	HBG_DUMP_PCU_I(HBG_REG_TX_FIFO_THRSLD_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_FIFO_THRSLD_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CFG_FIFO_THRSLD_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_INTRPT_MSK_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_INTRPT_STAT_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_INTRPT_CLR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_TX_BUS_ERR_ADDR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_BUS_ERR_ADDR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_MAX_FRAME_LEN_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DEBUG_ST_MCH_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_FIFO_CURR_STATUS_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_FIFO_HIST_STATUS_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_CFF_DATA_NUM_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_TX_PAUSE_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_CFF_ADDR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_BUF_SIZE_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_BUS_CTRL_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_CTRL_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_RX_PKT_MODE_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DBG_ST0_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DBG_ST1_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_DBG_ST2_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_BUS_RST_EN_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_TXINT_MSK_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_TXINT_STAT_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_TXINT_CLR_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_RXINT_MSK_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_RXINT_STAT_ADDR),
	HBG_DUMP_PCU_I(HBG_REG_CF_IND_RXINT_CLR_ADDR),
};

static const u32 hbg_dump_type_base_array[] = {
	[HBG_DUMP_REG_TYPE_SPEC] = 0,
	[HBG_DUMP_REG_TYPE_MDIO] = HBG_REG_MDIO_BASE,
	[HBG_DUMP_REG_TYPE_GMAC] = HBG_REG_SGMII_BASE,
	[HBG_DUMP_REG_TYPE_PCU] = HBG_REG_SGMII_BASE,
};

static int hbg_ethtool_get_regs_len(struct net_device *netdev)
{
	return ARRAY_SIZE(hbg_dump_reg_infos) * sizeof(struct hbg_reg_info);
}

static void hbg_ethtool_get_regs(struct net_device *netdev,
				 struct ethtool_regs *regs, void *data)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_reg_info *info;
	u32 i, offset = 0;

	regs->version = 0;
	for (i = 0; i < ARRAY_SIZE(hbg_dump_reg_infos); i++) {
		info = data + offset;

		*info = hbg_dump_reg_infos[i];
		info->val = hbg_reg_read(priv, info->offset);
		info->offset -= hbg_dump_type_base_array[info->type];

		offset += sizeof(*info);
	}
}

static void hbg_ethtool_get_pauseparam(struct net_device *net_dev,
				       struct ethtool_pauseparam *param)
{
	struct hbg_priv *priv = netdev_priv(net_dev);

	param->autoneg = priv->mac.pause_autoneg;
	hbg_hw_get_pause_enable(priv, &param->tx_pause, &param->rx_pause);
}

static int hbg_ethtool_set_pauseparam(struct net_device *net_dev,
				      struct ethtool_pauseparam *param)
{
	struct hbg_priv *priv = netdev_priv(net_dev);

	priv->mac.pause_autoneg = param->autoneg;
	phy_set_asym_pause(priv->mac.phydev, param->rx_pause, param->tx_pause);

	if (!param->autoneg)
		hbg_hw_set_pause_enable(priv, param->tx_pause, param->rx_pause);

	priv->user_def.pause_param = *param;
	return 0;
}

static int hbg_ethtool_reset(struct net_device *netdev, u32 *flags)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	if (*flags != ETH_RESET_DEDICATED)
		return -EOPNOTSUPP;

	*flags = 0;
	return hbg_reset(priv);
}

static const struct ethtool_ops hbg_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_regs_len		= hbg_ethtool_get_regs_len,
	.get_regs		= hbg_ethtool_get_regs,
	.get_pauseparam         = hbg_ethtool_get_pauseparam,
	.set_pauseparam         = hbg_ethtool_set_pauseparam,
	.reset			= hbg_ethtool_reset,
	.nway_reset		= phy_ethtool_nway_reset,
};

void hbg_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hbg_ethtool_ops;
}
