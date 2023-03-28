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
#define RAC_CTRL_PPR			0x00
#define RAC_ANA0A			0x0A
#define B_BAC_EQ_SEL			BIT(5)
#define RAC_ANA0C			0x0C
#define B_PCIE_BIT_PSAVE		BIT(15)
#define RAC_ANA10			0x10
#define B_PCIE_BIT_PINOUT_DIS		BIT(3)
#define RAC_REG_REV2			0x1B
#define BAC_CMU_EN_DLY_MASK		GENMASK(15, 12)
#define PCIE_DPHY_DLY_25US		0x1
#define RAC_ANA19			0x19
#define B_PCIE_BIT_RD_SEL		BIT(2)
#define RAC_REG_FLD_0			0x1D
#define BAC_AUTOK_N_MASK		GENMASK(3, 2)
#define PCIE_AUTOK_4			0x3
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

#define R_AX_PCIE_PS_CTRL_V1		0x3008
#define B_AX_CMAC_EXIT_L1_EN		BIT(7)
#define B_AX_DMAC0_EXIT_L1_EN		BIT(6)
#define B_AX_SEL_XFER_PENDING		BIT(3)
#define B_AX_SEL_REQ_ENTR_L1		BIT(2)
#define B_AX_SEL_REQ_EXIT_L1		BIT(0)

#define R_AX_PCIE_MIX_CFG_V1		0x300C
#define B_AX_ASPM_CTRL_L1		BIT(17)
#define B_AX_ASPM_CTRL_L0		BIT(16)
#define B_AX_ASPM_CTRL_MASK		GENMASK(17, 16)
#define B_AX_XFER_PENDING_FW		BIT(11)
#define B_AX_XFER_PENDING		BIT(10)
#define B_AX_REQ_EXIT_L1		BIT(9)
#define B_AX_REQ_ENTR_L1		BIT(8)
#define B_AX_L1SUB_DISABLE		BIT(0)

#define R_AX_L1_CLK_CTRL		0x3010
#define B_AX_CLK_REQ_N			BIT(1)

#define R_AX_PCIE_BG_CLR		0x303C
#define B_AX_BG_CLR_ASYNC_M3		BIT(4)

#define R_AX_PCIE_LAT_CTRL		0x3044
#define B_AX_CLK_REQ_SEL_OPT		BIT(1)
#define B_AX_CLK_REQ_SEL		BIT(0)

#define R_AX_PCIE_IO_RCY_M1 0x3100
#define B_AX_PCIE_IO_RCY_P_M1 BIT(5)
#define B_AX_PCIE_IO_RCY_WDT_P_M1 BIT(4)
#define B_AX_PCIE_IO_RCY_WDT_MODE_M1 BIT(3)
#define B_AX_PCIE_IO_RCY_TRIG_M1 BIT(0)

#define R_AX_PCIE_WDT_TIMER_M1 0x3104
#define B_AX_PCIE_WDT_TIMER_M1_MASK GENMASK(31, 0)

#define R_AX_PCIE_IO_RCY_M2 0x310C
#define B_AX_PCIE_IO_RCY_P_M2 BIT(5)
#define B_AX_PCIE_IO_RCY_WDT_P_M2 BIT(4)
#define B_AX_PCIE_IO_RCY_WDT_MODE_M2 BIT(3)
#define B_AX_PCIE_IO_RCY_TRIG_M2 BIT(0)

#define R_AX_PCIE_WDT_TIMER_M2 0x3110
#define B_AX_PCIE_WDT_TIMER_M2_MASK GENMASK(31, 0)

#define R_AX_PCIE_IO_RCY_E0 0x3118
#define B_AX_PCIE_IO_RCY_P_E0 BIT(5)
#define B_AX_PCIE_IO_RCY_WDT_P_E0 BIT(4)
#define B_AX_PCIE_IO_RCY_WDT_MODE_E0 BIT(3)
#define B_AX_PCIE_IO_RCY_TRIG_E0 BIT(0)

#define R_AX_PCIE_WDT_TIMER_E0 0x311C
#define B_AX_PCIE_WDT_TIMER_E0_MASK GENMASK(31, 0)

