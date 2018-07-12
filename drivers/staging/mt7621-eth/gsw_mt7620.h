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

#ifndef _RALINK_GSW_MT7620_H__
#define _RALINK_GSW_MT7620_H__

#define GSW_REG_PHY_TIMEOUT	(5 * HZ)

#define MT7620_GSW_REG_PIAC	0x0004

#define GSW_NUM_VLANS		16
#define GSW_NUM_VIDS		4096
#define GSW_NUM_PORTS		7
#define GSW_PORT6		6

#define GSW_MDIO_ACCESS		BIT(31)
#define GSW_MDIO_READ		BIT(19)
#define GSW_MDIO_WRITE		BIT(18)
#define GSW_MDIO_START		BIT(16)
#define GSW_MDIO_ADDR_SHIFT	20
#define GSW_MDIO_REG_SHIFT	25

#define GSW_REG_PORT_PMCR(x)	(0x3000 + (x * 0x100))
#define GSW_REG_PORT_STATUS(x)	(0x3008 + (x * 0x100))
#define GSW_REG_SMACCR0		0x3fE4
#define GSW_REG_SMACCR1		0x3fE8
#define GSW_REG_CKGCR		0x3ff0

#define GSW_REG_IMR		0x7008
#define GSW_REG_ISR		0x700c
#define GSW_REG_GPC1		0x7014

#define SYSC_REG_CHIP_REV_ID	0x0c
#define SYSC_REG_CFG		0x10
#define SYSC_REG_CFG1		0x14
#define RST_CTRL_MCM		BIT(2)
#define SYSC_PAD_RGMII2_MDIO	0x58
#define SYSC_GPIO_MODE		0x60

#define PORT_IRQ_ST_CHG		0x7f

#define MT7621_ESW_PHY_POLLING	0x0000
#define MT7620_ESW_PHY_POLLING	0x7000

#define	PMCR_IPG		BIT(18)
#define	PMCR_MAC_MODE		BIT(16)
#define	PMCR_FORCE		BIT(15)
#define	PMCR_TX_EN		BIT(14)
#define	PMCR_RX_EN		BIT(13)
#define	PMCR_BACKOFF		BIT(9)
#define	PMCR_BACKPRES		BIT(8)
#define	PMCR_RX_FC		BIT(5)
#define	PMCR_TX_FC		BIT(4)
#define	PMCR_SPEED(_x)		(_x << 2)
#define	PMCR_DUPLEX		BIT(1)
#define	PMCR_LINK		BIT(0)

#define PHY_AN_EN		BIT(31)
#define PHY_PRE_EN		BIT(30)
#define PMY_MDC_CONF(_x)	((_x & 0x3f) << 24)

/* ethernet subsystem config register */
#define ETHSYS_SYSCFG0		0x14
/* ethernet subsystem clock register */
#define ETHSYS_CLKCFG0		0x2c
#define ETHSYS_TRGMII_CLK_SEL362_5	BIT(11)

/* p5 RGMII wrapper TX clock control register */
#define MT7530_P5RGMIITXCR	0x7b04
/* p5 RGMII wrapper RX clock control register */
#define MT7530_P5RGMIIRXCR	0x7b00
/* TRGMII TDX ODT registers */
#define MT7530_TRGMII_TD0_ODT	0x7a54
#define MT7530_TRGMII_TD1_ODT	0x7a5c
#define MT7530_TRGMII_TD2_ODT	0x7a64
#define MT7530_TRGMII_TD3_ODT	0x7a6c
#define MT7530_TRGMII_TD4_ODT	0x7a74
#define MT7530_TRGMII_TD5_ODT	0x7a7c
/* TRGMII TCK ctrl register */
#define MT7530_TRGMII_TCK_CTRL	0x7a78
/* TRGMII Tx ctrl register */
#define MT7530_TRGMII_TXCTRL	0x7a40
/* port 6 extended control register */
#define MT7530_P6ECR            0x7830
/* IO driver control register */
#define MT7530_IO_DRV_CR	0x7810
/* top signal control register */
#define MT7530_TOP_SIG_CTRL	0x7808
/* modified hwtrap register */
#define MT7530_MHWTRAP		0x7804
/* hwtrap status register */
#define MT7530_HWTRAP		0x7800
/* status interrupt register */
#define MT7530_SYS_INT_STS	0x700c
/* system nterrupt register */
#define MT7530_SYS_INT_EN	0x7008
/* system control register */
#define MT7530_SYS_CTRL		0x7000
/* port MAC status register */
#define MT7530_PMSR_P(x)	(0x3008 + (x * 0x100))
/* port MAC control register */
#define MT7530_PMCR_P(x)	(0x3000 + (x * 0x100))

