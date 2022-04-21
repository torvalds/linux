/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2020  Realtek Corporation
 */

#ifndef __RTW89_PCI_H__
#define __RTW89_PCI_H__

#include "txrx.h"

#define MDIO_PG0_G1 0
#define MDIO_PG1_G1 1
#define MDIO_PG0_G2 2
#define MDIO_PG1_G2 3
#define RAC_ANA10			0x10
#define RAC_ANA19			0x19
#define RAC_ANA1F			0x1F
#define RAC_ANA24			0x24
#define B_AX_DEGLITCH			GENMASK(11, 8)
#define RAC_ANA26			0x26
#define B_AX_RXEN			GENMASK(15, 14)
#define RAC_CTRL_PPR_V1			0x30
#define B_AX_CLK_CALIB_EN		BIT(12)
#define B_AX_CALIB_EN			BIT(13)
#define B_AX_DIV			GENMASK(15, 14)
#define RAC_SET_PPR_V1			0x31

#define R_AX_DBI_FLAG			0x1090
#define B_AX_DBI_RFLAG			BIT(17)
#define B_AX_DBI_WFLAG			BIT(16)
#define B_AX_DBI_WREN_MSK		GENMASK(15, 12)
#define B_AX_DBI_ADDR_MSK		GENMASK(11, 2)
#define R_AX_DBI_WDATA			0x1094
#define R_AX_DBI_RDATA			0x1098

#define R_AX_MDIO_WDATA			0x10A4
#define R_AX_MDIO_RDATA			0x10A6

#define RTW89_PCI_WR_RETRY_CNT		20

/* Interrupts */
#define R_AX_HIMR0 0x01A0
#define B_AX_HALT_C2H_INT_EN BIT(21)
#define R_AX_HISR0 0x01A4

#define R_AX_MDIO_CFG			0x10A0
#define B_AX_MDIO_PHY_ADDR_MASK		GENMASK(13, 12)
#define B_AX_MDIO_RFLAG			BIT(9)
#define B_AX_MDIO_WFLAG			BIT(8)
#define B_AX_MDIO_ADDR_MASK		GENMASK(4, 0)

#define R_AX_PCIE_HIMR00	0x10B0
#define B_AX_HC00ISR_IND_INT_EN		BIT(27)
#define B_AX_HD1ISR_IND_INT_EN		BIT(26)
#define B_AX_HD0ISR_IND_INT_EN		BIT(25)
#define B_AX_HS0ISR_IND_INT_EN		BIT(24)
#define B_AX_RETRAIN_INT_EN		BIT(21)
#define B_AX_RPQBD_FULL_INT_EN		BIT(20)
#define B_AX_RDU_INT_EN			BIT(19)
#define B_AX_RXDMA_STUCK_INT_EN		BIT(18)
#define B_AX_TXDMA_STUCK_INT_EN		BIT(17)
#define B_AX_PCIE_HOTRST_INT_EN		BIT(16)
#define B_AX_PCIE_FLR_INT_EN		BIT(15)
#define B_AX_PCIE_PERST_INT_EN		BIT(14)
#define B_AX_TXDMA_CH12_INT_EN		BIT(13)
#define B_AX_TXDMA_CH9_INT_EN		BIT(12)
#define B_AX_TXDMA_CH8_INT_EN		BIT(11)
#define B_AX_TXDMA_ACH7_INT_EN		BIT(10)
#define B_AX_TXDMA_ACH6_INT_EN		BIT(9)
#define B_AX_TXDMA_ACH5_INT_EN		BIT(8)
#define B_AX_TXDMA_ACH4_INT_EN		BIT(7)
#define B_AX_TXDMA_ACH3_INT_EN		BIT(6)
#define B_AX_TXDMA_ACH2_INT_EN		BIT(5)
#define B_AX_TXDMA_ACH1_INT_EN		BIT(4)
#define B_AX_TXDMA_ACH0_INT_EN		BIT(3)
#define B_AX_RPQDMA_INT_EN		BIT(2)
#define B_AX_RXP1DMA_INT_EN		BIT(1)
#define B_AX_RXDMA_INT_EN		BIT(0)