#define R_AX_PCIE_IO_RCY_S1 0x3124
#define B_AX_PCIE_IO_RCY_RP_S1 BIT(7)
#define B_AX_PCIE_IO_RCY_WP_S1 BIT(6)
#define B_AX_PCIE_IO_RCY_WDT_RP_S1 BIT(5)
#define B_AX_PCIE_IO_RCY_WDT_WP_S1 BIT(4)
#define B_AX_PCIE_IO_RCY_WDT_MODE_S1 BIT(3)
#define B_AX_PCIE_IO_RCY_RTRIG_S1 BIT(1)
#define B_AX_PCIE_IO_RCY_WTRIG_S1 BIT(0)

#define R_AX_PCIE_WDT_TIMER_S1 0x3128
#define B_AX_PCIE_WDT_TIMER_S1_MASK GENMASK(31, 0)

#define R_RAC_DIRECT_OFFSET_G1 0x3800
#define FILTER_OUT_EQ_MASK GENMASK(14, 10)
#define R_RAC_DIRECT_OFFSET_G2 0x3880
#define REG_FILTER_OUT_MASK GENMASK(6, 2)
#define RAC_MULT 2

#define RTW89_PCI_WR_RETRY_CNT		20

/* Interrupts */
#define R_AX_HIMR0 0x01A0
#define B_AX_WDT_TIMEOUT_INT_EN BIT(22)
#define B_AX_HALT_C2H_INT_EN BIT(21)
#define R_AX_HISR0 0x01A4

#define R_AX_HIMR1 0x01A8
#define B_AX_GPIO18_INT_EN BIT(2)
#define B_AX_GPIO17_INT_EN BIT(1)
#define B_AX_GPIO16_INT_EN BIT(0)

#define R_AX_HISR1 0x01AC
#define B_AX_GPIO18_INT BIT(2)
#define B_AX_GPIO17_INT BIT(1)
#define B_AX_GPIO16_INT BIT(0)

#define R_AX_MDIO_CFG			0x10A0
#define B_AX_MDIO_PHY_ADDR_MASK		GENMASK(13, 12)
#define B_AX_MDIO_RFLAG			BIT(9)
#define B_AX_MDIO_WFLAG			BIT(8)
#define B_AX_MDIO_ADDR_MASK		GENMASK(4, 0)

#define R_AX_PCIE_HIMR00	0x10B0
#define R_AX_HAXI_HIMR00 0x10B0
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
#define R_AX_HAXI_HISR00 0x10B4
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

#define R_AX_HAXI_IDCT_MSK 0x10B8
#define B_AX_TXBD_LEN0_ERR_IDCT_MSK BIT(3)
#define B_AX_TXBD_4KBOUND_ERR_IDCT_MSK BIT(2)
#define B_AX_RXMDA_STUCK_IDCT_MSK BIT(1)
#define B_AX_TXMDA_STUCK_IDCT_MSK BIT(0)

#define R_AX_HAXI_IDCT 0x10BC
#define B_AX_TXBD_LEN0_ERR_IDCT BIT(3)
#define B_AX_TXBD_4KBOUND_ERR_IDCT BIT(2)
#define B_AX_RXMDA_STUCK_IDCT BIT(1)
#define B_AX_TXMDA_STUCK_IDCT BIT(0)

#define R_AX_HAXI_HIMR10 0x11E0
#define B_AX_TXDMA_CH11_INT_EN_V1 BIT(1)
#define B_AX_TXDMA_CH10_INT_EN_V1 BIT(0)

#define R_AX_PCIE_HIMR10	0x13B0
#define B_AX_HC10ISR_IND_INT_EN		BIT(28)
#define B_AX_TXDMA_CH11_INT_EN		BIT(12)
#define B_AX_TXDMA_CH10_INT_EN		BIT(11)

#define R_AX_PCIE_HISR10	0x13B4
#define B_AX_HC10ISR_IND_INT		BIT(28)
#define B_AX_TXDMA_CH11_INT		BIT(12)
#define B_AX_TXDMA_CH10_INT		BIT(11)

