/* SPDX-License-Identifier: GPL-2.0 */

/* PRU-ICSS MII_RT register definitions
 *
 * Copyright (C) 2015-2022 Texas Instruments Incorporated - https://www.ti.com
 */

#ifndef __NET_PRUSS_MII_RT_H__
#define __NET_PRUSS_MII_RT_H__

#include <linux/if_ether.h>
#include <linux/phy.h>

/* PRUSS_MII_RT Registers */
#define PRUSS_MII_RT_RXCFG0		0x0
#define PRUSS_MII_RT_RXCFG1		0x4
#define PRUSS_MII_RT_TXCFG0		0x10
#define PRUSS_MII_RT_TXCFG1		0x14
#define PRUSS_MII_RT_TX_CRC0		0x20
#define PRUSS_MII_RT_TX_CRC1		0x24
#define PRUSS_MII_RT_TX_IPG0		0x30
#define PRUSS_MII_RT_TX_IPG1		0x34
#define PRUSS_MII_RT_PRS0		0x38
#define PRUSS_MII_RT_PRS1		0x3c
#define PRUSS_MII_RT_RX_FRMS0		0x40
#define PRUSS_MII_RT_RX_FRMS1		0x44
#define PRUSS_MII_RT_RX_PCNT0		0x48
#define PRUSS_MII_RT_RX_PCNT1		0x4c
#define PRUSS_MII_RT_RX_ERR0		0x50
#define PRUSS_MII_RT_RX_ERR1		0x54

/* PRUSS_MII_RT_RXCFG0/1 bits */
#define PRUSS_MII_RT_RXCFG_RX_ENABLE		BIT(0)
#define PRUSS_MII_RT_RXCFG_RX_DATA_RDY_MODE_DIS	BIT(1)
#define PRUSS_MII_RT_RXCFG_RX_CUT_PREAMBLE	BIT(2)
#define PRUSS_MII_RT_RXCFG_RX_MUX_SEL		BIT(3)
#define PRUSS_MII_RT_RXCFG_RX_L2_EN		BIT(4)
#define PRUSS_MII_RT_RXCFG_RX_BYTE_SWAP		BIT(5)
#define PRUSS_MII_RT_RXCFG_RX_AUTO_FWD_PRE	BIT(6)
#define PRUSS_MII_RT_RXCFG_RX_L2_EOF_SCLR_DIS	BIT(9)

/* PRUSS_MII_RT_TXCFG0/1 bits */
#define PRUSS_MII_RT_TXCFG_TX_ENABLE		BIT(0)
#define PRUSS_MII_RT_TXCFG_TX_AUTO_PREAMBLE	BIT(1)
#define PRUSS_MII_RT_TXCFG_TX_EN_MODE		BIT(2)
#define PRUSS_MII_RT_TXCFG_TX_BYTE_SWAP		BIT(3)
#define PRUSS_MII_RT_TXCFG_TX_MUX_SEL		BIT(8)
#define PRUSS_MII_RT_TXCFG_PRE_TX_AUTO_SEQUENCE	BIT(9)
#define PRUSS_MII_RT_TXCFG_PRE_TX_AUTO_ESC_ERR	BIT(10)
#define PRUSS_MII_RT_TXCFG_TX_32_MODE_EN	BIT(11)
#define PRUSS_MII_RT_TXCFG_TX_IPG_WIRE_CLK_EN	BIT(12)	/* SR2.0 onwards */

#define PRUSS_MII_RT_TXCFG_TX_START_DELAY_SHIFT	16
#define PRUSS_MII_RT_TXCFG_TX_START_DELAY_MASK	GENMASK(25, 16)

#define PRUSS_MII_RT_TXCFG_TX_CLK_DELAY_SHIFT	28
#define PRUSS_MII_RT_TXCFG_TX_CLK_DELAY_MASK	GENMASK(30, 28)

/* PRUSS_MII_RT_TX_IPG0/1 bits */
#define PRUSS_MII_RT_TX_IPG_IPG_SHIFT	0
#define PRUSS_MII_RT_TX_IPG_IPG_MASK	GENMASK(9, 0)

/* PRUSS_MII_RT_PRS0/1 bits */
#define PRUSS_MII_RT_PRS_COL	BIT(0)
#define PRUSS_MII_RT_PRS_CRS	BIT(1)

/* PRUSS_MII_RT_RX_FRMS0/1 bits */
#define PRUSS_MII_RT_RX_FRMS_MIN_FRM_SHIFT	0
#define PRUSS_MII_RT_RX_FRMS_MIN_FRM_MASK	GENMASK(15, 0)

#define PRUSS_MII_RT_RX_FRMS_MAX_FRM_SHIFT	16
#define PRUSS_MII_RT_RX_FRMS_MAX_FRM_MASK	GENMASK(31, 16)