#define R_AX_PCIE_HISR00	0x10B4
#define B_AX_HC00ISR_IND_INT		BIT(27)
#define B_AX_HD1ISR_IND_INT		BIT(26)
#define B_AX_HD0ISR_IND_INT		BIT(25)
#define B_AX_HS0ISR_IND_INT		BIT(24)
#define B_AX_RETRAIN_INT		BIT(21)
#define B_AX_RPQBD_FULL_INT		BIT(20)
#define B_AX_RDU_INT			BIT(19)
#define B_AX_RXDMA_STUCK_INT		BIT(18)
#define B_AX_TXDMA_STUCK_INT		BIT(17)
#define B_AX_PCIE_HOTRST_INT		BIT(16)
#define B_AX_PCIE_FLR_INT		BIT(15)
#define B_AX_PCIE_PERST_INT		BIT(14)
#define B_AX_TXDMA_CH12_INT		BIT(13)
#define B_AX_TXDMA_CH9_INT		BIT(12)
#define B_AX_TXDMA_CH8_INT		BIT(11)
#define B_AX_TXDMA_ACH7_INT		BIT(10)
#define B_AX_TXDMA_ACH6_INT		BIT(9)
#define B_AX_TXDMA_ACH5_INT		BIT(8)
#define B_AX_TXDMA_ACH4_INT		BIT(7)
#define B_AX_TXDMA_ACH3_INT		BIT(6)
#define B_AX_TXDMA_ACH2_INT		BIT(5)
#define B_AX_TXDMA_ACH1_INT		BIT(4)
#define B_AX_TXDMA_ACH0_INT		BIT(3)
#define B_AX_RPQDMA_INT			BIT(2)
#define B_AX_RXP1DMA_INT		BIT(1)
#define B_AX_RXDMA_INT			BIT(0)

#define R_AX_PCIE_HIMR10	0x13B0
#define B_AX_HC10ISR_IND_INT_EN		BIT(28)
#define B_AX_TXDMA_CH11_INT_EN		BIT(12)
#define B_AX_TXDMA_CH10_INT_EN		BIT(11)

#define R_AX_PCIE_HISR10	0x13B4
#define B_AX_HC10ISR_IND_INT		BIT(28)
#define B_AX_TXDMA_CH11_INT		BIT(12)
#define B_AX_TXDMA_CH10_INT		BIT(11)

/* TX/RX */
#define R_AX_RXQ_RXBD_IDX	0x1050
#define R_AX_RPQ_RXBD_IDX	0x1054
#define R_AX_ACH0_TXBD_IDX	0x1058
#define R_AX_ACH1_TXBD_IDX	0x105C
#define R_AX_ACH2_TXBD_IDX	0x1060
#define R_AX_ACH3_TXBD_IDX	0x1064
#define R_AX_ACH4_TXBD_IDX	0x1068
#define R_AX_ACH5_TXBD_IDX	0x106C
#define R_AX_ACH6_TXBD_IDX	0x1070
#define R_AX_ACH7_TXBD_IDX	0x1074
#define R_AX_CH8_TXBD_IDX	0x1078 /* Management Queue band 0 */
#define R_AX_CH9_TXBD_IDX	0x107C /* HI Queue band 0 */
#define R_AX_CH10_TXBD_IDX	0x137C /* Management Queue band 1 */
#define R_AX_CH11_TXBD_IDX	0x1380 /* HI Queue band 1 */
#define R_AX_CH12_TXBD_IDX	0x1080 /* FWCMD Queue */
#define R_AX_CH10_TXBD_IDX_V1	0x11D0
#define R_AX_CH11_TXBD_IDX_V1	0x11D4
#define R_AX_RXQ_RXBD_IDX_V1	0x1218
#define R_AX_RPQ_RXBD_IDX_V1	0x121C
#define TXBD_HW_IDX_MASK	GENMASK(27, 16)
#define TXBD_HOST_IDX_MASK	GENMASK(11, 0)