#define R_AX_PCIE_HIMR00_V1 0x30B0
#define B_AX_HCI_AXIDMA_INT_EN BIT(29)
#define B_AX_HC00ISR_IND_INT_EN_V1 BIT(28)
#define B_AX_HD1ISR_IND_INT_EN_V1 BIT(27)
#define B_AX_HD0ISR_IND_INT_EN_V1 BIT(26)
#define B_AX_HS1ISR_IND_INT_EN BIT(25)
#define B_AX_PCIE_DBG_STE_INT_EN BIT(13)

#define R_AX_PCIE_HISR00_V1 0x30B4
#define B_AX_HCI_AXIDMA_INT BIT(29)
#define B_AX_HC00ISR_IND_INT_V1 BIT(28)
#define B_AX_HD1ISR_IND_INT_V1 BIT(27)
#define B_AX_HD0ISR_IND_INT_V1 BIT(26)
#define B_AX_HS1ISR_IND_INT BIT(25)
#define B_AX_PCIE_DBG_STE_INT BIT(13)

/* TX/RX */
#define R_AX_DRV_FW_HSK_0	0x01B0
#define R_AX_DRV_FW_HSK_1	0x01B4
#define R_AX_DRV_FW_HSK_2	0x01B8
#define R_AX_DRV_FW_HSK_3	0x01BC
#define R_AX_DRV_FW_HSK_4	0x01C0
#define R_AX_DRV_FW_HSK_5	0x01C4
#define R_AX_DRV_FW_HSK_6	0x01C8
#define R_AX_DRV_FW_HSK_7	0x01CC

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
#define B_AX_TX_STOP1_MASK		(B_AX_STOP_ACH0 | B_AX_STOP_ACH1 | \
					 B_AX_STOP_ACH2 | B_AX_STOP_ACH3 | \
					 B_AX_STOP_ACH4 | B_AX_STOP_ACH5 | \
					 B_AX_STOP_ACH6 | B_AX_STOP_ACH7 | \
					 B_AX_STOP_CH8 | B_AX_STOP_CH9 | \
					 B_AX_STOP_CH12)
#define B_AX_TX_STOP1_MASK_V1		(B_AX_STOP_ACH0 | B_AX_STOP_ACH1 | \
					 B_AX_STOP_ACH2 | B_AX_STOP_ACH3 | \
					 B_AX_STOP_CH8 | B_AX_STOP_CH9 | \
					 B_AX_STOP_CH12)

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
#define B_AX_CH12_BUSY			BIT(18)
#define B_AX_CH9_BUSY			BIT(17)
#define B_AX_CH8_BUSY			BIT(16)
#define B_AX_ACH7_BUSY			BIT(15)
#define B_AX_ACH6_BUSY			BIT(14)
#define B_AX_ACH5_BUSY			BIT(13)
#define B_AX_ACH4_BUSY			BIT(12)
#define B_AX_ACH3_BUSY			BIT(11)
#define B_AX_ACH2_BUSY			BIT(10)
#define B_AX_ACH1_BUSY			BIT(9)
#define B_AX_ACH0_BUSY			BIT(8)
#define B_AX_RPQ_BUSY			BIT(1)
#define B_AX_RXQ_BUSY			BIT(0)
#define DMA_BUSY1_CHECK		(B_AX_ACH0_BUSY | B_AX_ACH1_BUSY | B_AX_ACH2_BUSY | \
				 B_AX_ACH3_BUSY | B_AX_ACH4_BUSY | B_AX_ACH5_BUSY | \
				 B_AX_ACH6_BUSY | B_AX_ACH7_BUSY | B_AX_CH8_BUSY | \
				 B_AX_CH9_BUSY | B_AX_CH12_BUSY)
#define DMA_BUSY1_CHECK_V1	(B_AX_ACH0_BUSY | B_AX_ACH1_BUSY | B_AX_ACH2_BUSY | \
				 B_AX_ACH3_BUSY | B_AX_CH8_BUSY | B_AX_CH9_BUSY | \
				 B_AX_CH12_BUSY)

