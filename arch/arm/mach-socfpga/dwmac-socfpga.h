/*
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DWMAC_SOCFPGA_H
#define DWMAC_SOCFPGA_H

#define EMAC_SPLITTER_CTRL_REG				0x0
#define EMAC_SPLITTER_CTRL_SPEED_MASK			0x3
#define EMAC_SPLITTER_CTRL_SPEED_10			0x2
#define EMAC_SPLITTER_CTRL_SPEED_100			0x3
#define EMAC_SPLITTER_CTRL_SPEED_1000			0x0
#define TSE_PCS_CONTROL_REG				0x00
#define TSE_PCS_STATUS_REG				0x02
#define TSE_PCS_PHY_ID_0_REG				0x04
#define TSE_PCS_PHY_ID_1_REG				0x06
#define TSE_PCS_DEV_ABILITY_REG			0x08
#define TSE_PCS_PARTNER_ABILITY_REG			0x0A
#define TSE_PCS_AN_EXPANSION_REG			0x0C
#define TSE_PCS_DEVICE_NEXT_PAGE_REG			0x0E
#define TSE_PCS_PARTER_NEXT_PAGE_REG			0x10
#define TSE_PCS_MASTER_SLAVE_CNTL_REG			0x12
#define TSE_PCS_MASTER_SLAVE_STAT_REG			0x14
#define TSE_PCS_EXTENDED_STAT_REG			0x1E
#define TSE_PCS_SCRATCH_REG				0x20
#define TSE_PCS_REVISION_REG				0x22
#define TSE_PCS_LINK_TIMER_0_REG			0x24
#define TSE_PCS_LINK_TIMER_1_REG			0x26
#define TSE_PCS_IF_MODE_REG				0x28
#define TSE_PCS_SW_RST_MASK				0x2000
#define TSE_PCS_CONTROL_AN_EN_MASK			0x1000
#define TSE_PCS_STATUS_LINK_MASK			0x0004
#define TSE_PCS_STATUS_AN_COMPLETED_MASK		0x0020
#define TSE_PCS_USE_SGMII_AN_MASK			0x0002
#define TSE_PCS_SGMII_DUPLEX_MASK			0x0010
#define TSE_PCS_SGMII_SPEED_MASK			0x000C
#define TSE_PCS_PARTNER_SPEED_MASK			0x0c00
#define TSE_PCS_PARTNER_DUPLEX_MASK			0x1000
#define TSE_PCS_SGMII_SPEED_1000			0x8
#define TSE_PCS_SGMII_SPEED_100			0x4
#define TSE_PCS_SGMII_SPEED_10				0x0
#define TSE_PCS_PARTNER_SPEED_1000			0x0800
#define TSE_PCS_PARTNER_SPEED_100			0x0400
#define TSE_PCS_PARTNER_SPEED_10			0x0000
#define TSE_PCS_PARTNER_DUPLEX_FULL			0x1000
#define TSE_PCS_PARTNER_DUPLEX_HALF			0x0000
#define TSE_PCS_SGMII_LINK_TIMER_0			0x0D40
#define TSE_PCS_SGMII_LINK_TIMER_1			0x0003
#define TSE_PCS_PHY_ID					0x12345678
#define SGMII_ADAPTER_CTRL_REG				0x00
#define SGMII_ADAPTER_STATUS_REG			0x04
#define SGMII_ADAPTER_ENABLE				0x0000
#define SGMII_ADAPTER_DISABLE				0x0001
#define TSE_PCS_CONTROL_RESTART_AN_MASK		0x0200
#define LINK_TIMER					20
#define AUTONEGO_TIMER					100
#define TSE_PCS_SW_RESET_TIMEOUT			100
#define TSE_PCS_ADDR					0xFF200040
#define TSE_PCS_SIZE					0x40
#define SGMII_ADAPTER_ADDR				0xFF200010
#define SGMII_ADAPTER_SIZE				0x08

struct dwmac_plat_priv {
	void __iomem *emac_splitter_base;
	void __iomem *tse_pcs_base;
	void __iomem *sgmii_adapter_base;
	struct platform_device *pdev;
	struct timer_list an_timer;
	struct timer_list link_timer;
};

void adapter_config(void *priv, unsigned int speed);
int adapter_init(struct platform_device *pdev, int phymode, u32 *val);

#endif /* #ifndef DWMAC_SOCFPGA_H */
