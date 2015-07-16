/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __XGENE_ENET_XGMAC_H__
#define __XGENE_ENET_XGMAC_H__

#define X2_BLOCK_ETH_MAC_CSR_OFFSET	0x3000
#define BLOCK_AXG_MAC_OFFSET		0x0800
#define BLOCK_AXG_MAC_CSR_OFFSET	0x2000

#define XGENET_CONFIG_REG_ADDR		0x20
#define XGENET_SRST_ADDR		0x00
#define XGENET_CLKEN_ADDR		0x08

#define CSR_CLK		BIT(0)
#define XGENET_CLK	BIT(1)
#define PCS_CLK		BIT(3)
#define AN_REF_CLK	BIT(4)
#define AN_CLK		BIT(5)
#define AD_CLK		BIT(6)

#define CSR_RST		BIT(0)
#define XGENET_RST	BIT(1)
#define PCS_RST		BIT(3)
#define AN_REF_RST	BIT(4)
#define AN_RST		BIT(5)
#define AD_RST		BIT(6)

#define AXGMAC_CONFIG_0			0x0000
#define AXGMAC_CONFIG_1			0x0004
#define HSTMACRST			BIT(31)
#define HSTTCTLEN			BIT(31)
#define HSTTFEN				BIT(30)
#define HSTRCTLEN			BIT(29)
#define HSTRFEN				BIT(28)
#define HSTPPEN				BIT(7)
#define HSTDRPLT64			BIT(5)
#define HSTLENCHK			BIT(3)
#define HSTMACADR_LSW_ADDR		0x0010
#define HSTMACADR_MSW_ADDR		0x0014
#define HSTMAXFRAME_LENGTH_ADDR		0x0020

#define XG_MCX_RX_DV_GATE_REG_0_ADDR	0x0004
#define XG_RSIF_CONFIG_REG_ADDR		0x00a0
#define XCLE_BYPASS_REG0_ADDR           0x0160
#define XCLE_BYPASS_REG1_ADDR           0x0164
#define XG_CFG_BYPASS_ADDR		0x0204
#define XG_LINK_STATUS_ADDR		0x0228
#define XG_ENET_SPARE_CFG_REG_ADDR	0x040c
#define XG_ENET_SPARE_CFG_REG_1_ADDR	0x0410
#define XGENET_RX_DV_GATE_REG_0_ADDR	0x0804

extern struct xgene_mac_ops xgene_xgmac_ops;
extern struct xgene_port_ops xgene_xgport_ops;

#endif /* __XGENE_ENET_XGMAC_H__ */