#define MT7621_XTAL_SHIFT	6
#define MT7621_XTAL_MASK	0x7
#define MT7621_XTAL_25		6
#define MT7621_XTAL_40		3
#define MT7621_MDIO_DRV_MASK	(3 << 4)
#define MT7621_GE1_MODE_MASK	(3 << 12)

#define TRGMII_TXCTRL_TXC_INV	BIT(30)
#define P6ECR_INTF_MODE_RGMII	BIT(1)
#define P5RGMIIRXCR_C_ALIGN	BIT(8)
#define P5RGMIIRXCR_DELAY_2	BIT(1)
#define P5RGMIITXCR_DELAY_2	(BIT(8) | BIT(2))

/* TOP_SIG_CTRL bits */
#define TOP_SIG_CTRL_NORMAL	(BIT(17) | BIT(16))

/* MHWTRAP bits */
#define MHWTRAP_MANUAL		BIT(16)
#define MHWTRAP_P5_MAC_SEL	BIT(13)
#define MHWTRAP_P6_DIS		BIT(8)
#define MHWTRAP_P5_RGMII_MODE	BIT(7)
#define MHWTRAP_P5_DIS		BIT(6)
#define MHWTRAP_PHY_ACCESS	BIT(5)

/* HWTRAP bits */
#define HWTRAP_XTAL_SHIFT	9
#define HWTRAP_XTAL_MASK	0x3

/* SYS_CTRL bits */
#define SYS_CTRL_SW_RST		BIT(1)
#define SYS_CTRL_REG_RST	BIT(0)

/* PMCR bits */
#define PMCR_IFG_XMIT_96	BIT(18)
#define PMCR_MAC_MODE		BIT(16)
#define PMCR_FORCE_MODE		BIT(15)
#define PMCR_TX_EN		BIT(14)
#define PMCR_RX_EN		BIT(13)
#define PMCR_BACK_PRES_EN	BIT(9)
#define PMCR_BACKOFF_EN		BIT(8)
#define PMCR_TX_FC_EN		BIT(5)
#define PMCR_RX_FC_EN		BIT(4)
#define PMCR_FORCE_SPEED_1000	BIT(3)
#define PMCR_FORCE_FDX		BIT(1)
#define PMCR_FORCE_LNK		BIT(0)
#define PMCR_FIXED_LINK		(PMCR_IFG_XMIT_96 | PMCR_MAC_MODE | \
				 PMCR_FORCE_MODE | PMCR_TX_EN | PMCR_RX_EN | \
				 PMCR_BACK_PRES_EN | PMCR_BACKOFF_EN | \
				 PMCR_FORCE_SPEED_1000 | PMCR_FORCE_FDX | \
				 PMCR_FORCE_LNK)

#define PMCR_FIXED_LINK_FC	(PMCR_FIXED_LINK | \
				 PMCR_TX_FC_EN | PMCR_RX_FC_EN)

/* TRGMII control registers */
#define GSW_INTF_MODE		0x390
#define GSW_TRGMII_TD0_ODT	0x354
#define GSW_TRGMII_TD1_ODT	0x35c
#define GSW_TRGMII_TD2_ODT	0x364
#define GSW_TRGMII_TD3_ODT	0x36c
#define GSW_TRGMII_TXCTL_ODT	0x374
#define GSW_TRGMII_TCK_ODT	0x37c
#define GSW_TRGMII_RCK_CTRL	0x300

#define INTF_MODE_TRGMII	BIT(1)
#define TRGMII_RCK_CTRL_RX_RST	BIT(31)

/* Mac control registers */
#define MTK_MAC_P2_MCR		0x200
#define MTK_MAC_P1_MCR		0x100