#define R_AX_ACH0_TXBD_DESA_L	0x1110
#define R_AX_ACH0_TXBD_DESA_H	0x1114
#define R_AX_ACH1_TXBD_DESA_L	0x1118
#define R_AX_ACH1_TXBD_DESA_H	0x111C
#define R_AX_ACH2_TXBD_DESA_L	0x1120
#define R_AX_ACH2_TXBD_DESA_H	0x1124
#define R_AX_ACH3_TXBD_DESA_L	0x1128
#define R_AX_ACH3_TXBD_DESA_H	0x112C
#define R_AX_ACH4_TXBD_DESA_L	0x1130
#define R_AX_ACH4_TXBD_DESA_H	0x1134
#define R_AX_ACH5_TXBD_DESA_L	0x1138
#define R_AX_ACH5_TXBD_DESA_H	0x113C
#define R_AX_ACH6_TXBD_DESA_L	0x1140
#define R_AX_ACH6_TXBD_DESA_H	0x1144
#define R_AX_ACH7_TXBD_DESA_L	0x1148
#define R_AX_ACH7_TXBD_DESA_H	0x114C
#define R_AX_CH8_TXBD_DESA_L	0x1150
#define R_AX_CH8_TXBD_DESA_H	0x1154
#define R_AX_CH9_TXBD_DESA_L	0x1158
#define R_AX_CH9_TXBD_DESA_H	0x115C
#define R_AX_CH10_TXBD_DESA_L	0x1358
#define R_AX_CH10_TXBD_DESA_H	0x135C
#define R_AX_CH11_TXBD_DESA_L	0x1360
#define R_AX_CH11_TXBD_DESA_H	0x1364
#define R_AX_CH12_TXBD_DESA_L	0x1160
#define R_AX_CH12_TXBD_DESA_H	0x1164
#define R_AX_RXQ_RXBD_DESA_L	0x1100
#define R_AX_RXQ_RXBD_DESA_H	0x1104
#define R_AX_RPQ_RXBD_DESA_L	0x1108
#define R_AX_RPQ_RXBD_DESA_H	0x110C
#define R_AX_RXQ_RXBD_DESA_L_V1 0x1220
#define R_AX_RXQ_RXBD_DESA_H_V1 0x1224
#define R_AX_RPQ_RXBD_DESA_L_V1 0x1228
#define R_AX_RPQ_RXBD_DESA_H_V1 0x122C
#define R_AX_ACH0_TXBD_DESA_L_V1 0x1230
#define R_AX_ACH0_TXBD_DESA_H_V1 0x1234
#define R_AX_ACH1_TXBD_DESA_L_V1 0x1238
#define R_AX_ACH1_TXBD_DESA_H_V1 0x123C
#define R_AX_ACH2_TXBD_DESA_L_V1 0x1240
#define R_AX_ACH2_TXBD_DESA_H_V1 0x1244
#define R_AX_ACH3_TXBD_DESA_L_V1 0x1248
#define R_AX_ACH3_TXBD_DESA_H_V1 0x124C
#define R_AX_ACH4_TXBD_DESA_L_V1 0x1250
#define R_AX_ACH4_TXBD_DESA_H_V1 0x1254
#define R_AX_ACH5_TXBD_DESA_L_V1 0x1258
#define R_AX_ACH5_TXBD_DESA_H_V1 0x125C
#define R_AX_ACH6_TXBD_DESA_L_V1 0x1260
#define R_AX_ACH6_TXBD_DESA_H_V1 0x1264
#define R_AX_ACH7_TXBD_DESA_L_V1 0x1268
#define R_AX_ACH7_TXBD_DESA_H_V1 0x126C
#define R_AX_CH8_TXBD_DESA_L_V1 0x1270
#define R_AX_CH8_TXBD_DESA_H_V1 0x1274
#define R_AX_CH9_TXBD_DESA_L_V1 0x1278
#define R_AX_CH9_TXBD_DESA_H_V1 0x127C
#define R_AX_CH12_TXBD_DESA_L_V1 0x1280
#define R_AX_CH12_TXBD_DESA_H_V1 0x1284
#define R_AX_CH10_TXBD_DESA_L_V1 0x1458
#define R_AX_CH10_TXBD_DESA_H_V1 0x145C
#define R_AX_CH11_TXBD_DESA_L_V1 0x1460
#define R_AX_CH11_TXBD_DESA_H_V1 0x1464
#define B_AX_DESC_NUM_MSK		GENMASK(11, 0)

