/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/if_vlan.h>
#include <linux/of_net.h>

#include <asm/mach-ralink/ralink_regs.h>

#include "mtk_eth_soc.h"
#include "gsw_mt7620.h"
#include "mdio.h"

#define MT7620_CDMA_CSG_CFG	0x400
#define MT7621_CDMP_IG_CTRL	(MT7620_CDMA_CSG_CFG + 0x00)
#define MT7621_CDMP_EG_CTRL	(MT7620_CDMA_CSG_CFG + 0x04)
#define MT7621_RESET_FE		BIT(6)
#define MT7621_L4_VALID		BIT(24)

#define MT7621_TX_DMA_UDF	BIT(19)

#define CDMA_ICS_EN		BIT(2)
#define CDMA_UCS_EN		BIT(1)
#define CDMA_TCS_EN		BIT(0)

#define GDMA_ICS_EN		BIT(22)
#define GDMA_TCS_EN		BIT(21)
#define GDMA_UCS_EN		BIT(20)

/* frame engine counters */
#define MT7621_REG_MIB_OFFSET	0x2000
#define MT7621_PPE_AC_BCNT0	(MT7621_REG_MIB_OFFSET + 0x00)
#define MT7621_GDM1_TX_GBCNT	(MT7621_REG_MIB_OFFSET + 0x400)
#define MT7621_GDM2_TX_GBCNT	(MT7621_GDM1_TX_GBCNT + 0x40)

#define GSW_REG_GDMA1_MAC_ADRL	0x508
#define GSW_REG_GDMA1_MAC_ADRH	0x50C
#define GSW_REG_GDMA2_MAC_ADRL	0x1508
#define GSW_REG_GDMA2_MAC_ADRH	0x150C

#define MT7621_MTK_RST_GL	0x04
#define MT7620_MTK_INT_STATUS2	0x08

/* MTK_INT_STATUS reg on mt7620 define CNT_GDM1_AF at BIT(29)
 * but after test it should be BIT(13).
 */
#define MT7621_MTK_GDM1_AF	BIT(28)
#define MT7621_MTK_GDM2_AF	BIT(29)

static const u16 mt7621_reg_table[MTK_REG_COUNT] = {
	[MTK_REG_PDMA_GLO_CFG] = RT5350_PDMA_GLO_CFG,
	[MTK_REG_PDMA_RST_CFG] = RT5350_PDMA_RST_CFG,
	[MTK_REG_DLY_INT_CFG] = RT5350_DLY_INT_CFG,
	[MTK_REG_TX_BASE_PTR0] = RT5350_TX_BASE_PTR0,
	[MTK_REG_TX_MAX_CNT0] = RT5350_TX_MAX_CNT0,
	[MTK_REG_TX_CTX_IDX0] = RT5350_TX_CTX_IDX0,
	[MTK_REG_TX_DTX_IDX0] = RT5350_TX_DTX_IDX0,
	[MTK_REG_RX_BASE_PTR0] = RT5350_RX_BASE_PTR0,
	[MTK_REG_RX_MAX_CNT0] = RT5350_RX_MAX_CNT0,
	[MTK_REG_RX_CALC_IDX0] = RT5350_RX_CALC_IDX0,
	[MTK_REG_RX_DRX_IDX0] = RT5350_RX_DRX_IDX0,
	[MTK_REG_MTK_INT_ENABLE] = RT5350_MTK_INT_ENABLE,
	[MTK_REG_MTK_INT_STATUS] = RT5350_MTK_INT_STATUS,
	[MTK_REG_MTK_DMA_VID_BASE] = 0,
	[MTK_REG_MTK_COUNTER_BASE] = MT7621_GDM1_TX_GBCNT,
	[MTK_REG_MTK_RST_GL] = MT7621_MTK_RST_GL,
	[MTK_REG_MTK_INT_STATUS2] = MT7620_MTK_INT_STATUS2,
};

static void mt7621_mtk_reset(struct mtk_eth *eth)
{
	mtk_reset(eth, MT7621_RESET_FE);
}

static int mt7621_fwd_config(struct mtk_eth *eth)
{
	/* Setup GMAC1 only, there is no support for GMAC2 yet */
	mtk_w32(eth, mtk_r32(eth, MT7620_GDMA1_FWD_CFG) & ~0xffff,
		MT7620_GDMA1_FWD_CFG);

	/* Enable RX checksum */
	mtk_w32(eth, mtk_r32(eth, MT7620_GDMA1_FWD_CFG) | (GDMA_ICS_EN |
		       GDMA_TCS_EN | GDMA_UCS_EN),
		       MT7620_GDMA1_FWD_CFG);

	/* Enable RX VLan Offloading */
	mtk_w32(eth, 0, MT7621_CDMP_EG_CTRL);

	return 0;
}

static void mt7621_set_mac(struct mtk_mac *mac, unsigned char *hwaddr)
{
	unsigned long flags;

	spin_lock_irqsave(&mac->hw->page_lock, flags);
	if (mac->id == 0) {
		mtk_w32(mac->hw, (hwaddr[0] << 8) | hwaddr[1],
			GSW_REG_GDMA1_MAC_ADRH);
		mtk_w32(mac->hw, (hwaddr[2] << 24) | (hwaddr[3] << 16) |
			(hwaddr[4] << 8) | hwaddr[5],
			GSW_REG_GDMA1_MAC_ADRL);
	}
	if (mac->id == 1) {
		mtk_w32(mac->hw, (hwaddr[0] << 8) | hwaddr[1],
			GSW_REG_GDMA2_MAC_ADRH);
		mtk_w32(mac->hw, (hwaddr[2] << 24) | (hwaddr[3] << 16) |
			(hwaddr[4] << 8) | hwaddr[5],
			GSW_REG_GDMA2_MAC_ADRL);
	}
	spin_unlock_irqrestore(&mac->hw->page_lock, flags);
}

static struct mtk_soc_data mt7621_data = {
	.hw_features = NETIF_F_IP_CSUM | NETIF_F_RXCSUM |
		       NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
		       NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 |
		       NETIF_F_IPV6_CSUM,
	.dma_type = MTK_PDMA,
	.dma_ring_size = 256,
	.napi_weight = 64,
	.new_stats = 1,
	.padding_64b = 1,
	.rx_2b_offset = 1,
	.rx_sg_dma = 1,
	.has_switch = 1,
	.mac_count = 2,
	.reset_fe = mt7621_mtk_reset,
	.set_mac = mt7621_set_mac,
	.fwd_config = mt7621_fwd_config,
	.switch_init = mtk_gsw_init,
	.reg_table = mt7621_reg_table,
	.pdma_glo_cfg = MTK_PDMA_SIZE_16DWORDS,
	.rx_int = RT5350_RX_DONE_INT,
	.tx_int = RT5350_TX_DONE_INT,
	.status_int = MT7621_MTK_GDM1_AF | MT7621_MTK_GDM2_AF,
	.checksum_bit = MT7621_L4_VALID,
	.has_carrier = mt7620_has_carrier,
	.mdio_read = mt7620_mdio_read,
	.mdio_write = mt7620_mdio_write,
	.mdio_adjust_link = mt7620_mdio_link_adjust,
};

const struct of_device_id of_mtk_match[] = {
	{ .compatible = "mediatek,mt7621-eth", .data = &mt7621_data },
	{},
};

MODULE_DEVICE_TABLE(of, of_mtk_match);