#define R_AX_PCIE_DMA_BUSY2	0x131C
#define B_AX_CH11_BUSY			BIT(1)
#define B_AX_CH10_BUSY			BIT(0)

/* Configure */
#define R_AX_PCIE_INIT_CFG2		0x1004
#define B_AX_WD_ITVL_IDLE		GENMASK(27, 24)
#define B_AX_WD_ITVL_ACT		GENMASK(19, 16)
#define B_AX_PCIE_RX_APPLEN_MASK	GENMASK(13, 0)

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

#define R_AX_TXBD_RWPTR_CLR2_V1		0x11C4
#define B_AX_CLR_CH11_IDX		BIT(1)
#define B_AX_CLR_CH10_IDX		BIT(0)

#define R_AX_LBC_WATCHDOG		0x11D8
#define B_AX_LBC_TIMER			GENMASK(7, 4)
#define B_AX_LBC_FLAG			BIT(1)
#define B_AX_LBC_EN			BIT(0)

#define R_AX_RXBD_RWPTR_CLR_V1		0x1200
#define B_AX_CLR_RPQ_IDX		BIT(1)
#define B_AX_CLR_RXQ_IDX		BIT(0)

#define R_AX_HAXI_EXP_CTRL		0x1204
#define B_AX_MAX_TAG_NUM_V1_MASK	GENMASK(2, 0)

#define R_AX_PCIE_EXP_CTRL		0x13F0
#define B_AX_EN_CHKDSC_NO_RX_STUCK	BIT(20)
#define B_AX_MAX_TAG_NUM		GENMASK(18, 16)
#define B_AX_SIC_EN_FORCE_CLKREQ	BIT(4)

#define R_AX_PCIE_RX_PREF_ADV		0x13F4
#define B_AX_RXDMA_PREF_ADV_EN		BIT(0)

#define R_AX_PCIE_HRPWM_V1		0x30C0
#define R_AX_PCIE_CRPWM			0x30C4

#define RTW89_PCI_TXBD_NUM_MAX		256
#define RTW89_PCI_RXBD_NUM_MAX		256
#define RTW89_PCI_TXWD_NUM_MAX		512
#define RTW89_PCI_TXWD_PAGE_SIZE	128
#define RTW89_PCI_ADDRINFO_MAX		4
#define RTW89_PCI_RX_BUF_SIZE		11460

#define RTW89_PCI_POLL_BDRAM_RST_CNT	100
#define RTW89_PCI_MULTITAG		8

/* PCIE CFG register */
#define RTW89_PCIE_L1_STS_V1		0x80
#define RTW89_BCFG_LINK_SPEED_MASK	GENMASK(19, 16)
#define RTW89_PCIE_GEN1_SPEED		0x01
#define RTW89_PCIE_GEN2_SPEED		0x02
#define RTW89_PCIE_PHY_RATE		0x82
#define RTW89_PCIE_PHY_RATE_MASK	GENMASK(1, 0)
#define RTW89_PCIE_L1SS_STS_V1		0x0168
#define RTW89_PCIE_BIT_ASPM_L11		BIT(3)
#define RTW89_PCIE_BIT_ASPM_L12		BIT(2)
#define RTW89_PCIE_BIT_PCI_L11		BIT(1)
#define RTW89_PCIE_BIT_PCI_L12		BIT(0)
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

#define INTF_INTGRA_MINREF_V1	90
#define INTF_INTGRA_HOSTREF_V1	100