#define R_AX_RXQ_RXBD_NUM	0x1020
#define R_AX_RPQ_RXBD_NUM	0x1022
#define R_AX_ACH0_TXBD_NUM	0x1024
#define R_AX_ACH1_TXBD_NUM	0x1026
#define R_AX_ACH2_TXBD_NUM	0x1028
#define R_AX_ACH3_TXBD_NUM	0x102A
#define R_AX_ACH4_TXBD_NUM	0x102C
#define R_AX_ACH5_TXBD_NUM	0x102E
#define R_AX_ACH6_TXBD_NUM	0x1030
#define R_AX_ACH7_TXBD_NUM	0x1032
#define R_AX_CH8_TXBD_NUM	0x1034
#define R_AX_CH9_TXBD_NUM	0x1036
#define R_AX_CH10_TXBD_NUM	0x1338
#define R_AX_CH11_TXBD_NUM	0x133A
#define R_AX_CH12_TXBD_NUM	0x1038
#define R_AX_RXQ_RXBD_NUM_V1	0x1210
#define R_AX_RPQ_RXBD_NUM_V1	0x1212
#define R_AX_CH10_TXBD_NUM_V1	0x1438
#define R_AX_CH11_TXBD_NUM_V1	0x143A

#define R_AX_ACH0_BDRAM_CTRL	0x1200
#define R_AX_ACH1_BDRAM_CTRL	0x1204
#define R_AX_ACH2_BDRAM_CTRL	0x1208
#define R_AX_ACH3_BDRAM_CTRL	0x120C
#define R_AX_ACH4_BDRAM_CTRL	0x1210
#define R_AX_ACH5_BDRAM_CTRL	0x1214
#define R_AX_ACH6_BDRAM_CTRL	0x1218
#define R_AX_ACH7_BDRAM_CTRL	0x121C
#define R_AX_CH8_BDRAM_CTRL	0x1220
#define R_AX_CH9_BDRAM_CTRL	0x1224
#define R_AX_CH10_BDRAM_CTRL	0x1320
#define R_AX_CH11_BDRAM_CTRL	0x1324
#define R_AX_CH12_BDRAM_CTRL	0x1228
#define R_AX_ACH0_BDRAM_CTRL_V1 0x1300
#define R_AX_ACH1_BDRAM_CTRL_V1 0x1304
#define R_AX_ACH2_BDRAM_CTRL_V1 0x1308
#define R_AX_ACH3_BDRAM_CTRL_V1 0x130C
#define R_AX_ACH4_BDRAM_CTRL_V1 0x1310
#define R_AX_ACH5_BDRAM_CTRL_V1 0x1314
#define R_AX_ACH6_BDRAM_CTRL_V1 0x1318
#define R_AX_ACH7_BDRAM_CTRL_V1 0x131C
#define R_AX_CH8_BDRAM_CTRL_V1 0x1320
#define R_AX_CH9_BDRAM_CTRL_V1 0x1324
#define R_AX_CH12_BDRAM_CTRL_V1 0x1328
#define R_AX_CH10_BDRAM_CTRL_V1 0x1420
#define R_AX_CH11_BDRAM_CTRL_V1 0x1424
#define BDRAM_SIDX_MASK		GENMASK(7, 0)
#define BDRAM_MAX_MASK		GENMASK(15, 8)
#define BDRAM_MIN_MASK		GENMASK(23, 16)

#define R_AX_PCIE_INIT_CFG1	0x1000
#define B_AX_PCIE_RXRST_KEEP_REG	BIT(23)
#define B_AX_PCIE_TXRST_KEEP_REG	BIT(22)
#define B_AX_PCIE_PERST_KEEP_REG	BIT(21)
#define B_AX_PCIE_FLR_KEEP_REG		BIT(20)
#define B_AX_PCIE_TRAIN_KEEP_REG	BIT(19)
#define B_AX_RXBD_MODE			BIT(18)
#define B_AX_PCIE_MAX_RXDMA_MASK	GENMASK(16, 14)
#define B_AX_RXHCI_EN			BIT(13)
#define B_AX_LATENCY_CONTROL		BIT(12)
#define B_AX_TXHCI_EN			BIT(11)
#define B_AX_PCIE_MAX_TXDMA_MASK	GENMASK(10, 8)
#define B_AX_TX_TRUNC_MODE		BIT(5)
#define B_AX_RX_TRUNC_MODE		BIT(4)
#define B_AX_RST_BDRAM			BIT(3)
#define B_AX_DIS_RXDMA_PRE		BIT(2)

#define R_AX_TXDMA_ADDR_H	0x10F0
#define R_AX_RXDMA_ADDR_H	0x10F4

