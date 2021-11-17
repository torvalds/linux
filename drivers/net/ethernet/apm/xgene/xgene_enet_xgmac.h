/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
 */

#ifndef __XGENE_ENET_XGMAC_H__
#define __XGENE_ENET_XGMAC_H__

#define X2_BLOCK_ETH_MAC_CSR_OFFSET	0x3000
#define BLOCK_AXG_MAC_OFFSET		0x0800
#define BLOCK_AXG_STATS_OFFSET		0x0800
#define BLOCK_AXG_MAC_CSR_OFFSET	0x2000
#define BLOCK_PCS_OFFSET		0x3800

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
#define XG_MCX_ECM_CFG_0_ADDR		0x0074
#define XG_MCX_MULTI_DPF0_ADDR		0x007c
#define XG_MCX_MULTI_DPF1_ADDR		0x0080
#define XG_DEF_PAUSE_THRES		0x390
#define XG_DEF_PAUSE_OFF_THRES		0x2c0
#define XG_RSIF_CONFIG_REG_ADDR		0x00a0
#define XG_RSIF_CLE_BUFF_THRESH                0x3
#define RSIF_CLE_BUFF_THRESH_SET(dst, val)     xgene_set_bits(dst, val, 0, 3)
#define XG_RSIF_CONFIG1_REG_ADDR       0x00b8
#define XG_RSIF_PLC_CLE_BUFF_THRESH    0x1
#define RSIF_PLC_CLE_BUFF_THRESH_SET(dst, val) xgene_set_bits(dst, val, 0, 2)
#define XG_MCX_ECM_CONFIG0_REG_0_ADDR          0x0070
#define XG_MCX_ICM_ECM_DROP_COUNT_REG0_ADDR    0x0124
#define XCLE_BYPASS_REG0_ADDR           0x0160
#define XCLE_BYPASS_REG1_ADDR           0x0164
#define XG_CFG_BYPASS_ADDR		0x0204
#define XG_CFG_LINK_AGGR_RESUME_0_ADDR	0x0214
#define XG_LINK_STATUS_ADDR		0x0228
#define XG_TSIF_MSS_REG0_ADDR		0x02a4
#define XG_DEBUG_REG_ADDR		0x0400
#define XG_ENET_SPARE_CFG_REG_ADDR	0x040c
#define XG_ENET_SPARE_CFG_REG_1_ADDR	0x0410
#define XGENET_RX_DV_GATE_REG_0_ADDR	0x0804
#define XGENET_ECM_CONFIG0_REG_0	0x0870
#define XGENET_ICM_ECM_DROP_COUNT_REG0	0x0924
#define XGENET_CSR_ECM_CFG_0_ADDR	0x0880
#define XGENET_CSR_MULTI_DPF0_ADDR	0x0888
#define XGENET_CSR_MULTI_DPF1_ADDR	0x088c
#define XG_RXBUF_PAUSE_THRESH		0x0020
#define XG_MCX_ICM_CONFIG0_REG_0_ADDR	0x00e0
#define XG_MCX_ICM_CONFIG2_REG_0_ADDR	0x00e8

#define PCS_CONTROL_1			0x0000
#define PCS_CTRL_PCS_RST		BIT(15)

extern const struct xgene_mac_ops xgene_xgmac_ops;
extern const struct xgene_port_ops xgene_xgport_ops;

#endif /* __XGENE_ENET_XGMAC_H__ */