enum rtw89_pcie_phy {
	PCIE_PHY_GEN1,
	PCIE_PHY_GEN2,
	PCIE_PHY_GEN1_UNDEFINE = 0x7F,
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

enum mac_ax_bd_trunc_mode {
	MAC_AX_BD_NORM,
	MAC_AX_BD_TRUNC,
	MAC_AX_BD_DEF = 0xFE
};

enum mac_ax_rxbd_mode {
	MAC_AX_RXBD_PKT,
	MAC_AX_RXBD_SEP,
	MAC_AX_RXBD_DEF = 0xFE
};

enum mac_ax_tag_mode {
	MAC_AX_TAG_SGL,
	MAC_AX_TAG_MULTI,
	MAC_AX_TAG_DEF = 0xFE
};

enum mac_ax_tx_burst {
	MAC_AX_TX_BURST_16B = 0,
	MAC_AX_TX_BURST_32B = 1,
	MAC_AX_TX_BURST_64B = 2,
	MAC_AX_TX_BURST_V1_64B = 0,
	MAC_AX_TX_BURST_128B = 3,
	MAC_AX_TX_BURST_V1_128B = 1,
	MAC_AX_TX_BURST_256B = 4,
	MAC_AX_TX_BURST_V1_256B = 2,
	MAC_AX_TX_BURST_512B = 5,
	MAC_AX_TX_BURST_1024B = 6,
	MAC_AX_TX_BURST_2048B = 7,
	MAC_AX_TX_BURST_DEF = 0xFE
};

enum mac_ax_rx_burst {
	MAC_AX_RX_BURST_16B = 0,
	MAC_AX_RX_BURST_32B = 1,
	MAC_AX_RX_BURST_64B = 2,
	MAC_AX_RX_BURST_V1_64B = 0,
	MAC_AX_RX_BURST_128B = 3,
	MAC_AX_RX_BURST_V1_128B = 1,
	MAC_AX_RX_BURST_V1_256B = 0,
	MAC_AX_RX_BURST_DEF = 0xFE
};

enum mac_ax_wd_dma_intvl {
	MAC_AX_WD_DMA_INTVL_0S,
	MAC_AX_WD_DMA_INTVL_256NS,
	MAC_AX_WD_DMA_INTVL_512NS,
	MAC_AX_WD_DMA_INTVL_768NS,
	MAC_AX_WD_DMA_INTVL_1US,
	MAC_AX_WD_DMA_INTVL_1_5US,
	MAC_AX_WD_DMA_INTVL_2US,
	MAC_AX_WD_DMA_INTVL_4US,
	MAC_AX_WD_DMA_INTVL_8US,
	MAC_AX_WD_DMA_INTVL_16US,
	MAC_AX_WD_DMA_INTVL_DEF = 0xFE
};

enum mac_ax_multi_tag_num {
	MAC_AX_TAG_NUM_1,
	MAC_AX_TAG_NUM_2,
	MAC_AX_TAG_NUM_3,
	MAC_AX_TAG_NUM_4,
	MAC_AX_TAG_NUM_5,
	MAC_AX_TAG_NUM_6,
	MAC_AX_TAG_NUM_7,
	MAC_AX_TAG_NUM_8,
	MAC_AX_TAG_NUM_DEF = 0xFE
};

enum mac_ax_lbc_tmr {
	MAC_AX_LBC_TMR_8US = 0,
	MAC_AX_LBC_TMR_16US,
	MAC_AX_LBC_TMR_32US,
	MAC_AX_LBC_TMR_64US,
	MAC_AX_LBC_TMR_128US,
	MAC_AX_LBC_TMR_256US,
	MAC_AX_LBC_TMR_512US,
	MAC_AX_LBC_TMR_1MS,
	MAC_AX_LBC_TMR_2MS,
	MAC_AX_LBC_TMR_4MS,
	MAC_AX_LBC_TMR_8MS,
	MAC_AX_LBC_TMR_DEF = 0xFE
};

enum mac_ax_pcie_func_ctrl {
	MAC_AX_PCIE_DISABLE = 0,
	MAC_AX_PCIE_ENABLE = 1,
	MAC_AX_PCIE_DEFAULT = 0xFE,
	MAC_AX_PCIE_IGNORE = 0xFF
};

enum mac_ax_io_rcy_tmr {
	MAC_AX_IO_RCY_ANA_TMR_2MS = 24000,
	MAC_AX_IO_RCY_ANA_TMR_4MS = 48000,
	MAC_AX_IO_RCY_ANA_TMR_6MS = 72000,
	MAC_AX_IO_RCY_ANA_TMR_DEF = 0xFE
};

enum rtw89_pci_intr_mask_cfg {
	RTW89_PCI_INTR_MASK_RESET,
	RTW89_PCI_INTR_MASK_NORMAL,
	RTW89_PCI_INTR_MASK_LOW_POWER,
	RTW89_PCI_INTR_MASK_RECOVERY_START,
	RTW89_PCI_INTR_MASK_RECOVERY_COMPLETE,
};

struct rtw89_pci_isrs;
struct rtw89_pci;

struct rtw89_pci_bd_idx_addr {
	u32 tx_bd_addrs[RTW89_TXCH_NUM];
	u32 rx_bd_addrs[RTW89_RXCH_NUM];
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

struct rtw89_pci_bd_ram {
	u8 start_idx;
	u8 max_num;
	u8 min_num;
};

struct rtw89_pci_info {
	enum mac_ax_bd_trunc_mode txbd_trunc_mode;
	enum mac_ax_bd_trunc_mode rxbd_trunc_mode;
	enum mac_ax_rxbd_mode rxbd_mode;
	enum mac_ax_tag_mode tag_mode;
	enum mac_ax_tx_burst tx_burst;
	enum mac_ax_rx_burst rx_burst;
	enum mac_ax_wd_dma_intvl wd_dma_idle_intvl;
	enum mac_ax_wd_dma_intvl wd_dma_act_intvl;
	enum mac_ax_multi_tag_num multi_tag_num;
	enum mac_ax_pcie_func_ctrl lbc_en;
	enum mac_ax_lbc_tmr lbc_tmr;
	enum mac_ax_pcie_func_ctrl autok_en;
	enum mac_ax_pcie_func_ctrl io_rcy_en;
	enum mac_ax_io_rcy_tmr io_rcy_tmr;