#define R_AX_PCIE_DMA_STOP1	0x1010
#define B_AX_STOP_PCIEIO		BIT(20)
#define B_AX_STOP_WPDMA			BIT(19)
#define B_AX_STOP_CH12			BIT(18)
#define B_AX_STOP_CH9			BIT(17)
#define B_AX_STOP_CH8			BIT(16)
#define B_AX_STOP_ACH7			BIT(15)
#define B_AX_STOP_ACH6			BIT(14)
#define B_AX_STOP_ACH5			BIT(13)
#define B_AX_STOP_ACH4			BIT(12)
#define B_AX_STOP_ACH3			BIT(11)
#define B_AX_STOP_ACH2			BIT(10)
#define B_AX_STOP_ACH1			BIT(9)
#define B_AX_STOP_ACH0			BIT(8)
#define B_AX_STOP_RPQ			BIT(1)
#define B_AX_STOP_RXQ			BIT(0)
#define B_AX_TX_STOP1_ALL		GENMASK(18, 8)

#define R_AX_PCIE_DMA_STOP2	0x1310
#define B_AX_STOP_CH11			BIT(1)
#define B_AX_STOP_CH10			BIT(0)
#define B_AX_TX_STOP2_ALL		GENMASK(1, 0)

#define R_AX_TXBD_RWPTR_CLR1	0x1014
#define B_AX_CLR_CH12_IDX		BIT(10)
#define B_AX_CLR_CH9_IDX		BIT(9)
#define B_AX_CLR_CH8_IDX		BIT(8)
#define B_AX_CLR_ACH7_IDX		BIT(7)
#define B_AX_CLR_ACH6_IDX		BIT(6)
#define B_AX_CLR_ACH5_IDX		BIT(5)
#define B_AX_CLR_ACH4_IDX		BIT(4)
#define B_AX_CLR_ACH3_IDX		BIT(3)
#define B_AX_CLR_ACH2_IDX		BIT(2)
#define B_AX_CLR_ACH1_IDX		BIT(1)
#define B_AX_CLR_ACH0_IDX		BIT(0)
#define B_AX_TXBD_CLR1_ALL		GENMASK(10, 0)

#define R_AX_RXBD_RWPTR_CLR	0x1018
#define B_AX_CLR_RPQ_IDX		BIT(1)
#define B_AX_CLR_RXQ_IDX		BIT(0)
#define B_AX_RXBD_CLR_ALL		GENMASK(1, 0)

#define R_AX_TXBD_RWPTR_CLR2	0x1314
#define B_AX_CLR_CH11_IDX		BIT(1)
#define B_AX_CLR_CH10_IDX		BIT(0)
#define B_AX_TXBD_CLR2_ALL		GENMASK(1, 0)

#define R_AX_PCIE_DMA_BUSY1	0x101C
#define B_AX_PCIEIO_RX_BUSY		BIT(22)
#define B_AX_PCIEIO_TX_BUSY		BIT(21)
#define B_AX_PCIEIO_BUSY		BIT(20)
#define B_AX_WPDMA_BUSY			BIT(19)

#define R_AX_PCIE_DMA_BUSY2	0x131C
#define B_AX_CH11_BUSY			BIT(1)
#define B_AX_CH10_BUSY			BIT(0)

/* Configure */
#define R_AX_PCIE_INIT_CFG2		0x1004
#define B_AX_WD_ITVL_IDLE		GENMASK(27, 24)
#define B_AX_WD_ITVL_ACT		GENMASK(19, 16)

#define R_AX_PCIE_PS_CTRL		0x1008
#define B_AX_L1OFF_PWR_OFF_EN		BIT(5)

#define R_AX_INT_MIT_RX			0x10D4
#define B_AX_RXMIT_RXP2_SEL		BIT(19)
#define B_AX_RXMIT_RXP1_SEL		BIT(18)
#define B_AX_RXTIMER_UNIT_MASK		GENMASK(17, 16)
#define AX_RXTIMER_UNIT_64US		0
#define AX_RXTIMER_UNIT_128US		1
#define AX_RXTIMER_UNIT_256US		2
#define AX_RXTIMER_UNIT_512US		3
#define B_AX_RXCOUNTER_MATCH_MASK	GENMASK(15, 8)
#define B_AX_RXTIMER_MATCH_MASK		GENMASK(7, 0)