#define MAC_MCR_MAX_RX_2K	BIT(29)
#define MAC_MCR_IPG_CFG		(BIT(18) | BIT(16))
#define MAC_MCR_FORCE_MODE	BIT(15)
#define MAC_MCR_TX_EN		BIT(14)
#define MAC_MCR_RX_EN		BIT(13)
#define MAC_MCR_BACKOFF_EN	BIT(9)
#define MAC_MCR_BACKPR_EN	BIT(8)
#define MAC_MCR_FORCE_RX_FC	BIT(5)
#define MAC_MCR_FORCE_TX_FC	BIT(4)
#define MAC_MCR_SPEED_1000	BIT(3)
#define MAC_MCR_FORCE_DPX	BIT(1)
#define MAC_MCR_FORCE_LINK	BIT(0)
#define MAC_MCR_FIXED_LINK	(MAC_MCR_MAX_RX_2K | MAC_MCR_IPG_CFG | \
				 MAC_MCR_FORCE_MODE | MAC_MCR_TX_EN | \
				 MAC_MCR_RX_EN | MAC_MCR_BACKOFF_EN | \
				 MAC_MCR_BACKPR_EN | MAC_MCR_FORCE_RX_FC | \
				 MAC_MCR_FORCE_TX_FC | MAC_MCR_SPEED_1000 | \
				 MAC_MCR_FORCE_DPX | MAC_MCR_FORCE_LINK)
#define MAC_MCR_FIXED_LINK_FC	(MAC_MCR_MAX_RX_2K | MAC_MCR_IPG_CFG | \
				 MAC_MCR_FIXED_LINK)

/* possible XTAL speed */
#define	MT7623_XTAL_40		0
#define MT7623_XTAL_20		1
#define MT7623_XTAL_25		3

/* GPIO port control registers */
#define	GPIO_OD33_CTRL8		0x4c0
#define	GPIO_BIAS_CTRL		0xed0
#define GPIO_DRV_SEL10		0xf00

/* on MT7620 the functio of port 4 can be software configured */
enum {
	PORT4_EPHY = 0,
	PORT4_EXT,
};

/* struct mt7620_gsw -	the structure that holds the SoC specific data
 * @dev:		The Device struct
 * @base:		The base address
 * @piac_offset:	The PIAC base may change depending on SoC
 * @irq:		The IRQ we are using
 * @port4:		The port4 mode on MT7620
 * @autopoll:		Is MDIO autopolling enabled
 * @ethsys:		The ethsys register map
 * @pctl:		The pin control register map
 * @clk_gsw:		The switch clock
 * @clk_gp1:		The gmac1 clock
 * @clk_gp2:		The gmac2 clock
 * @clk_trgpll:		The trgmii pll clock
 */
struct mt7620_gsw {
	struct device		*dev;
	void __iomem		*base;
	u32			piac_offset;
	int			irq;
	int			port4;
	unsigned long int	autopoll;

	struct regmap		*ethsys;
	struct regmap		*pctl;

	struct clk		*clk_gsw;
	struct clk		*clk_gp1;
	struct clk		*clk_gp2;
	struct clk		*clk_trgpll;
};

/* switch register I/O wrappers */
void mtk_switch_w32(struct mt7620_gsw *gsw, u32 val, unsigned reg);
u32 mtk_switch_r32(struct mt7620_gsw *gsw, unsigned reg);

/* the callback used by the driver core to bringup the switch */
int mtk_gsw_init(struct mtk_eth *eth);

/* MDIO access wrappers */
int mt7620_mdio_write(struct mii_bus *bus, int phy_addr, int phy_reg, u16 val);
int mt7620_mdio_read(struct mii_bus *bus, int phy_addr, int phy_reg);
void mt7620_mdio_link_adjust(struct mtk_eth *eth, int port);
int mt7620_has_carrier(struct mtk_eth *eth);
void mt7620_print_link_state(struct mtk_eth *eth, int port, int link,
			     int speed, int duplex);
void mt7530_mdio_w32(struct mt7620_gsw *gsw, u32 reg, u32 val);
u32 mt7530_mdio_r32(struct mt7620_gsw *gsw, u32 reg);
void mt7530_mdio_m32(struct mt7620_gsw *gsw, u32 mask, u32 set, u32 reg);

u32 _mt7620_mii_write(struct mt7620_gsw *gsw, u32 phy_addr,
		      u32 phy_register, u32 write_data);
u32 _mt7620_mii_read(struct mt7620_gsw *gsw, int phy_addr, int phy_reg);
void mt7620_handle_carrier(struct mtk_eth *eth);

#endif