	u32 init_cfg_reg;
	u32 txhci_en_bit;
	u32 rxhci_en_bit;
	u32 rxbd_mode_bit;
	u32 exp_ctrl_reg;
	u32 max_tag_num_mask;
	u32 rxbd_rwptr_clr_reg;
	u32 txbd_rwptr_clr2_reg;
	struct rtw89_reg_def dma_stop1;
	struct rtw89_reg_def dma_stop2;
	struct rtw89_reg_def dma_busy1;
	u32 dma_busy2_reg;
	u32 dma_busy3_reg;

	u32 rpwm_addr;
	u32 cpwm_addr;
	u32 tx_dma_ch_mask;
	const struct rtw89_pci_bd_idx_addr *bd_idx_addr_low_power;
	const struct rtw89_pci_ch_dma_addr_set *dma_addr_set;
	const struct rtw89_pci_bd_ram (*bd_ram_table)[RTW89_TXCH_NUM];

	int (*ltr_set)(struct rtw89_dev *rtwdev, bool en);
	u32 (*fill_txaddr_info)(struct rtw89_dev *rtwdev,
				void *txaddr_info_addr, u32 total_len,
				dma_addr_t dma, u8 *add_info_nr);
	void (*config_intr_mask)(struct rtw89_dev *rtwdev);
	void (*enable_intr)(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci);
	void (*disable_intr)(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci);
	void (*recognize_intrs)(struct rtw89_dev *rtwdev,
				struct rtw89_pci *rtwpci,
				struct rtw89_pci_isrs *isrs);
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

#define RTW89_TXADDR_INFO_NR_V1		10

struct rtw89_pci_tx_addr_info_32_v1 {
	__le16 length_opt;
#define B_PCIADDR_LEN_V1_MASK		GENMASK(10, 0)
#define B_PCIADDR_HIGH_SEL_V1_MASK	GENMASK(14, 11)
#define B_PCIADDR_LS_V1_MASK		BIT(15)
#define TXADDR_INFO_LENTHG_V1_MAX	ALIGN_DOWN(BIT(11) - 1, 4)
	__le16 dma_low_lsb;
	__le16 dma_low_msb;
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
	u32 ind_isrs;
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
	bool low_power;
	bool under_recovery;
	struct rtw89_pci_tx_ring tx_rings[RTW89_TXCH_NUM];
	struct rtw89_pci_rx_ring rx_rings[RTW89_RXCH_NUM];
	struct sk_buff_head h2c_queue;
	struct sk_buff_head h2c_release_queue;
	DECLARE_BITMAP(kick_map, RTW89_TXCH_NUM);