#define R_AX_DBG_ERR_FLAG		0x11C4
#define B_AX_PCIE_RPQ_FULL		BIT(29)
#define B_AX_PCIE_RXQ_FULL		BIT(28)
#define B_AX_CPL_STATUS_MASK		GENMASK(27, 25)
#define B_AX_RX_STUCK			BIT(22)
#define B_AX_TX_STUCK			BIT(21)
#define B_AX_PCIEDBG_TXERR0		BIT(16)
#define B_AX_PCIE_RXP1_ERR0		BIT(4)
#define B_AX_PCIE_TXBD_LEN0		BIT(1)
#define B_AX_PCIE_TXBD_4KBOUD_LENERR	BIT(0)

#define R_AX_LBC_WATCHDOG		0x11D8
#define B_AX_LBC_TIMER			GENMASK(7, 4)
#define B_AX_LBC_FLAG			BIT(1)
#define B_AX_LBC_EN			BIT(0)

#define R_AX_PCIE_EXP_CTRL		0x13F0
#define B_AX_EN_CHKDSC_NO_RX_STUCK	BIT(20)
#define B_AX_MAX_TAG_NUM		GENMASK(18, 16)
#define B_AX_SIC_EN_FORCE_CLKREQ	BIT(4)

#define R_AX_PCIE_RX_PREF_ADV		0x13F4
#define B_AX_RXDMA_PREF_ADV_EN		BIT(0)

#define RTW89_PCI_TXBD_NUM_MAX		256
#define RTW89_PCI_RXBD_NUM_MAX		256
#define RTW89_PCI_TXWD_NUM_MAX		512
#define RTW89_PCI_TXWD_PAGE_SIZE	128
#define RTW89_PCI_ADDRINFO_MAX		4
#define RTW89_PCI_RX_BUF_SIZE		11460

#define RTW89_PCI_POLL_BDRAM_RST_CNT	100
#define RTW89_PCI_MULTITAG		8

/* PCIE CFG register */
#define RTW89_PCIE_ASPM_CTRL		0x070F
#define RTW89_L1DLY_MASK		GENMASK(5, 3)
#define RTW89_L0DLY_MASK		GENMASK(2, 0)
#define RTW89_PCIE_TIMER_CTRL		0x0718
#define RTW89_PCIE_BIT_L1SUB		BIT(5)
#define RTW89_PCIE_L1_CTRL		0x0719
#define RTW89_PCIE_BIT_CLK		BIT(4)
#define RTW89_PCIE_BIT_L1		BIT(3)
#define RTW89_PCIE_CLK_CTRL		0x0725
#define RTW89_PCIE_RST_MSTATE		0x0B48
#define RTW89_PCIE_BIT_CFG_RST_MSTATE	BIT(0)
#define RTW89_PCIE_PHY_RATE		0x82
#define RTW89_PCIE_PHY_RATE_MASK	GENMASK(1, 0)
#define INTF_INTGRA_MINREF_V1	90
#define INTF_INTGRA_HOSTREF_V1	100

enum rtw89_pcie_phy {
	PCIE_PHY_GEN1,
	PCIE_PHY_GEN2,
	PCIE_PHY_GEN1_UNDEFINE = 0x7F,
};

enum mac_ax_func_sw {
	MAC_AX_FUNC_DIS,
	MAC_AX_FUNC_EN,
};

enum rtw89_pcie_l0sdly {
	PCIE_L0SDLY_1US = 0,
	PCIE_L0SDLY_2US = 1,
	PCIE_L0SDLY_3US = 2,
	PCIE_L0SDLY_4US = 3,
	PCIE_L0SDLY_5US = 4,
	PCIE_L0SDLY_6US = 5,
	PCIE_L0SDLY_7US = 6,
};

enum rtw89_pcie_l1dly {
	PCIE_L1DLY_16US = 4,
	PCIE_L1DLY_32US = 5,
	PCIE_L1DLY_64US = 6,
	PCIE_L1DLY_HW_INFI = 7,
};

enum rtw89_pcie_clkdly_hw {
	PCIE_CLKDLY_HW_0 = 0,
	PCIE_CLKDLY_HW_30US = 0x1,
	PCIE_CLKDLY_HW_50US = 0x2,
	PCIE_CLKDLY_HW_100US = 0x3,
	PCIE_CLKDLY_HW_150US = 0x4,
	PCIE_CLKDLY_HW_200US = 0x5,
};