/* Min/Max in MII_RT_RX_FRMS */
/* For EMAC and Switch */
#define PRUSS_MII_RT_RX_FRMS_MAX	(VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)
#define PRUSS_MII_RT_RX_FRMS_MIN_FRM	(64)

/* for HSR and PRP */
#define PRUSS_MII_RT_RX_FRMS_MAX_FRM_LRE	(PRUSS_MII_RT_RX_FRMS_MAX + \
						 ICSS_LRE_TAG_RCT_SIZE)
/* PRUSS_MII_RT_RX_PCNT0/1 bits */
#define PRUSS_MII_RT_RX_PCNT_MIN_PCNT_SHIFT	0
#define PRUSS_MII_RT_RX_PCNT_MIN_PCNT_MASK	GENMASK(3, 0)

#define PRUSS_MII_RT_RX_PCNT_MAX_PCNT_SHIFT	4
#define PRUSS_MII_RT_RX_PCNT_MAX_PCNT_MASK	GENMASK(7, 4)

/* PRUSS_MII_RT_RX_ERR0/1 bits */
#define PRUSS_MII_RT_RX_ERR_MIN_PCNT_ERR	BIT(0)
#define PRUSS_MII_RT_RX_ERR_MAX_PCNT_ERR	BIT(1)
#define PRUSS_MII_RT_RX_ERR_MIN_FRM_ERR		BIT(2)
#define PRUSS_MII_RT_RX_ERR_MAX_FRM_ERR		BIT(3)

#define ICSSG_CFG_OFFSET	0
#define RGMII_CFG_OFFSET	4

/* Constant to choose between MII0 and MII1 */
#define ICSS_MII0	0
#define ICSS_MII1	1

/* ICSSG_CFG Register bits */
#define ICSSG_CFG_SGMII_MODE	BIT(16)
#define ICSSG_CFG_TX_PRU_EN	BIT(11)
#define ICSSG_CFG_RX_SFD_TX_SOF_EN	BIT(10)
#define ICSSG_CFG_RTU_PRU_PSI_SHARE_EN	BIT(9)
#define ICSSG_CFG_IEP1_TX_EN	BIT(8)
#define ICSSG_CFG_MII1_MODE	GENMASK(6, 5)
#define ICSSG_CFG_MII1_MODE_SHIFT	5
#define ICSSG_CFG_MII0_MODE	GENMASK(4, 3)
#define ICSSG_CFG_MII0_MODE_SHIFT	3
#define ICSSG_CFG_RX_L2_G_EN	BIT(2)
#define ICSSG_CFG_TX_L2_EN	BIT(1)
#define ICSSG_CFG_TX_L1_EN	BIT(0)

enum mii_mode {
	MII_MODE_MII = 0,
	MII_MODE_RGMII
};

/* RGMII CFG Register bits */
#define RGMII_CFG_INBAND_EN_MII0	BIT(16)
#define RGMII_CFG_GIG_EN_MII0	BIT(17)
#define RGMII_CFG_INBAND_EN_MII1	BIT(20)
#define RGMII_CFG_GIG_EN_MII1	BIT(21)
#define RGMII_CFG_FULL_DUPLEX_MII0	BIT(18)
#define RGMII_CFG_FULL_DUPLEX_MII1	BIT(22)
#define RGMII_CFG_SPEED_MII0	GENMASK(2, 1)
#define RGMII_CFG_SPEED_MII1	GENMASK(6, 5)
#define RGMII_CFG_SPEED_MII0_SHIFT	1
#define RGMII_CFG_SPEED_MII1_SHIFT	5
#define RGMII_CFG_FULLDUPLEX_MII0	BIT(3)
#define RGMII_CFG_FULLDUPLEX_MII1	BIT(7)
#define RGMII_CFG_FULLDUPLEX_MII0_SHIFT	3
#define RGMII_CFG_FULLDUPLEX_MII1_SHIFT	7
#define RGMII_CFG_SPEED_10M	0
#define RGMII_CFG_SPEED_100M	1
#define RGMII_CFG_SPEED_1G	2

struct regmap;
struct prueth_emac;

void icssg_mii_update_ipg(struct regmap *mii_rt, int mii, u32 ipg);
void icssg_mii_update_mtu(struct regmap *mii_rt, int mii, int mtu);
void icssg_update_rgmii_cfg(struct regmap *miig_rt, struct prueth_emac *emac);
u32 icssg_rgmii_cfg_get_bitfield(struct regmap *miig_rt, u32 mask, u32 shift);
u32 icssg_rgmii_get_speed(struct regmap *miig_rt, int mii);
u32 icssg_rgmii_get_fullduplex(struct regmap *miig_rt, int mii);
void icssg_miig_set_interface_mode(struct regmap *miig_rt, int mii, phy_interface_t phy_if);

#endif /* __NET_PRUSS_MII_RT_H__ */