	u32 ind_intrs;
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
extern const struct rtw89_pci_bd_ram rtw89_bd_ram_table_dual[RTW89_TXCH_NUM];
extern const struct rtw89_pci_bd_ram rtw89_bd_ram_table_single[RTW89_TXCH_NUM];

struct pci_device_id;

int rtw89_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void rtw89_pci_remove(struct pci_dev *pdev);
int rtw89_pci_ltr_set(struct rtw89_dev *rtwdev, bool en);
int rtw89_pci_ltr_set_v1(struct rtw89_dev *rtwdev, bool en);
u32 rtw89_pci_fill_txaddr_info(struct rtw89_dev *rtwdev,
			       void *txaddr_info_addr, u32 total_len,
			       dma_addr_t dma, u8 *add_info_nr);
u32 rtw89_pci_fill_txaddr_info_v1(struct rtw89_dev *rtwdev,
				  void *txaddr_info_addr, u32 total_len,
				  dma_addr_t dma, u8 *add_info_nr);
void rtw89_pci_config_intr_mask(struct rtw89_dev *rtwdev);
void rtw89_pci_config_intr_mask_v1(struct rtw89_dev *rtwdev);
void rtw89_pci_enable_intr(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci);
void rtw89_pci_disable_intr(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci);
void rtw89_pci_enable_intr_v1(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci);
void rtw89_pci_disable_intr_v1(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci);
void rtw89_pci_recognize_intrs(struct rtw89_dev *rtwdev,
			       struct rtw89_pci *rtwpci,
			       struct rtw89_pci_isrs *isrs);
void rtw89_pci_recognize_intrs_v1(struct rtw89_dev *rtwdev,
				  struct rtw89_pci *rtwpci,
				  struct rtw89_pci_isrs *isrs);

static inline
u32 rtw89_chip_fill_txaddr_info(struct rtw89_dev *rtwdev,
				void *txaddr_info_addr, u32 total_len,
				dma_addr_t dma, u8 *add_info_nr)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;

	return info->fill_txaddr_info(rtwdev, txaddr_info_addr, total_len,
				      dma, add_info_nr);
}

static inline void rtw89_chip_config_intr_mask(struct rtw89_dev *rtwdev,
					       enum rtw89_pci_intr_mask_cfg cfg)
{
	struct rtw89_pci *rtwpci = (struct rtw89_pci *)rtwdev->priv;
	const struct rtw89_pci_info *info = rtwdev->pci_info;

	switch (cfg) {
	default:
	case RTW89_PCI_INTR_MASK_RESET:
		rtwpci->low_power = false;
		rtwpci->under_recovery = false;
		break;
	case RTW89_PCI_INTR_MASK_NORMAL:
		rtwpci->low_power = false;
		break;
	case RTW89_PCI_INTR_MASK_LOW_POWER:
		rtwpci->low_power = true;
		break;
	case RTW89_PCI_INTR_MASK_RECOVERY_START:
		rtwpci->under_recovery = true;
		break;
	case RTW89_PCI_INTR_MASK_RECOVERY_COMPLETE:
		rtwpci->under_recovery = false;
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_HCI,
		    "Configure PCI interrupt mask mode low_power=%d under_recovery=%d\n",
		    rtwpci->low_power, rtwpci->under_recovery);

	info->config_intr_mask(rtwdev);
}

static inline
void rtw89_chip_enable_intr(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;

	info->enable_intr(rtwdev, rtwpci);
}

static inline
void rtw89_chip_disable_intr(struct rtw89_dev *rtwdev, struct rtw89_pci *rtwpci)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;

	info->disable_intr(rtwdev, rtwpci);
}

static inline
void rtw89_chip_recognize_intrs(struct rtw89_dev *rtwdev,
				struct rtw89_pci *rtwpci,
				struct rtw89_pci_isrs *isrs)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;

	info->recognize_intrs(rtwdev, rtwpci, isrs);
}

#endif