struct rtw89_pci_ch_dma_addr {
	u32 num;
	u32 idx;
	u32 bdram;
	u32 desa_l;
	u32 desa_h;
};

struct rtw89_pci_ch_dma_addr_set {
	struct rtw89_pci_ch_dma_addr tx[RTW89_TXCH_NUM];
	struct rtw89_pci_ch_dma_addr rx[RTW89_RXCH_NUM];
};

struct rtw89_pci_info {
	const struct rtw89_pci_ch_dma_addr_set *dma_addr_set;
};

struct rtw89_pci_bd_ram {
	u8 start_idx;
	u8 max_num;
	u8 min_num;
};

struct rtw89_pci_tx_data {
	dma_addr_t dma;
};

struct rtw89_pci_rx_info {
	dma_addr_t dma;
	u32 fs:1, ls:1, tag:11, len:14;
};

#define RTW89_PCI_TXBD_OPTION_LS	BIT(14)

struct rtw89_pci_tx_bd_32 {
	__le16 length;
	__le16 option;
	__le32 dma;
} __packed;

#define RTW89_PCI_TXWP_VALID		BIT(15)

struct rtw89_pci_tx_wp_info {
	__le16 seq0;
	__le16 seq1;
	__le16 seq2;
	__le16 seq3;
} __packed;

#define RTW89_PCI_ADDR_MSDU_LS		BIT(15)
#define RTW89_PCI_ADDR_LS		BIT(14)
#define RTW89_PCI_ADDR_HIGH(a)		(((a) << 6) & GENMASK(13, 6))
#define RTW89_PCI_ADDR_NUM(x)		((x) & GENMASK(5, 0))

struct rtw89_pci_tx_addr_info_32 {
	__le16 length;
	__le16 option;
	__le32 dma;
} __packed;

#define RTW89_PCI_RPP_POLLUTED		BIT(31)
#define RTW89_PCI_RPP_SEQ		GENMASK(30, 16)
#define RTW89_PCI_RPP_TX_STATUS		GENMASK(15, 13)
#define RTW89_TX_DONE			0x0
#define RTW89_TX_RETRY_LIMIT		0x1
#define RTW89_TX_LIFE_TIME		0x2
#define RTW89_TX_MACID_DROP		0x3
#define RTW89_PCI_RPP_QSEL		GENMASK(12, 8)
#define RTW89_PCI_RPP_MACID		GENMASK(7, 0)

struct rtw89_pci_rpp_fmt {
	__le32 dword;
} __packed;

struct rtw89_pci_rx_bd_32 {
	__le16 buf_size;
	__le16 rsvd;
	__le32 dma;
} __packed;

#define RTW89_PCI_RXBD_FS		BIT(15)
#define RTW89_PCI_RXBD_LS		BIT(14)
#define RTW89_PCI_RXBD_WRITE_SIZE	GENMASK(13, 0)
#define RTW89_PCI_RXBD_TAG		GENMASK(28, 16)

struct rtw89_pci_rxbd_info {
	__le32 dword;
};

struct rtw89_pci_tx_wd {
	struct list_head list;
	struct sk_buff_head queue;

	void *vaddr;
	dma_addr_t paddr;
	u32 len;
	u32 seq;
};

struct rtw89_pci_dma_ring {
	void *head;
	u8 desc_size;
	dma_addr_t dma;

	struct rtw89_pci_ch_dma_addr addr;

	u32 len;
	u32 wp; /* host idx */
	u32 rp; /* hw idx */
};

struct rtw89_pci_tx_wd_ring {
	void *head;
	dma_addr_t dma;

	struct rtw89_pci_tx_wd pages[RTW89_PCI_TXWD_NUM_MAX];
	struct list_head free_pages;

	u32 page_size;
	u32 page_num;
	u32 curr_num;
};

#define RTW89_RX_TAG_MAX		0x1fff

struct rtw89_pci_tx_ring {
	struct rtw89_pci_tx_wd_ring wd_ring;
	struct rtw89_pci_dma_ring bd_ring;
	struct list_head busy_pages;
	u8 txch;
	bool dma_enabled;
	u16 tag; /* range from 0x0001 ~ 0x1fff */

	u64 tx_cnt;
	u64 tx_acked;
	u64 tx_retry_lmt;
	u64 tx_life_time;
	u64 tx_mac_id_drop;
};

struct rtw89_pci_rx_ring {
	struct rtw89_pci_dma_ring bd_ring;
	struct sk_buff *buf[RTW89_PCI_RXBD_NUM_MAX];
	u32 buf_sz;
	struct sk_buff *diliver_skb;
	struct rtw89_rx_desc_info diliver_desc;
};

struct rtw89_pci_isrs {
	u32 halt_c2h_isrs;
	u32 isrs[2];
};

struct rtw89_pci {
	struct pci_dev *pdev;

	/* protect HW irq related registers */
	spinlock_t irq_lock;
	/* protect TRX resources (exclude RXQ) */
	spinlock_t trx_lock;
	bool running;
	struct rtw89_pci_tx_ring tx_rings[RTW89_TXCH_NUM];
	struct rtw89_pci_rx_ring rx_rings[RTW89_RXCH_NUM];
	struct sk_buff_head h2c_queue;
	struct sk_buff_head h2c_release_queue;

	u32 halt_c2h_intrs;
	u32 intrs[2];
	void __iomem *mmap;
};

static inline struct rtw89_pci_rx_info *RTW89_PCI_RX_SKB_CB(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct rtw89_pci_tx_data) >
		     sizeof(info->status.status_driver_data));

	return (struct rtw89_pci_rx_info *)skb->cb;
}

static inline struct rtw89_pci_rx_bd_32 *
RTW89_PCI_RX_BD(struct rtw89_pci_rx_ring *rx_ring, u32 idx)
{
	struct rtw89_pci_dma_ring *bd_ring = &rx_ring->bd_ring;
	u8 *head = bd_ring->head;
	u32 desc_size = bd_ring->desc_size;
	u32 offset = idx * desc_size;

	return (struct rtw89_pci_rx_bd_32 *)(head + offset);
}

static inline void
rtw89_pci_rxbd_increase(struct rtw89_pci_rx_ring *rx_ring, u32 cnt)
{
	struct rtw89_pci_dma_ring *bd_ring = &rx_ring->bd_ring;

	bd_ring->wp += cnt;

	if (bd_ring->wp >= bd_ring->len)
		bd_ring->wp -= bd_ring->len;
}

static inline struct rtw89_pci_tx_data *RTW89_PCI_TX_SKB_CB(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	return (struct rtw89_pci_tx_data *)info->status.status_driver_data;
}

static inline struct rtw89_pci_tx_bd_32 *
rtw89_pci_get_next_txbd(struct rtw89_pci_tx_ring *tx_ring)
{
	struct rtw89_pci_dma_ring *bd_ring = &tx_ring->bd_ring;
	struct rtw89_pci_tx_bd_32 *tx_bd, *head;

	head = bd_ring->head;
	tx_bd = head + bd_ring->wp;

	return tx_bd;
}

static inline struct rtw89_pci_tx_wd *
rtw89_pci_dequeue_txwd(struct rtw89_pci_tx_ring *tx_ring)
{
	struct rtw89_pci_tx_wd_ring *wd_ring = &tx_ring->wd_ring;
	struct rtw89_pci_tx_wd *txwd;

	txwd = list_first_entry_or_null(&wd_ring->free_pages,
					struct rtw89_pci_tx_wd, list);
	if (!txwd)
		return NULL;

	list_del_init(&txwd->list);
	txwd->len = 0;
	wd_ring->curr_num--;

	return txwd;
}

static inline void
rtw89_pci_enqueue_txwd(struct rtw89_pci_tx_ring *tx_ring,
		       struct rtw89_pci_tx_wd *txwd)
{
	struct rtw89_pci_tx_wd_ring *wd_ring = &tx_ring->wd_ring;

	memset(txwd->vaddr, 0, wd_ring->page_size);
	list_add_tail(&txwd->list, &wd_ring->free_pages);
	wd_ring->curr_num++;
}

static inline bool rtw89_pci_ltr_is_err_reg_val(u32 val)
{
	return val == 0xffffffff || val == 0xeaeaeaea;
}

extern const struct dev_pm_ops rtw89_pm_ops;
extern const struct rtw89_pci_ch_dma_addr_set rtw89_pci_ch_dma_addr_set;
extern const struct rtw89_pci_ch_dma_addr_set rtw89_pci_ch_dma_addr_set_v1;

struct pci_device_id;

int rtw89_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void rtw89_pci_remove(struct pci_dev *pdev);

#endif
