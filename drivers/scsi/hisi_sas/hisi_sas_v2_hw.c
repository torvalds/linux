/*
 * Copyright (c) 2016 Linaro Ltd.
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v2_hw"

/* global registers need init*/
#define DLVRY_QUEUE_ENABLE		0x0
#define IOST_BASE_ADDR_LO		0x8
#define IOST_BASE_ADDR_HI		0xc
#define ITCT_BASE_ADDR_LO		0x10
#define ITCT_BASE_ADDR_HI		0x14
#define IO_BROKEN_MSG_ADDR_LO		0x18
#define IO_BROKEN_MSG_ADDR_HI		0x1c
#define PHY_CONTEXT			0x20
#define PHY_STATE			0x24
#define PHY_PORT_NUM_MA			0x28
#define PORT_STATE			0x2c
#define PORT_STATE_PHY8_PORT_NUM_OFF	16
#define PORT_STATE_PHY8_PORT_NUM_MSK	(0xf << PORT_STATE_PHY8_PORT_NUM_OFF)
#define PORT_STATE_PHY8_CONN_RATE_OFF	20
#define PORT_STATE_PHY8_CONN_RATE_MSK	(0xf << PORT_STATE_PHY8_CONN_RATE_OFF)
#define PHY_CONN_RATE			0x30
#define HGC_TRANS_TASK_CNT_LIMIT	0x38
#define AXI_AHB_CLK_CFG			0x3c
#define ITCT_CLR			0x44
#define ITCT_CLR_EN_OFF			16
#define ITCT_CLR_EN_MSK			(0x1 << ITCT_CLR_EN_OFF)
#define ITCT_DEV_OFF			0
#define ITCT_DEV_MSK			(0x7ff << ITCT_DEV_OFF)
#define AXI_USER1			0x48
#define AXI_USER2			0x4c
#define IO_SATA_BROKEN_MSG_ADDR_LO	0x58
#define IO_SATA_BROKEN_MSG_ADDR_HI	0x5c
#define SATA_INITI_D2H_STORE_ADDR_LO	0x60
#define SATA_INITI_D2H_STORE_ADDR_HI	0x64
#define HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL	0x84
#define HGC_SAS_TXFAIL_RETRY_CTRL	0x88
#define HGC_GET_ITV_TIME		0x90
#define DEVICE_MSG_WORK_MODE		0x94
#define OPENA_WT_CONTI_TIME		0x9c
#define I_T_NEXUS_LOSS_TIME		0xa0
#define MAX_CON_TIME_LIMIT_TIME		0xa4
#define BUS_INACTIVE_LIMIT_TIME		0xa8
#define REJECT_TO_OPEN_LIMIT_TIME	0xac
#define CFG_AGING_TIME			0xbc
#define HGC_DFX_CFG2			0xc0
#define HGC_IOMB_PROC1_STATUS	0x104
#define CFG_1US_TIMER_TRSH		0xcc
#define HGC_LM_DFX_STATUS2		0x128
#define HGC_LM_DFX_STATUS2_IOSTLIST_OFF		0
#define HGC_LM_DFX_STATUS2_IOSTLIST_MSK	(0xfff << \
					 HGC_LM_DFX_STATUS2_IOSTLIST_OFF)
#define HGC_LM_DFX_STATUS2_ITCTLIST_OFF		12
#define HGC_LM_DFX_STATUS2_ITCTLIST_MSK	(0x7ff << \
					 HGC_LM_DFX_STATUS2_ITCTLIST_OFF)
#define HGC_CQE_ECC_ADDR		0x13c
#define HGC_CQE_ECC_1B_ADDR_OFF	0
#define HGC_CQE_ECC_1B_ADDR_MSK	(0x3f << HGC_CQE_ECC_1B_ADDR_OFF)
#define HGC_CQE_ECC_MB_ADDR_OFF	8
#define HGC_CQE_ECC_MB_ADDR_MSK (0x3f << HGC_CQE_ECC_MB_ADDR_OFF)
#define HGC_IOST_ECC_ADDR		0x140
#define HGC_IOST_ECC_1B_ADDR_OFF	0
#define HGC_IOST_ECC_1B_ADDR_MSK	(0x3ff << HGC_IOST_ECC_1B_ADDR_OFF)
#define HGC_IOST_ECC_MB_ADDR_OFF	16
#define HGC_IOST_ECC_MB_ADDR_MSK	(0x3ff << HGC_IOST_ECC_MB_ADDR_OFF)
#define HGC_DQE_ECC_ADDR		0x144
#define HGC_DQE_ECC_1B_ADDR_OFF	0
#define HGC_DQE_ECC_1B_ADDR_MSK	(0xfff << HGC_DQE_ECC_1B_ADDR_OFF)
#define HGC_DQE_ECC_MB_ADDR_OFF	16
#define HGC_DQE_ECC_MB_ADDR_MSK (0xfff << HGC_DQE_ECC_MB_ADDR_OFF)
#define HGC_INVLD_DQE_INFO		0x148
#define HGC_INVLD_DQE_INFO_FB_CH0_OFF	9
#define HGC_INVLD_DQE_INFO_FB_CH0_MSK	(0x1 << HGC_INVLD_DQE_INFO_FB_CH0_OFF)
#define HGC_INVLD_DQE_INFO_FB_CH3_OFF	18
#define HGC_ITCT_ECC_ADDR		0x150
#define HGC_ITCT_ECC_1B_ADDR_OFF		0
#define HGC_ITCT_ECC_1B_ADDR_MSK		(0x3ff << \
						 HGC_ITCT_ECC_1B_ADDR_OFF)
#define HGC_ITCT_ECC_MB_ADDR_OFF		16
#define HGC_ITCT_ECC_MB_ADDR_MSK		(0x3ff << \
						 HGC_ITCT_ECC_MB_ADDR_OFF)
#define HGC_AXI_FIFO_ERR_INFO	0x154
#define AXI_ERR_INFO_OFF		0
#define AXI_ERR_INFO_MSK		(0xff << AXI_ERR_INFO_OFF)
#define FIFO_ERR_INFO_OFF		8
#define FIFO_ERR_INFO_MSK		(0xff << FIFO_ERR_INFO_OFF)
#define INT_COAL_EN			0x19c
#define OQ_INT_COAL_TIME		0x1a0
#define OQ_INT_COAL_CNT			0x1a4
#define ENT_INT_COAL_TIME		0x1a8
#define ENT_INT_COAL_CNT		0x1ac
#define OQ_INT_SRC			0x1b0
#define OQ_INT_SRC_MSK			0x1b4
#define ENT_INT_SRC1			0x1b8
#define ENT_INT_SRC1_D2H_FIS_CH0_OFF	0
#define ENT_INT_SRC1_D2H_FIS_CH0_MSK	(0x1 << ENT_INT_SRC1_D2H_FIS_CH0_OFF)
#define ENT_INT_SRC1_D2H_FIS_CH1_OFF	8
#define ENT_INT_SRC1_D2H_FIS_CH1_MSK	(0x1 << ENT_INT_SRC1_D2H_FIS_CH1_OFF)
#define ENT_INT_SRC2			0x1bc
#define ENT_INT_SRC3			0x1c0
#define ENT_INT_SRC3_WP_DEPTH_OFF		8
#define ENT_INT_SRC3_IPTT_SLOT_NOMATCH_OFF	9
#define ENT_INT_SRC3_RP_DEPTH_OFF		10
#define ENT_INT_SRC3_AXI_OFF			11
#define ENT_INT_SRC3_FIFO_OFF			12
#define ENT_INT_SRC3_LM_OFF				14
#define ENT_INT_SRC3_ITC_INT_OFF	15
#define ENT_INT_SRC3_ITC_INT_MSK	(0x1 << ENT_INT_SRC3_ITC_INT_OFF)
#define ENT_INT_SRC3_ABT_OFF		16
#define ENT_INT_SRC_MSK1		0x1c4
#define ENT_INT_SRC_MSK2		0x1c8
#define ENT_INT_SRC_MSK3		0x1cc
#define ENT_INT_SRC_MSK3_ENT95_MSK_OFF	31
#define ENT_INT_SRC_MSK3_ENT95_MSK_MSK	(0x1 << ENT_INT_SRC_MSK3_ENT95_MSK_OFF)
#define SAS_ECC_INTR			0x1e8
#define SAS_ECC_INTR_DQE_ECC_1B_OFF		0
#define SAS_ECC_INTR_DQE_ECC_MB_OFF		1
#define SAS_ECC_INTR_IOST_ECC_1B_OFF	2
#define SAS_ECC_INTR_IOST_ECC_MB_OFF	3
#define SAS_ECC_INTR_ITCT_ECC_MB_OFF	4
#define SAS_ECC_INTR_ITCT_ECC_1B_OFF	5
#define SAS_ECC_INTR_IOSTLIST_ECC_MB_OFF	6
#define SAS_ECC_INTR_IOSTLIST_ECC_1B_OFF	7
#define SAS_ECC_INTR_ITCTLIST_ECC_1B_OFF	8
#define SAS_ECC_INTR_ITCTLIST_ECC_MB_OFF	9
#define SAS_ECC_INTR_CQE_ECC_1B_OFF		10
#define SAS_ECC_INTR_CQE_ECC_MB_OFF		11
#define SAS_ECC_INTR_NCQ_MEM0_ECC_MB_OFF	12
#define SAS_ECC_INTR_NCQ_MEM0_ECC_1B_OFF	13
#define SAS_ECC_INTR_NCQ_MEM1_ECC_MB_OFF	14
#define SAS_ECC_INTR_NCQ_MEM1_ECC_1B_OFF	15
#define SAS_ECC_INTR_NCQ_MEM2_ECC_MB_OFF	16
#define SAS_ECC_INTR_NCQ_MEM2_ECC_1B_OFF	17
#define SAS_ECC_INTR_NCQ_MEM3_ECC_MB_OFF	18
#define SAS_ECC_INTR_NCQ_MEM3_ECC_1B_OFF	19
#define SAS_ECC_INTR_MSK		0x1ec
#define HGC_ERR_STAT_EN			0x238
#define DLVRY_Q_0_BASE_ADDR_LO		0x260
#define DLVRY_Q_0_BASE_ADDR_HI		0x264
#define DLVRY_Q_0_DEPTH			0x268
#define DLVRY_Q_0_WR_PTR		0x26c
#define DLVRY_Q_0_RD_PTR		0x270
#define HYPER_STREAM_ID_EN_CFG		0xc80
#define OQ0_INT_SRC_MSK			0xc90
#define COMPL_Q_0_BASE_ADDR_LO		0x4e0
#define COMPL_Q_0_BASE_ADDR_HI		0x4e4
#define COMPL_Q_0_DEPTH			0x4e8
#define COMPL_Q_0_WR_PTR		0x4ec
#define COMPL_Q_0_RD_PTR		0x4f0
#define HGC_RXM_DFX_STATUS14	0xae8
#define HGC_RXM_DFX_STATUS14_MEM0_OFF		0
#define HGC_RXM_DFX_STATUS14_MEM0_MSK		(0x1ff << \
						 HGC_RXM_DFX_STATUS14_MEM0_OFF)
#define HGC_RXM_DFX_STATUS14_MEM1_OFF		9
#define HGC_RXM_DFX_STATUS14_MEM1_MSK		(0x1ff << \
						 HGC_RXM_DFX_STATUS14_MEM1_OFF)
#define HGC_RXM_DFX_STATUS14_MEM2_OFF		18
#define HGC_RXM_DFX_STATUS14_MEM2_MSK		(0x1ff << \
						 HGC_RXM_DFX_STATUS14_MEM2_OFF)
#define HGC_RXM_DFX_STATUS15	0xaec
#define HGC_RXM_DFX_STATUS15_MEM3_OFF		0
#define HGC_RXM_DFX_STATUS15_MEM3_MSK		(0x1ff << \
						 HGC_RXM_DFX_STATUS15_MEM3_OFF)
/* phy registers need init */
#define PORT_BASE			(0x2000)

#define PHY_CFG				(PORT_BASE + 0x0)
#define HARD_PHY_LINKRATE		(PORT_BASE + 0x4)
#define PHY_CFG_ENA_OFF			0
#define PHY_CFG_ENA_MSK			(0x1 << PHY_CFG_ENA_OFF)
#define PHY_CFG_DC_OPT_OFF		2
#define PHY_CFG_DC_OPT_MSK		(0x1 << PHY_CFG_DC_OPT_OFF)
#define PROG_PHY_LINK_RATE		(PORT_BASE + 0x8)
#define PROG_PHY_LINK_RATE_MAX_OFF	0
#define PROG_PHY_LINK_RATE_MAX_MSK	(0xff << PROG_PHY_LINK_RATE_MAX_OFF)
#define PHY_CTRL			(PORT_BASE + 0x14)
#define PHY_CTRL_RESET_OFF		0
#define PHY_CTRL_RESET_MSK		(0x1 << PHY_CTRL_RESET_OFF)
#define SAS_PHY_CTRL			(PORT_BASE + 0x20)
#define SL_CFG				(PORT_BASE + 0x84)
#define PHY_PCN				(PORT_BASE + 0x44)
#define SL_TOUT_CFG			(PORT_BASE + 0x8c)
#define SL_CONTROL			(PORT_BASE + 0x94)
#define SL_CONTROL_NOTIFY_EN_OFF	0
#define SL_CONTROL_NOTIFY_EN_MSK	(0x1 << SL_CONTROL_NOTIFY_EN_OFF)
#define SL_CONTROL_CTA_OFF		17
#define SL_CONTROL_CTA_MSK		(0x1 << SL_CONTROL_CTA_OFF)
#define RX_PRIMS_STATUS			(PORT_BASE + 0x98)
#define RX_BCAST_CHG_OFF		1
#define RX_BCAST_CHG_MSK		(0x1 << RX_BCAST_CHG_OFF)
#define TX_ID_DWORD0			(PORT_BASE + 0x9c)
#define TX_ID_DWORD1			(PORT_BASE + 0xa0)
#define TX_ID_DWORD2			(PORT_BASE + 0xa4)
#define TX_ID_DWORD3			(PORT_BASE + 0xa8)
#define TX_ID_DWORD4			(PORT_BASE + 0xaC)
#define TX_ID_DWORD5			(PORT_BASE + 0xb0)
#define TX_ID_DWORD6			(PORT_BASE + 0xb4)
#define TXID_AUTO			(PORT_BASE + 0xb8)
#define TXID_AUTO_CT3_OFF		1
#define TXID_AUTO_CT3_MSK		(0x1 << TXID_AUTO_CT3_OFF)
#define TXID_AUTO_CTB_OFF		11
#define TXID_AUTO_CTB_MSK		(0x1 << TXID_AUTO_CTB_OFF)
#define TX_HARDRST_OFF			2
#define TX_HARDRST_MSK			(0x1 << TX_HARDRST_OFF)
#define RX_IDAF_DWORD0			(PORT_BASE + 0xc4)
#define RX_IDAF_DWORD1			(PORT_BASE + 0xc8)
#define RX_IDAF_DWORD2			(PORT_BASE + 0xcc)
#define RX_IDAF_DWORD3			(PORT_BASE + 0xd0)
#define RX_IDAF_DWORD4			(PORT_BASE + 0xd4)
#define RX_IDAF_DWORD5			(PORT_BASE + 0xd8)
#define RX_IDAF_DWORD6			(PORT_BASE + 0xdc)
#define RXOP_CHECK_CFG_H		(PORT_BASE + 0xfc)
#define CON_CONTROL			(PORT_BASE + 0x118)
#define CON_CONTROL_CFG_OPEN_ACC_STP_OFF	0
#define CON_CONTROL_CFG_OPEN_ACC_STP_MSK	\
		(0x01 << CON_CONTROL_CFG_OPEN_ACC_STP_OFF)
#define DONE_RECEIVED_TIME		(PORT_BASE + 0x11c)
#define CHL_INT0			(PORT_BASE + 0x1b4)
#define CHL_INT0_HOTPLUG_TOUT_OFF	0
#define CHL_INT0_HOTPLUG_TOUT_MSK	(0x1 << CHL_INT0_HOTPLUG_TOUT_OFF)
#define CHL_INT0_SL_RX_BCST_ACK_OFF	1
#define CHL_INT0_SL_RX_BCST_ACK_MSK	(0x1 << CHL_INT0_SL_RX_BCST_ACK_OFF)
#define CHL_INT0_SL_PHY_ENABLE_OFF	2
#define CHL_INT0_SL_PHY_ENABLE_MSK	(0x1 << CHL_INT0_SL_PHY_ENABLE_OFF)
#define CHL_INT0_NOT_RDY_OFF		4
#define CHL_INT0_NOT_RDY_MSK		(0x1 << CHL_INT0_NOT_RDY_OFF)
#define CHL_INT0_PHY_RDY_OFF		5
#define CHL_INT0_PHY_RDY_MSK		(0x1 << CHL_INT0_PHY_RDY_OFF)
#define CHL_INT1			(PORT_BASE + 0x1b8)
#define CHL_INT1_DMAC_TX_ECC_ERR_OFF	15
#define CHL_INT1_DMAC_TX_ECC_ERR_MSK	(0x1 << CHL_INT1_DMAC_TX_ECC_ERR_OFF)
#define CHL_INT1_DMAC_RX_ECC_ERR_OFF	17
#define CHL_INT1_DMAC_RX_ECC_ERR_MSK	(0x1 << CHL_INT1_DMAC_RX_ECC_ERR_OFF)
#define CHL_INT1_DMAC_TX_AXI_WR_ERR_OFF	19
#define CHL_INT1_DMAC_TX_AXI_RD_ERR_OFF	20
#define CHL_INT1_DMAC_RX_AXI_WR_ERR_OFF	21
#define CHL_INT1_DMAC_RX_AXI_RD_ERR_OFF	22
#define CHL_INT2			(PORT_BASE + 0x1bc)
#define CHL_INT2_SL_IDAF_TOUT_CONF_OFF	0
#define CHL_INT0_MSK			(PORT_BASE + 0x1c0)
#define CHL_INT1_MSK			(PORT_BASE + 0x1c4)
#define CHL_INT2_MSK			(PORT_BASE + 0x1c8)
#define CHL_INT_COAL_EN			(PORT_BASE + 0x1d0)
#define DMA_TX_DFX0				(PORT_BASE + 0x200)
#define DMA_TX_DFX1				(PORT_BASE + 0x204)
#define DMA_TX_DFX1_IPTT_OFF		0
#define DMA_TX_DFX1_IPTT_MSK		(0xffff << DMA_TX_DFX1_IPTT_OFF)
#define DMA_TX_FIFO_DFX0		(PORT_BASE + 0x240)
#define PORT_DFX0				(PORT_BASE + 0x258)
#define LINK_DFX2					(PORT_BASE + 0X264)
#define LINK_DFX2_RCVR_HOLD_STS_OFF	9
#define LINK_DFX2_RCVR_HOLD_STS_MSK	(0x1 << LINK_DFX2_RCVR_HOLD_STS_OFF)
#define LINK_DFX2_SEND_HOLD_STS_OFF	10
#define LINK_DFX2_SEND_HOLD_STS_MSK	(0x1 << LINK_DFX2_SEND_HOLD_STS_OFF)
#define SAS_ERR_CNT4_REG		(PORT_BASE + 0x290)
#define SAS_ERR_CNT6_REG		(PORT_BASE + 0x298)
#define PHY_CTRL_RDY_MSK		(PORT_BASE + 0x2b0)
#define PHYCTRL_NOT_RDY_MSK		(PORT_BASE + 0x2b4)
#define PHYCTRL_DWS_RESET_MSK		(PORT_BASE + 0x2b8)
#define PHYCTRL_PHY_ENA_MSK		(PORT_BASE + 0x2bc)
#define SL_RX_BCAST_CHK_MSK		(PORT_BASE + 0x2c0)
#define PHYCTRL_OOB_RESTART_MSK		(PORT_BASE + 0x2c4)
#define DMA_TX_STATUS			(PORT_BASE + 0x2d0)
#define DMA_TX_STATUS_BUSY_OFF		0
#define DMA_TX_STATUS_BUSY_MSK		(0x1 << DMA_TX_STATUS_BUSY_OFF)
#define DMA_RX_STATUS			(PORT_BASE + 0x2e8)
#define DMA_RX_STATUS_BUSY_OFF		0
#define DMA_RX_STATUS_BUSY_MSK		(0x1 << DMA_RX_STATUS_BUSY_OFF)

#define AXI_CFG				(0x5100)
#define AM_CFG_MAX_TRANS		(0x5010)
#define AM_CFG_SINGLE_PORT_MAX_TRANS	(0x5014)

#define AXI_MASTER_CFG_BASE		(0x5000)
#define AM_CTRL_GLOBAL			(0x0)
#define AM_CURR_TRANS_RETURN	(0x150)

/* HW dma structures */
/* Delivery queue header */
/* dw0 */
#define CMD_HDR_ABORT_FLAG_OFF		0
#define CMD_HDR_ABORT_FLAG_MSK		(0x3 << CMD_HDR_ABORT_FLAG_OFF)
#define CMD_HDR_ABORT_DEVICE_TYPE_OFF	2
#define CMD_HDR_ABORT_DEVICE_TYPE_MSK	(0x1 << CMD_HDR_ABORT_DEVICE_TYPE_OFF)
#define CMD_HDR_RESP_REPORT_OFF		5
#define CMD_HDR_RESP_REPORT_MSK		(0x1 << CMD_HDR_RESP_REPORT_OFF)
#define CMD_HDR_TLR_CTRL_OFF		6
#define CMD_HDR_TLR_CTRL_MSK		(0x3 << CMD_HDR_TLR_CTRL_OFF)
#define CMD_HDR_PORT_OFF		18
#define CMD_HDR_PORT_MSK		(0xf << CMD_HDR_PORT_OFF)
#define CMD_HDR_PRIORITY_OFF		27
#define CMD_HDR_PRIORITY_MSK		(0x1 << CMD_HDR_PRIORITY_OFF)
#define CMD_HDR_CMD_OFF			29
#define CMD_HDR_CMD_MSK			(0x7 << CMD_HDR_CMD_OFF)
/* dw1 */
#define CMD_HDR_DIR_OFF			5
#define CMD_HDR_DIR_MSK			(0x3 << CMD_HDR_DIR_OFF)
#define CMD_HDR_RESET_OFF		7
#define CMD_HDR_RESET_MSK		(0x1 << CMD_HDR_RESET_OFF)
#define CMD_HDR_VDTL_OFF		10
#define CMD_HDR_VDTL_MSK		(0x1 << CMD_HDR_VDTL_OFF)
#define CMD_HDR_FRAME_TYPE_OFF		11
#define CMD_HDR_FRAME_TYPE_MSK		(0x1f << CMD_HDR_FRAME_TYPE_OFF)
#define CMD_HDR_DEV_ID_OFF		16
#define CMD_HDR_DEV_ID_MSK		(0xffff << CMD_HDR_DEV_ID_OFF)
/* dw2 */
#define CMD_HDR_CFL_OFF			0
#define CMD_HDR_CFL_MSK			(0x1ff << CMD_HDR_CFL_OFF)
#define CMD_HDR_NCQ_TAG_OFF		10
#define CMD_HDR_NCQ_TAG_MSK		(0x1f << CMD_HDR_NCQ_TAG_OFF)
#define CMD_HDR_MRFL_OFF		15
#define CMD_HDR_MRFL_MSK		(0x1ff << CMD_HDR_MRFL_OFF)
#define CMD_HDR_SG_MOD_OFF		24
#define CMD_HDR_SG_MOD_MSK		(0x3 << CMD_HDR_SG_MOD_OFF)
#define CMD_HDR_FIRST_BURST_OFF		26
#define CMD_HDR_FIRST_BURST_MSK		(0x1 << CMD_HDR_SG_MOD_OFF)
/* dw3 */
#define CMD_HDR_IPTT_OFF		0
#define CMD_HDR_IPTT_MSK		(0xffff << CMD_HDR_IPTT_OFF)
/* dw6 */
#define CMD_HDR_DIF_SGL_LEN_OFF		0
#define CMD_HDR_DIF_SGL_LEN_MSK		(0xffff << CMD_HDR_DIF_SGL_LEN_OFF)
#define CMD_HDR_DATA_SGL_LEN_OFF	16
#define CMD_HDR_DATA_SGL_LEN_MSK	(0xffff << CMD_HDR_DATA_SGL_LEN_OFF)
#define CMD_HDR_ABORT_IPTT_OFF		16
#define CMD_HDR_ABORT_IPTT_MSK		(0xffff << CMD_HDR_ABORT_IPTT_OFF)

/* Completion header */
/* dw0 */
#define CMPLT_HDR_ERR_PHASE_OFF	2
#define CMPLT_HDR_ERR_PHASE_MSK	(0xff << CMPLT_HDR_ERR_PHASE_OFF)
#define CMPLT_HDR_RSPNS_XFRD_OFF	10
#define CMPLT_HDR_RSPNS_XFRD_MSK	(0x1 << CMPLT_HDR_RSPNS_XFRD_OFF)
#define CMPLT_HDR_ERX_OFF		12
#define CMPLT_HDR_ERX_MSK		(0x1 << CMPLT_HDR_ERX_OFF)
#define CMPLT_HDR_ABORT_STAT_OFF	13
#define CMPLT_HDR_ABORT_STAT_MSK	(0x7 << CMPLT_HDR_ABORT_STAT_OFF)
/* abort_stat */
#define STAT_IO_NOT_VALID		0x1
#define STAT_IO_NO_DEVICE		0x2
#define STAT_IO_COMPLETE		0x3
#define STAT_IO_ABORTED			0x4
/* dw1 */
#define CMPLT_HDR_IPTT_OFF		0
#define CMPLT_HDR_IPTT_MSK		(0xffff << CMPLT_HDR_IPTT_OFF)
#define CMPLT_HDR_DEV_ID_OFF		16
#define CMPLT_HDR_DEV_ID_MSK		(0xffff << CMPLT_HDR_DEV_ID_OFF)

/* ITCT header */
/* qw0 */
#define ITCT_HDR_DEV_TYPE_OFF		0
#define ITCT_HDR_DEV_TYPE_MSK		(0x3 << ITCT_HDR_DEV_TYPE_OFF)
#define ITCT_HDR_VALID_OFF		2
#define ITCT_HDR_VALID_MSK		(0x1 << ITCT_HDR_VALID_OFF)
#define ITCT_HDR_MCR_OFF		5
#define ITCT_HDR_MCR_MSK		(0xf << ITCT_HDR_MCR_OFF)
#define ITCT_HDR_VLN_OFF		9
#define ITCT_HDR_VLN_MSK		(0xf << ITCT_HDR_VLN_OFF)
#define ITCT_HDR_SMP_TIMEOUT_OFF	16
#define ITCT_HDR_SMP_TIMEOUT_8US	1
#define ITCT_HDR_SMP_TIMEOUT		(ITCT_HDR_SMP_TIMEOUT_8US * \
					 250) /* 2ms */
#define ITCT_HDR_AWT_CONTINUE_OFF	25
#define ITCT_HDR_PORT_ID_OFF		28
#define ITCT_HDR_PORT_ID_MSK		(0xf << ITCT_HDR_PORT_ID_OFF)
/* qw2 */
#define ITCT_HDR_INLT_OFF		0
#define ITCT_HDR_INLT_MSK		(0xffffULL << ITCT_HDR_INLT_OFF)
#define ITCT_HDR_BITLT_OFF		16
#define ITCT_HDR_BITLT_MSK		(0xffffULL << ITCT_HDR_BITLT_OFF)
#define ITCT_HDR_MCTLT_OFF		32
#define ITCT_HDR_MCTLT_MSK		(0xffffULL << ITCT_HDR_MCTLT_OFF)
#define ITCT_HDR_RTOLT_OFF		48
#define ITCT_HDR_RTOLT_MSK		(0xffffULL << ITCT_HDR_RTOLT_OFF)

#define HISI_SAS_FATAL_INT_NR	2

struct hisi_sas_complete_v2_hdr {
	__le32 dw0;
	__le32 dw1;
	__le32 act;
	__le32 dw3;
};

struct hisi_sas_err_record_v2 {
	/* dw0 */
	__le32 trans_tx_fail_type;

	/* dw1 */
	__le32 trans_rx_fail_type;

	/* dw2 */
	__le16 dma_tx_err_type;
	__le16 sipc_rx_err_type;

	/* dw3 */
	__le32 dma_rx_err_type;
};

struct signal_attenuation_s {
	u32 de_emphasis;
	u32 preshoot;
	u32 boost;
};

struct sig_atten_lu_s {
	const struct signal_attenuation_s *att;
	u32 sas_phy_ctrl;
};

static const struct hisi_sas_hw_error one_bit_ecc_errors[] = {
	{
		.irq_msk = BIT(SAS_ECC_INTR_DQE_ECC_1B_OFF),
		.msk = HGC_DQE_ECC_1B_ADDR_MSK,
		.shift = HGC_DQE_ECC_1B_ADDR_OFF,
		.msg = "hgc_dqe_acc1b_intr found: Ram address is 0x%08X\n",
		.reg = HGC_DQE_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_IOST_ECC_1B_OFF),
		.msk = HGC_IOST_ECC_1B_ADDR_MSK,
		.shift = HGC_IOST_ECC_1B_ADDR_OFF,
		.msg = "hgc_iost_acc1b_intr found: Ram address is 0x%08X\n",
		.reg = HGC_IOST_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_ITCT_ECC_1B_OFF),
		.msk = HGC_ITCT_ECC_1B_ADDR_MSK,
		.shift = HGC_ITCT_ECC_1B_ADDR_OFF,
		.msg = "hgc_itct_acc1b_intr found: am address is 0x%08X\n",
		.reg = HGC_ITCT_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_IOSTLIST_ECC_1B_OFF),
		.msk = HGC_LM_DFX_STATUS2_IOSTLIST_MSK,
		.shift = HGC_LM_DFX_STATUS2_IOSTLIST_OFF,
		.msg = "hgc_iostl_acc1b_intr found: memory address is 0x%08X\n",
		.reg = HGC_LM_DFX_STATUS2,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_ITCTLIST_ECC_1B_OFF),
		.msk = HGC_LM_DFX_STATUS2_ITCTLIST_MSK,
		.shift = HGC_LM_DFX_STATUS2_ITCTLIST_OFF,
		.msg = "hgc_itctl_acc1b_intr found: memory address is 0x%08X\n",
		.reg = HGC_LM_DFX_STATUS2,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_CQE_ECC_1B_OFF),
		.msk = HGC_CQE_ECC_1B_ADDR_MSK,
		.shift = HGC_CQE_ECC_1B_ADDR_OFF,
		.msg = "hgc_cqe_acc1b_intr found: Ram address is 0x%08X\n",
		.reg = HGC_CQE_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM0_ECC_1B_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM0_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM0_OFF,
		.msg = "rxm_mem0_acc1b_intr found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM1_ECC_1B_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM1_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM1_OFF,
		.msg = "rxm_mem1_acc1b_intr found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM2_ECC_1B_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM2_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM2_OFF,
		.msg = "rxm_mem2_acc1b_intr found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM3_ECC_1B_OFF),
		.msk = HGC_RXM_DFX_STATUS15_MEM3_MSK,
		.shift = HGC_RXM_DFX_STATUS15_MEM3_OFF,
		.msg = "rxm_mem3_acc1b_intr found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS15,
	},
};

static const struct hisi_sas_hw_error multi_bit_ecc_errors[] = {
	{
		.irq_msk = BIT(SAS_ECC_INTR_DQE_ECC_MB_OFF),
		.msk = HGC_DQE_ECC_MB_ADDR_MSK,
		.shift = HGC_DQE_ECC_MB_ADDR_OFF,
		.msg = "hgc_dqe_accbad_intr (0x%x) found: Ram address is 0x%08X\n",
		.reg = HGC_DQE_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_IOST_ECC_MB_OFF),
		.msk = HGC_IOST_ECC_MB_ADDR_MSK,
		.shift = HGC_IOST_ECC_MB_ADDR_OFF,
		.msg = "hgc_iost_accbad_intr (0x%x) found: Ram address is 0x%08X\n",
		.reg = HGC_IOST_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_ITCT_ECC_MB_OFF),
		.msk = HGC_ITCT_ECC_MB_ADDR_MSK,
		.shift = HGC_ITCT_ECC_MB_ADDR_OFF,
		.msg = "hgc_itct_accbad_intr (0x%x) found: Ram address is 0x%08X\n",
		.reg = HGC_ITCT_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_IOSTLIST_ECC_MB_OFF),
		.msk = HGC_LM_DFX_STATUS2_IOSTLIST_MSK,
		.shift = HGC_LM_DFX_STATUS2_IOSTLIST_OFF,
		.msg = "hgc_iostl_accbad_intr (0x%x) found: memory address is 0x%08X\n",
		.reg = HGC_LM_DFX_STATUS2,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_ITCTLIST_ECC_MB_OFF),
		.msk = HGC_LM_DFX_STATUS2_ITCTLIST_MSK,
		.shift = HGC_LM_DFX_STATUS2_ITCTLIST_OFF,
		.msg = "hgc_itctl_accbad_intr (0x%x) found: memory address is 0x%08X\n",
		.reg = HGC_LM_DFX_STATUS2,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_CQE_ECC_MB_OFF),
		.msk = HGC_CQE_ECC_MB_ADDR_MSK,
		.shift = HGC_CQE_ECC_MB_ADDR_OFF,
		.msg = "hgc_cqe_accbad_intr (0x%x) found: Ram address is 0x%08X\n",
		.reg = HGC_CQE_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM0_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM0_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM0_OFF,
		.msg = "rxm_mem0_accbad_intr (0x%x) found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM1_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM1_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM1_OFF,
		.msg = "rxm_mem1_accbad_intr (0x%x) found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM2_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM2_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM2_OFF,
		.msg = "rxm_mem2_accbad_intr (0x%x) found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM3_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS15_MEM3_MSK,
		.shift = HGC_RXM_DFX_STATUS15_MEM3_OFF,
		.msg = "rxm_mem3_accbad_intr (0x%x) found: memory address is 0x%08X\n",
		.reg = HGC_RXM_DFX_STATUS15,
	},
};

enum {
	HISI_SAS_PHY_PHY_UPDOWN,
	HISI_SAS_PHY_CHNL_INT,
	HISI_SAS_PHY_INT_NR
};

enum {
	TRANS_TX_FAIL_BASE = 0x0, /* dw0 */
	TRANS_RX_FAIL_BASE = 0x20, /* dw1 */
	DMA_TX_ERR_BASE = 0x40, /* dw2 bit 15-0 */
	SIPC_RX_ERR_BASE = 0x50, /* dw2 bit 31-16*/
	DMA_RX_ERR_BASE = 0x60, /* dw3 */

	/* trans tx*/
	TRANS_TX_OPEN_FAIL_WITH_IT_NEXUS_LOSS = TRANS_TX_FAIL_BASE, /* 0x0 */
	TRANS_TX_ERR_PHY_NOT_ENABLE, /* 0x1 */
	TRANS_TX_OPEN_CNX_ERR_WRONG_DESTINATION, /* 0x2 */
	TRANS_TX_OPEN_CNX_ERR_ZONE_VIOLATION, /* 0x3 */
	TRANS_TX_OPEN_CNX_ERR_BY_OTHER, /* 0x4 */
	RESERVED0, /* 0x5 */
	TRANS_TX_OPEN_CNX_ERR_AIP_TIMEOUT, /* 0x6 */
	TRANS_TX_OPEN_CNX_ERR_STP_RESOURCES_BUSY, /* 0x7 */
	TRANS_TX_OPEN_CNX_ERR_PROTOCOL_NOT_SUPPORTED, /* 0x8 */
	TRANS_TX_OPEN_CNX_ERR_CONNECTION_RATE_NOT_SUPPORTED, /* 0x9 */
	TRANS_TX_OPEN_CNX_ERR_BAD_DESTINATION, /* 0xa */
	TRANS_TX_OPEN_CNX_ERR_BREAK_RCVD, /* 0xb */
	TRANS_TX_OPEN_CNX_ERR_LOW_PHY_POWER, /* 0xc */
	TRANS_TX_OPEN_CNX_ERR_PATHWAY_BLOCKED, /* 0xd */
	TRANS_TX_OPEN_CNX_ERR_OPEN_TIMEOUT, /* 0xe */
	TRANS_TX_OPEN_CNX_ERR_NO_DESTINATION, /* 0xf */
	TRANS_TX_OPEN_RETRY_ERR_THRESHOLD_REACHED, /* 0x10 */
	TRANS_TX_ERR_FRAME_TXED, /* 0x11 */
	TRANS_TX_ERR_WITH_BREAK_TIMEOUT, /* 0x12 */
	TRANS_TX_ERR_WITH_BREAK_REQUEST, /* 0x13 */
	TRANS_TX_ERR_WITH_BREAK_RECEVIED, /* 0x14 */
	TRANS_TX_ERR_WITH_CLOSE_TIMEOUT, /* 0x15 */
	TRANS_TX_ERR_WITH_CLOSE_NORMAL, /* 0x16 for ssp*/
	TRANS_TX_ERR_WITH_CLOSE_PHYDISALE, /* 0x17 */
	TRANS_TX_ERR_WITH_CLOSE_DWS_TIMEOUT, /* 0x18 */
	TRANS_TX_ERR_WITH_CLOSE_COMINIT, /* 0x19 */
	TRANS_TX_ERR_WITH_NAK_RECEVIED, /* 0x1a for ssp*/
	TRANS_TX_ERR_WITH_ACK_NAK_TIMEOUT, /* 0x1b for ssp*/
	/*IO_TX_ERR_WITH_R_ERR_RECEVIED, [> 0x1b for sata/stp<] */
	TRANS_TX_ERR_WITH_CREDIT_TIMEOUT, /* 0x1c for ssp */
	/*IO_RX_ERR_WITH_SATA_DEVICE_LOST 0x1c for sata/stp */
	TRANS_TX_ERR_WITH_IPTT_CONFLICT, /* 0x1d for ssp/smp */
	TRANS_TX_ERR_WITH_OPEN_BY_DES_OR_OTHERS, /* 0x1e */
	/*IO_TX_ERR_WITH_SYNC_RXD, [> 0x1e <] for sata/stp */
	TRANS_TX_ERR_WITH_WAIT_RECV_TIMEOUT, /* 0x1f for sata/stp */

	/* trans rx */
	TRANS_RX_ERR_WITH_RXFRAME_CRC_ERR = TRANS_RX_FAIL_BASE, /* 0x20 */
	TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR, /* 0x21 for sata/stp */
	TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM, /* 0x22 for ssp/smp */
	/*IO_ERR_WITH_RXFIS_8B10B_CODE_ERR, [> 0x22 <] for sata/stp */
	TRANS_RX_ERR_WITH_RXFIS_DECODE_ERROR, /* 0x23 for sata/stp */
	TRANS_RX_ERR_WITH_RXFIS_CRC_ERR, /* 0x24 for sata/stp */
	TRANS_RX_ERR_WITH_RXFRAME_LENGTH_OVERRUN, /* 0x25 for smp */
	/*IO_ERR_WITH_RXFIS_TX SYNCP, [> 0x25 <] for sata/stp */
	TRANS_RX_ERR_WITH_RXFIS_RX_SYNCP, /* 0x26 for sata/stp*/
	TRANS_RX_ERR_WITH_LINK_BUF_OVERRUN, /* 0x27 */
	TRANS_RX_ERR_WITH_BREAK_TIMEOUT, /* 0x28 */
	TRANS_RX_ERR_WITH_BREAK_REQUEST, /* 0x29 */
	TRANS_RX_ERR_WITH_BREAK_RECEVIED, /* 0x2a */
	RESERVED1, /* 0x2b */
	TRANS_RX_ERR_WITH_CLOSE_NORMAL, /* 0x2c */
	TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE, /* 0x2d */
	TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT, /* 0x2e */
	TRANS_RX_ERR_WITH_CLOSE_COMINIT, /* 0x2f */
	TRANS_RX_ERR_WITH_DATA_LEN0, /* 0x30 for ssp/smp */
	TRANS_RX_ERR_WITH_BAD_HASH, /* 0x31 for ssp */
	/*IO_RX_ERR_WITH_FIS_TOO_SHORT, [> 0x31 <] for sata/stp */
	TRANS_RX_XRDY_WLEN_ZERO_ERR, /* 0x32 for ssp*/
	/*IO_RX_ERR_WITH_FIS_TOO_LONG, [> 0x32 <] for sata/stp */
	TRANS_RX_SSP_FRM_LEN_ERR, /* 0x33 for ssp */
	/*IO_RX_ERR_WITH_SATA_DEVICE_LOST, [> 0x33 <] for sata */
	RESERVED2, /* 0x34 */
	RESERVED3, /* 0x35 */
	RESERVED4, /* 0x36 */
	RESERVED5, /* 0x37 */
	TRANS_RX_ERR_WITH_BAD_FRM_TYPE, /* 0x38 */
	TRANS_RX_SMP_FRM_LEN_ERR, /* 0x39 */
	TRANS_RX_SMP_RESP_TIMEOUT_ERR, /* 0x3a */
	RESERVED6, /* 0x3b */
	RESERVED7, /* 0x3c */
	RESERVED8, /* 0x3d */
	RESERVED9, /* 0x3e */
	TRANS_RX_R_ERR, /* 0x3f */

	/* dma tx */
	DMA_TX_DIF_CRC_ERR = DMA_TX_ERR_BASE, /* 0x40 */
	DMA_TX_DIF_APP_ERR, /* 0x41 */
	DMA_TX_DIF_RPP_ERR, /* 0x42 */
	DMA_TX_DATA_SGL_OVERFLOW, /* 0x43 */
	DMA_TX_DIF_SGL_OVERFLOW, /* 0x44 */
	DMA_TX_UNEXP_XFER_ERR, /* 0x45 */
	DMA_TX_UNEXP_RETRANS_ERR, /* 0x46 */
	DMA_TX_XFER_LEN_OVERFLOW, /* 0x47 */
	DMA_TX_XFER_OFFSET_ERR, /* 0x48 */
	DMA_TX_RAM_ECC_ERR, /* 0x49 */
	DMA_TX_DIF_LEN_ALIGN_ERR, /* 0x4a */
	DMA_TX_MAX_ERR_CODE,

	/* sipc rx */
	SIPC_RX_FIS_STATUS_ERR_BIT_VLD = SIPC_RX_ERR_BASE, /* 0x50 */
	SIPC_RX_PIO_WRSETUP_STATUS_DRQ_ERR, /* 0x51 */
	SIPC_RX_FIS_STATUS_BSY_BIT_ERR, /* 0x52 */
	SIPC_RX_WRSETUP_LEN_ODD_ERR, /* 0x53 */
	SIPC_RX_WRSETUP_LEN_ZERO_ERR, /* 0x54 */
	SIPC_RX_WRDATA_LEN_NOT_MATCH_ERR, /* 0x55 */
	SIPC_RX_NCQ_WRSETUP_OFFSET_ERR, /* 0x56 */
	SIPC_RX_NCQ_WRSETUP_AUTO_ACTIVE_ERR, /* 0x57 */
	SIPC_RX_SATA_UNEXP_FIS_ERR, /* 0x58 */
	SIPC_RX_WRSETUP_ESTATUS_ERR, /* 0x59 */
	SIPC_RX_DATA_UNDERFLOW_ERR, /* 0x5a */
	SIPC_RX_MAX_ERR_CODE,

	/* dma rx */
	DMA_RX_DIF_CRC_ERR = DMA_RX_ERR_BASE, /* 0x60 */
	DMA_RX_DIF_APP_ERR, /* 0x61 */
	DMA_RX_DIF_RPP_ERR, /* 0x62 */
	DMA_RX_DATA_SGL_OVERFLOW, /* 0x63 */
	DMA_RX_DIF_SGL_OVERFLOW, /* 0x64 */
	DMA_RX_DATA_LEN_OVERFLOW, /* 0x65 */
	DMA_RX_DATA_LEN_UNDERFLOW, /* 0x66 */
	DMA_RX_DATA_OFFSET_ERR, /* 0x67 */
	RESERVED10, /* 0x68 */
	DMA_RX_SATA_FRAME_TYPE_ERR, /* 0x69 */
	DMA_RX_RESP_BUF_OVERFLOW, /* 0x6a */
	DMA_RX_UNEXP_RETRANS_RESP_ERR, /* 0x6b */
	DMA_RX_UNEXP_NORM_RESP_ERR, /* 0x6c */
	DMA_RX_UNEXP_RDFRAME_ERR, /* 0x6d */
	DMA_RX_PIO_DATA_LEN_ERR, /* 0x6e */
	DMA_RX_RDSETUP_STATUS_ERR, /* 0x6f */
	DMA_RX_RDSETUP_STATUS_DRQ_ERR, /* 0x70 */
	DMA_RX_RDSETUP_STATUS_BSY_ERR, /* 0x71 */
	DMA_RX_RDSETUP_LEN_ODD_ERR, /* 0x72 */
	DMA_RX_RDSETUP_LEN_ZERO_ERR, /* 0x73 */
	DMA_RX_RDSETUP_LEN_OVER_ERR, /* 0x74 */
	DMA_RX_RDSETUP_OFFSET_ERR, /* 0x75 */
	DMA_RX_RDSETUP_ACTIVE_ERR, /* 0x76 */
	DMA_RX_RDSETUP_ESTATUS_ERR, /* 0x77 */
	DMA_RX_RAM_ECC_ERR, /* 0x78 */
	DMA_RX_UNKNOWN_FRM_ERR, /* 0x79 */
	DMA_RX_MAX_ERR_CODE,
};

#define HISI_SAS_COMMAND_ENTRIES_V2_HW 4096
#define HISI_MAX_SATA_SUPPORT_V2_HW	(HISI_SAS_COMMAND_ENTRIES_V2_HW/64 - 1)

#define DIR_NO_DATA 0
#define DIR_TO_INI 1
#define DIR_TO_DEVICE 2
#define DIR_RESERVED 3

#define ERR_ON_TX_PHASE(err_phase) (err_phase == 0x2 || \
		err_phase == 0x4 || err_phase == 0x8 ||\
		err_phase == 0x6 || err_phase == 0xa)
#define ERR_ON_RX_PHASE(err_phase) (err_phase == 0x10 || \
		err_phase == 0x20 || err_phase == 0x40)

static void link_timeout_disable_link(struct timer_list *t);

static u32 hisi_sas_read32(struct hisi_hba *hisi_hba, u32 off)
{
	void __iomem *regs = hisi_hba->regs + off;

	return readl(regs);
}

static u32 hisi_sas_read32_relaxed(struct hisi_hba *hisi_hba, u32 off)
{
	void __iomem *regs = hisi_hba->regs + off;

	return readl_relaxed(regs);
}

static void hisi_sas_write32(struct hisi_hba *hisi_hba, u32 off, u32 val)
{
	void __iomem *regs = hisi_hba->regs + off;

	writel(val, regs);
}

static void hisi_sas_phy_write32(struct hisi_hba *hisi_hba, int phy_no,
				 u32 off, u32 val)
{
	void __iomem *regs = hisi_hba->regs + (0x400 * phy_no) + off;

	writel(val, regs);
}

static u32 hisi_sas_phy_read32(struct hisi_hba *hisi_hba,
				      int phy_no, u32 off)
{
	void __iomem *regs = hisi_hba->regs + (0x400 * phy_no) + off;

	return readl(regs);
}

/* This function needs to be protected from pre-emption. */
static int
slot_index_alloc_quirk_v2_hw(struct hisi_hba *hisi_hba, int *slot_idx,
			     struct domain_device *device)
{
	int sata_dev = dev_is_sata(device);
	void *bitmap = hisi_hba->slot_index_tags;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	int sata_idx = sas_dev->sata_idx;
	int start, end;

	if (!sata_dev) {
		/*
		 * STP link SoC bug workaround: index starts from 1.
		 * additionally, we can only allocate odd IPTT(1~4095)
		 * for SAS/SMP device.
		 */
		start = 1;
		end = hisi_hba->slot_index_count;
	} else {
		if (sata_idx >= HISI_MAX_SATA_SUPPORT_V2_HW)
			return -EINVAL;

		/*
		 * For SATA device: allocate even IPTT in this interval
		 * [64*(sata_idx+1), 64*(sata_idx+2)], then each SATA device
		 * own 32 IPTTs. IPTT 0 shall not be used duing to STP link
		 * SoC bug workaround. So we ignore the first 32 even IPTTs.
		 */
		start = 64 * (sata_idx + 1);
		end = 64 * (sata_idx + 2);
	}

	while (1) {
		start = find_next_zero_bit(bitmap,
					hisi_hba->slot_index_count, start);
		if (start >= end)
			return -SAS_QUEUE_FULL;
		/*
		  * SAS IPTT bit0 should be 1, and SATA IPTT bit0 should be 0.
		  */
		if (sata_dev ^ (start & 1))
			break;
		start++;
	}

	set_bit(start, bitmap);
	*slot_idx = start;
	return 0;
}

static bool sata_index_alloc_v2_hw(struct hisi_hba *hisi_hba, int *idx)
{
	unsigned int index;
	struct device *dev = hisi_hba->dev;
	void *bitmap = hisi_hba->sata_dev_bitmap;

	index = find_first_zero_bit(bitmap, HISI_MAX_SATA_SUPPORT_V2_HW);
	if (index >= HISI_MAX_SATA_SUPPORT_V2_HW) {
		dev_warn(dev, "alloc sata index failed, index=%d\n", index);
		return false;
	}

	set_bit(index, bitmap);
	*idx = index;
	return true;
}


static struct
hisi_sas_device *alloc_dev_quirk_v2_hw(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = device->port->ha->lldd_ha;
	struct hisi_sas_device *sas_dev = NULL;
	int i, sata_dev = dev_is_sata(device);
	int sata_idx = -1;
	unsigned long flags;

	spin_lock_irqsave(&hisi_hba->lock, flags);

	if (sata_dev)
		if (!sata_index_alloc_v2_hw(hisi_hba, &sata_idx))
			goto out;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		/*
		 * SATA device id bit0 should be 0
		 */
		if (sata_dev && (i & 1))
			continue;
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
			int queue = i % hisi_hba->queue_count;
			struct hisi_sas_dq *dq = &hisi_hba->dq[queue];

			hisi_hba->devices[i].device_id = i;
			sas_dev = &hisi_hba->devices[i];
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
			sas_dev->dev_type = device->dev_type;
			sas_dev->hisi_hba = hisi_hba;
			sas_dev->sas_device = device;
			sas_dev->sata_idx = sata_idx;
			sas_dev->dq = dq;
			INIT_LIST_HEAD(&hisi_hba->devices[i].list);
			break;
		}
	}

out:
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	return sas_dev;
}

static void config_phy_opt_mode_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg &= ~PHY_CFG_DC_OPT_MSK;
	cfg |= 1 << PHY_CFG_DC_OPT_OFF;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void config_id_frame_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct sas_identify_frame identify_frame;
	u32 *identify_buffer;

	memset(&identify_frame, 0, sizeof(identify_frame));
	identify_frame.dev_type = SAS_END_DEVICE;
	identify_frame.frame_type = 0;
	identify_frame._un1 = 1;
	identify_frame.initiator_bits = SAS_PROTOCOL_ALL;
	identify_frame.target_bits = SAS_PROTOCOL_NONE;
	memcpy(&identify_frame._un4_11[0], hisi_hba->sas_addr, SAS_ADDR_SIZE);
	memcpy(&identify_frame.sas_addr[0], hisi_hba->sas_addr,	SAS_ADDR_SIZE);
	identify_frame.phy_id = phy_no;
	identify_buffer = (u32 *)(&identify_frame);

	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD0,
			__swab32(identify_buffer[0]));
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD1,
			__swab32(identify_buffer[1]));
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD2,
			__swab32(identify_buffer[2]));
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD3,
			__swab32(identify_buffer[3]));
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD4,
			__swab32(identify_buffer[4]));
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD5,
			__swab32(identify_buffer[5]));
}

static void setup_itct_v2_hw(struct hisi_hba *hisi_hba,
			     struct hisi_sas_device *sas_dev)
{
	struct domain_device *device = sas_dev->sas_device;
	struct device *dev = hisi_hba->dev;
	u64 qw0, device_id = sas_dev->device_id;
	struct hisi_sas_itct *itct = &hisi_hba->itct[device_id];
	struct domain_device *parent_dev = device->parent;
	struct asd_sas_port *sas_port = device->port;
	struct hisi_sas_port *port = to_hisi_sas_port(sas_port);

	memset(itct, 0, sizeof(*itct));

	/* qw0 */
	qw0 = 0;
	switch (sas_dev->dev_type) {
	case SAS_END_DEVICE:
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		qw0 = HISI_SAS_DEV_TYPE_SSP << ITCT_HDR_DEV_TYPE_OFF;
		break;
	case SAS_SATA_DEV:
	case SAS_SATA_PENDING:
		if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type))
			qw0 = HISI_SAS_DEV_TYPE_STP << ITCT_HDR_DEV_TYPE_OFF;
		else
			qw0 = HISI_SAS_DEV_TYPE_SATA << ITCT_HDR_DEV_TYPE_OFF;
		break;
	default:
		dev_warn(dev, "setup itct: unsupported dev type (%d)\n",
			 sas_dev->dev_type);
	}

	qw0 |= ((1 << ITCT_HDR_VALID_OFF) |
		(device->linkrate << ITCT_HDR_MCR_OFF) |
		(1 << ITCT_HDR_VLN_OFF) |
		(ITCT_HDR_SMP_TIMEOUT << ITCT_HDR_SMP_TIMEOUT_OFF) |
		(1 << ITCT_HDR_AWT_CONTINUE_OFF) |
		(port->id << ITCT_HDR_PORT_ID_OFF));
	itct->qw0 = cpu_to_le64(qw0);

	/* qw1 */
	memcpy(&itct->sas_addr, device->sas_addr, SAS_ADDR_SIZE);
	itct->sas_addr = __swab64(itct->sas_addr);

	/* qw2 */
	if (!dev_is_sata(device))
		itct->qw2 = cpu_to_le64((5000ULL << ITCT_HDR_INLT_OFF) |
					(0x1ULL << ITCT_HDR_BITLT_OFF) |
					(0x32ULL << ITCT_HDR_MCTLT_OFF) |
					(0x1ULL << ITCT_HDR_RTOLT_OFF));
}

static void clear_itct_v2_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_device *sas_dev)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	u64 dev_id = sas_dev->device_id;
	struct hisi_sas_itct *itct = &hisi_hba->itct[dev_id];
	u32 reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
	int i;

	sas_dev->completion = &completion;

	/* clear the itct interrupt state */
	if (ENT_INT_SRC3_ITC_INT_MSK & reg_val)
		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 ENT_INT_SRC3_ITC_INT_MSK);

	for (i = 0; i < 2; i++) {
		reg_val = ITCT_CLR_EN_MSK | (dev_id & ITCT_DEV_MSK);
		hisi_sas_write32(hisi_hba, ITCT_CLR, reg_val);
		wait_for_completion(sas_dev->completion);

		memset(itct, 0, sizeof(struct hisi_sas_itct));
	}
}

static void free_device_v2_hw(struct hisi_sas_device *sas_dev)
{
	struct hisi_hba *hisi_hba = sas_dev->hisi_hba;

	/* SoC bug workaround */
	if (dev_is_sata(sas_dev->sas_device))
		clear_bit(sas_dev->sata_idx, hisi_hba->sata_dev_bitmap);
}

static int reset_hw_v2_hw(struct hisi_hba *hisi_hba)
{
	int i, reset_val;
	u32 val;
	unsigned long end_time;
	struct device *dev = hisi_hba->dev;

	/* The mask needs to be set depending on the number of phys */
	if (hisi_hba->n_phy == 9)
		reset_val = 0x1fffff;
	else
		reset_val = 0x7ffff;

	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0);

	/* Disable all of the PHYs */
	for (i = 0; i < hisi_hba->n_phy; i++) {
		u32 phy_cfg = hisi_sas_phy_read32(hisi_hba, i, PHY_CFG);

		phy_cfg &= ~PHY_CTRL_RESET_MSK;
		hisi_sas_phy_write32(hisi_hba, i, PHY_CFG, phy_cfg);
	}
	udelay(50);

	/* Ensure DMA tx & rx idle */
	for (i = 0; i < hisi_hba->n_phy; i++) {
		u32 dma_tx_status, dma_rx_status;

		end_time = jiffies + msecs_to_jiffies(1000);

		while (1) {
			dma_tx_status = hisi_sas_phy_read32(hisi_hba, i,
							    DMA_TX_STATUS);
			dma_rx_status = hisi_sas_phy_read32(hisi_hba, i,
							    DMA_RX_STATUS);

			if (!(dma_tx_status & DMA_TX_STATUS_BUSY_MSK) &&
				!(dma_rx_status & DMA_RX_STATUS_BUSY_MSK))
				break;

			msleep(20);
			if (time_after(jiffies, end_time))
				return -EIO;
		}
	}

	/* Ensure axi bus idle */
	end_time = jiffies + msecs_to_jiffies(1000);
	while (1) {
		u32 axi_status =
			hisi_sas_read32(hisi_hba, AXI_CFG);

		if (axi_status == 0)
			break;

		msleep(20);
		if (time_after(jiffies, end_time))
			return -EIO;
	}

	if (ACPI_HANDLE(dev)) {
		acpi_status s;

		s = acpi_evaluate_object(ACPI_HANDLE(dev), "_RST", NULL, NULL);
		if (ACPI_FAILURE(s)) {
			dev_err(dev, "Reset failed\n");
			return -EIO;
		}
	} else if (hisi_hba->ctrl) {
		/* reset and disable clock*/
		regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_reset_reg,
				reset_val);
		regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_clock_ena_reg + 4,
				reset_val);
		msleep(1);
		regmap_read(hisi_hba->ctrl, hisi_hba->ctrl_reset_sts_reg, &val);
		if (reset_val != (val & reset_val)) {
			dev_err(dev, "SAS reset fail.\n");
			return -EIO;
		}

		/* De-reset and enable clock*/
		regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_reset_reg + 4,
				reset_val);
		regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_clock_ena_reg,
				reset_val);
		msleep(1);
		regmap_read(hisi_hba->ctrl, hisi_hba->ctrl_reset_sts_reg,
				&val);
		if (val & reset_val) {
			dev_err(dev, "SAS de-reset fail.\n");
			return -EIO;
		}
	} else {
		dev_err(dev, "no reset method\n");
		return -EINVAL;
	}

	return 0;
}

/* This function needs to be called after resetting SAS controller. */
static void phys_reject_stp_links_v2_hw(struct hisi_hba *hisi_hba)
{
	u32 cfg;
	int phy_no;

	hisi_hba->reject_stp_links_msk = (1 << hisi_hba->n_phy) - 1;
	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++) {
		cfg = hisi_sas_phy_read32(hisi_hba, phy_no, CON_CONTROL);
		if (!(cfg & CON_CONTROL_CFG_OPEN_ACC_STP_MSK))
			continue;

		cfg &= ~CON_CONTROL_CFG_OPEN_ACC_STP_MSK;
		hisi_sas_phy_write32(hisi_hba, phy_no, CON_CONTROL, cfg);
	}
}

static void phys_try_accept_stp_links_v2_hw(struct hisi_hba *hisi_hba)
{
	int phy_no;
	u32 dma_tx_dfx1;

	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++) {
		if (!(hisi_hba->reject_stp_links_msk & BIT(phy_no)))
			continue;

		dma_tx_dfx1 = hisi_sas_phy_read32(hisi_hba, phy_no,
						DMA_TX_DFX1);
		if (dma_tx_dfx1 & DMA_TX_DFX1_IPTT_MSK) {
			u32 cfg = hisi_sas_phy_read32(hisi_hba,
				phy_no, CON_CONTROL);

			cfg |= CON_CONTROL_CFG_OPEN_ACC_STP_MSK;
			hisi_sas_phy_write32(hisi_hba, phy_no,
				CON_CONTROL, cfg);
			clear_bit(phy_no, &hisi_hba->reject_stp_links_msk);
		}
	}
}

static const struct signal_attenuation_s x6000 = {9200, 0, 10476};
static const struct sig_atten_lu_s sig_atten_lu[] = {
	{ &x6000, 0x3016a68 },
};

static void init_reg_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	u32 sas_phy_ctrl = 0x30b9908;
	u32 signal[3];
	int i;

	/* Global registers init */

	/* Deal with am-max-transmissions quirk */
	if (device_property_present(dev, "hip06-sas-v2-quirk-amt")) {
		hisi_sas_write32(hisi_hba, AM_CFG_MAX_TRANS, 0x2020);
		hisi_sas_write32(hisi_hba, AM_CFG_SINGLE_PORT_MAX_TRANS,
				 0x2020);
	} /* Else, use defaults -> do nothing */

	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE,
			 (u32)((1ULL << hisi_hba->queue_count) - 1));
	hisi_sas_write32(hisi_hba, AXI_USER1, 0xc0000000);
	hisi_sas_write32(hisi_hba, AXI_USER2, 0x10000);
	hisi_sas_write32(hisi_hba, HGC_SAS_TXFAIL_RETRY_CTRL, 0x0);
	hisi_sas_write32(hisi_hba, HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL, 0x7FF);
	hisi_sas_write32(hisi_hba, OPENA_WT_CONTI_TIME, 0x1);
	hisi_sas_write32(hisi_hba, I_T_NEXUS_LOSS_TIME, 0x1F4);
	hisi_sas_write32(hisi_hba, MAX_CON_TIME_LIMIT_TIME, 0x32);
	hisi_sas_write32(hisi_hba, BUS_INACTIVE_LIMIT_TIME, 0x1);
	hisi_sas_write32(hisi_hba, CFG_AGING_TIME, 0x1);
	hisi_sas_write32(hisi_hba, HGC_ERR_STAT_EN, 0x1);
	hisi_sas_write32(hisi_hba, HGC_GET_ITV_TIME, 0x1);
	hisi_sas_write32(hisi_hba, INT_COAL_EN, 0xc);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME, 0x60);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT, 0x3);
	hisi_sas_write32(hisi_hba, ENT_INT_COAL_TIME, 0x1);
	hisi_sas_write32(hisi_hba, ENT_INT_COAL_CNT, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 0x0);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0x7efefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0x7efefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0x7ffe20fe);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xfff00c30);
	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK+0x4*i, 0);

	hisi_sas_write32(hisi_hba, AXI_AHB_CLK_CFG, 1);
	hisi_sas_write32(hisi_hba, HYPER_STREAM_ID_EN_CFG, 1);

	/* Get sas_phy_ctrl value to deal with TX FFE issue. */
	if (!device_property_read_u32_array(dev, "hisilicon,signal-attenuation",
					    signal, ARRAY_SIZE(signal))) {
		for (i = 0; i < ARRAY_SIZE(sig_atten_lu); i++) {
			const struct sig_atten_lu_s *lookup = &sig_atten_lu[i];
			const struct signal_attenuation_s *att = lookup->att;

			if ((signal[0] == att->de_emphasis) &&
			    (signal[1] == att->preshoot) &&
			    (signal[2] == att->boost)) {
				sas_phy_ctrl = lookup->sas_phy_ctrl;
				break;
			}
		}

		if (i == ARRAY_SIZE(sig_atten_lu))
			dev_warn(dev, "unknown signal attenuation values, using default PHY ctrl config\n");
	}

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, PROG_PHY_LINK_RATE, 0x855);
		hisi_sas_phy_write32(hisi_hba, i, SAS_PHY_CTRL, sas_phy_ctrl);
		hisi_sas_phy_write32(hisi_hba, i, SL_TOUT_CFG, 0x7d7d7d7d);
		hisi_sas_phy_write32(hisi_hba, i, SL_CONTROL, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, TXID_AUTO, 0x2);
		hisi_sas_phy_write32(hisi_hba, i, DONE_RECEIVED_TIME, 0x8);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xfff87fff);
		hisi_sas_phy_write32(hisi_hba, i, RXOP_CHECK_CFG_H, 0x1000);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xff857fff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0x8ffffbfe);
		hisi_sas_phy_write32(hisi_hba, i, SL_CFG, 0x13f801fc);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_DWS_RESET_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT_COAL_EN, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_OOB_RESTART_MSK, 0x0);
		if (hisi_hba->refclk_frequency_mhz == 66)
			hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL, 0x199B694);
		/* else, do nothing -> leave it how you found it */
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		/* Delivery queue */
		hisi_sas_write32(hisi_hba,
				 DLVRY_Q_0_BASE_ADDR_HI + (i * 0x14),
				 upper_32_bits(hisi_hba->cmd_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, DLVRY_Q_0_BASE_ADDR_LO + (i * 0x14),
				 lower_32_bits(hisi_hba->cmd_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, DLVRY_Q_0_DEPTH + (i * 0x14),
				 HISI_SAS_QUEUE_SLOTS);

		/* Completion queue */
		hisi_sas_write32(hisi_hba, COMPL_Q_0_BASE_ADDR_HI + (i * 0x14),
				 upper_32_bits(hisi_hba->complete_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, COMPL_Q_0_BASE_ADDR_LO + (i * 0x14),
				 lower_32_bits(hisi_hba->complete_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, COMPL_Q_0_DEPTH + (i * 0x14),
				 HISI_SAS_QUEUE_SLOTS);
	}

	/* itct */
	hisi_sas_write32(hisi_hba, ITCT_BASE_ADDR_LO,
			 lower_32_bits(hisi_hba->itct_dma));

	hisi_sas_write32(hisi_hba, ITCT_BASE_ADDR_HI,
			 upper_32_bits(hisi_hba->itct_dma));

	/* iost */
	hisi_sas_write32(hisi_hba, IOST_BASE_ADDR_LO,
			 lower_32_bits(hisi_hba->iost_dma));

	hisi_sas_write32(hisi_hba, IOST_BASE_ADDR_HI,
			 upper_32_bits(hisi_hba->iost_dma));

	/* breakpoint */
	hisi_sas_write32(hisi_hba, IO_BROKEN_MSG_ADDR_LO,
			 lower_32_bits(hisi_hba->breakpoint_dma));

	hisi_sas_write32(hisi_hba, IO_BROKEN_MSG_ADDR_HI,
			 upper_32_bits(hisi_hba->breakpoint_dma));

	/* SATA broken msg */
	hisi_sas_write32(hisi_hba, IO_SATA_BROKEN_MSG_ADDR_LO,
			 lower_32_bits(hisi_hba->sata_breakpoint_dma));

	hisi_sas_write32(hisi_hba, IO_SATA_BROKEN_MSG_ADDR_HI,
			 upper_32_bits(hisi_hba->sata_breakpoint_dma));

	/* SATA initial fis */
	hisi_sas_write32(hisi_hba, SATA_INITI_D2H_STORE_ADDR_LO,
			 lower_32_bits(hisi_hba->initial_fis_dma));

	hisi_sas_write32(hisi_hba, SATA_INITI_D2H_STORE_ADDR_HI,
			 upper_32_bits(hisi_hba->initial_fis_dma));
}

static void link_timeout_enable_link(struct timer_list *t)
{
	struct hisi_hba *hisi_hba = from_timer(hisi_hba, t, timer);
	int i, reg_val;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		if (hisi_hba->reject_stp_links_msk & BIT(i))
			continue;

		reg_val = hisi_sas_phy_read32(hisi_hba, i, CON_CONTROL);
		if (!(reg_val & BIT(0))) {
			hisi_sas_phy_write32(hisi_hba, i,
					CON_CONTROL, 0x7);
			break;
		}
	}

	hisi_hba->timer.function = link_timeout_disable_link;
	mod_timer(&hisi_hba->timer, jiffies + msecs_to_jiffies(900));
}

static void link_timeout_disable_link(struct timer_list *t)
{
	struct hisi_hba *hisi_hba = from_timer(hisi_hba, t, timer);
	int i, reg_val;

	reg_val = hisi_sas_read32(hisi_hba, PHY_STATE);
	for (i = 0; i < hisi_hba->n_phy && reg_val; i++) {
		if (hisi_hba->reject_stp_links_msk & BIT(i))
			continue;

		if (reg_val & BIT(i)) {
			hisi_sas_phy_write32(hisi_hba, i,
					CON_CONTROL, 0x6);
			break;
		}
	}

	hisi_hba->timer.function = link_timeout_enable_link;
	mod_timer(&hisi_hba->timer, jiffies + msecs_to_jiffies(100));
}

static void set_link_timer_quirk(struct hisi_hba *hisi_hba)
{
	hisi_hba->timer.function = link_timeout_disable_link;
	hisi_hba->timer.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&hisi_hba->timer);
}

static int hw_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int rc;

	rc = reset_hw_v2_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "hisi_sas_reset_hw failed, rc=%d", rc);
		return rc;
	}

	msleep(100);
	init_reg_v2_hw(hisi_hba);

	return 0;
}

static void enable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg |= PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static bool is_sata_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 context;

	context = hisi_sas_read32(hisi_hba, PHY_CONTEXT);
	if (context & (1 << phy_no))
		return true;

	return false;
}

static bool tx_fifo_is_empty_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 dfx_val;

	dfx_val = hisi_sas_phy_read32(hisi_hba, phy_no, DMA_TX_DFX1);

	if (dfx_val & BIT(16))
		return false;

	return true;
}

static bool axi_bus_is_idle_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	int i, max_loop = 1000;
	struct device *dev = hisi_hba->dev;
	u32 status, axi_status, dfx_val, dfx_tx_val;

	for (i = 0; i < max_loop; i++) {
		status = hisi_sas_read32_relaxed(hisi_hba,
			AXI_MASTER_CFG_BASE + AM_CURR_TRANS_RETURN);

		axi_status = hisi_sas_read32(hisi_hba, AXI_CFG);
		dfx_val = hisi_sas_phy_read32(hisi_hba, phy_no, DMA_TX_DFX1);
		dfx_tx_val = hisi_sas_phy_read32(hisi_hba,
			phy_no, DMA_TX_FIFO_DFX0);

		if ((status == 0x3) && (axi_status == 0x0) &&
		    (dfx_val & BIT(20)) && (dfx_tx_val & BIT(10)))
			return true;
		udelay(10);
	}
	dev_err(dev, "bus is not idle phy%d, axi150:0x%x axi100:0x%x port204:0x%x port240:0x%x\n",
			phy_no, status, axi_status,
			dfx_val, dfx_tx_val);
	return false;
}

static bool wait_io_done_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	int i, max_loop = 1000;
	struct device *dev = hisi_hba->dev;
	u32 status, tx_dfx0;

	for (i = 0; i < max_loop; i++) {
		status = hisi_sas_phy_read32(hisi_hba, phy_no, LINK_DFX2);
		status = (status & 0x3fc0) >> 6;

		if (status != 0x1)
			return true;

		tx_dfx0 = hisi_sas_phy_read32(hisi_hba, phy_no, DMA_TX_DFX0);
		if ((tx_dfx0 & 0x1ff) == 0x2)
			return true;
		udelay(10);
	}
	dev_err(dev, "IO not done phy%d, port264:0x%x port200:0x%x\n",
			phy_no, status, tx_dfx0);
	return false;
}

static bool allowed_disable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	if (tx_fifo_is_empty_v2_hw(hisi_hba, phy_no))
		return true;

	if (!axi_bus_is_idle_v2_hw(hisi_hba, phy_no))
		return false;

	if (!wait_io_done_v2_hw(hisi_hba, phy_no))
		return false;

	return true;
}


static void disable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg, axi_val, dfx0_val, txid_auto;
	struct device *dev = hisi_hba->dev;

	/* Close axi bus. */
	axi_val = hisi_sas_read32(hisi_hba, AXI_MASTER_CFG_BASE +
				AM_CTRL_GLOBAL);
	axi_val |= 0x1;
	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE +
		AM_CTRL_GLOBAL, axi_val);

	if (is_sata_phy_v2_hw(hisi_hba, phy_no)) {
		if (allowed_disable_phy_v2_hw(hisi_hba, phy_no))
			goto do_disable;

		/* Reset host controller. */
		queue_work(hisi_hba->wq, &hisi_hba->rst_work);
		return;
	}

	dfx0_val = hisi_sas_phy_read32(hisi_hba, phy_no, PORT_DFX0);
	dfx0_val = (dfx0_val & 0x1fc0) >> 6;
	if (dfx0_val != 0x4)
		goto do_disable;

	if (!tx_fifo_is_empty_v2_hw(hisi_hba, phy_no)) {
		dev_warn(dev, "phy%d, wait tx fifo need send break\n",
			phy_no);
		txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no,
					TXID_AUTO);
		txid_auto |= TXID_AUTO_CTB_MSK;
		hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
					txid_auto);
	}

do_disable:
	cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);
	cfg &= ~PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);

	/* Open axi bus. */
	axi_val &= ~0x1;
	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE +
		AM_CTRL_GLOBAL, axi_val);
}

static void start_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	config_id_frame_v2_hw(hisi_hba, phy_no);
	config_phy_opt_mode_v2_hw(hisi_hba, phy_no);
	enable_phy_v2_hw(hisi_hba, phy_no);
}

static void phy_hard_reset_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	u32 txid_auto;

	disable_phy_v2_hw(hisi_hba, phy_no);
	if (phy->identify.device_type == SAS_END_DEVICE) {
		txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
		hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
					txid_auto | TX_HARDRST_MSK);
	}
	msleep(100);
	start_phy_v2_hw(hisi_hba, phy_no);
}

static void phy_get_events_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_phy *sphy = sas_phy->phy;
	u32 err4_reg_val, err6_reg_val;

	/* loss dword syn, phy reset problem */
	err4_reg_val = hisi_sas_phy_read32(hisi_hba, phy_no, SAS_ERR_CNT4_REG);

	/* disparity err, invalid dword */
	err6_reg_val = hisi_sas_phy_read32(hisi_hba, phy_no, SAS_ERR_CNT6_REG);

	sphy->loss_of_dword_sync_count += (err4_reg_val >> 16) & 0xFFFF;
	sphy->phy_reset_problem_count += err4_reg_val & 0xFFFF;
	sphy->invalid_dword_count += (err6_reg_val & 0xFF0000) >> 16;
	sphy->running_disparity_error_count += err6_reg_val & 0xFF;
}

static void phys_init_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[i];
		struct asd_sas_phy *sas_phy = &phy->sas_phy;

		if (!sas_phy->phy->enabled)
			continue;

		start_phy_v2_hw(hisi_hba, i);
	}
}

static void sl_notify_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 sl_control;

	sl_control = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	sl_control |= SL_CONTROL_NOTIFY_EN_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL, sl_control);
	msleep(1);
	sl_control = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	sl_control &= ~SL_CONTROL_NOTIFY_EN_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL, sl_control);
}

static enum sas_linkrate phy_get_max_linkrate_v2_hw(void)
{
	return SAS_LINK_RATE_12_0_GBPS;
}

static void phy_set_linkrate_v2_hw(struct hisi_hba *hisi_hba, int phy_no,
		struct sas_phy_linkrates *r)
{
	u32 prog_phy_link_rate =
		hisi_sas_phy_read32(hisi_hba, phy_no, PROG_PHY_LINK_RATE);
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	int i;
	enum sas_linkrate min, max;
	u32 rate_mask = 0;

	if (r->maximum_linkrate == SAS_LINK_RATE_UNKNOWN) {
		max = sas_phy->phy->maximum_linkrate;
		min = r->minimum_linkrate;
	} else if (r->minimum_linkrate == SAS_LINK_RATE_UNKNOWN) {
		max = r->maximum_linkrate;
		min = sas_phy->phy->minimum_linkrate;
	} else
		return;

	sas_phy->phy->maximum_linkrate = max;
	sas_phy->phy->minimum_linkrate = min;

	max -= SAS_LINK_RATE_1_5_GBPS;

	for (i = 0; i <= max; i++)
		rate_mask |= 1 << (i * 2);

	prog_phy_link_rate &= ~0xff;
	prog_phy_link_rate |= rate_mask;

	disable_phy_v2_hw(hisi_hba, phy_no);
	msleep(100);
	hisi_sas_phy_write32(hisi_hba, phy_no, PROG_PHY_LINK_RATE,
			prog_phy_link_rate);
	start_phy_v2_hw(hisi_hba, phy_no);
}

static int get_wideport_bitmap_v2_hw(struct hisi_hba *hisi_hba, int port_id)
{
	int i, bitmap = 0;
	u32 phy_port_num_ma = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
	u32 phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);

	for (i = 0; i < (hisi_hba->n_phy < 9 ? hisi_hba->n_phy : 8); i++)
		if (phy_state & 1 << i)
			if (((phy_port_num_ma >> (i * 4)) & 0xf) == port_id)
				bitmap |= 1 << i;

	if (hisi_hba->n_phy == 9) {
		u32 port_state = hisi_sas_read32(hisi_hba, PORT_STATE);

		if (phy_state & 1 << 8)
			if (((port_state & PORT_STATE_PHY8_PORT_NUM_MSK) >>
			     PORT_STATE_PHY8_PORT_NUM_OFF) == port_id)
				bitmap |= 1 << 9;
	}

	return bitmap;
}

/*
 * The callpath to this function and upto writing the write
 * queue pointer should be safe from interruption.
 */
static int
get_free_slot_v2_hw(struct hisi_hba *hisi_hba, struct hisi_sas_dq *dq)
{
	struct device *dev = hisi_hba->dev;
	int queue = dq->id;
	u32 r, w;

	w = dq->wr_point;
	r = hisi_sas_read32_relaxed(hisi_hba,
				DLVRY_Q_0_RD_PTR + (queue * 0x14));
	if (r == (w+1) % HISI_SAS_QUEUE_SLOTS) {
		dev_warn(dev, "full queue=%d r=%d w=%d\n\n",
				queue, r, w);
		return -EAGAIN;
	}

	return 0;
}

static void start_delivery_v2_hw(struct hisi_sas_dq *dq)
{
	struct hisi_hba *hisi_hba = dq->hisi_hba;
	int dlvry_queue = dq->slot_prep->dlvry_queue;
	int dlvry_queue_slot = dq->slot_prep->dlvry_queue_slot;

	dq->wr_point = ++dlvry_queue_slot % HISI_SAS_QUEUE_SLOTS;
	hisi_sas_write32(hisi_hba, DLVRY_Q_0_WR_PTR + (dlvry_queue * 0x14),
			 dq->wr_point);
}

static int prep_prd_sge_v2_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_slot *slot,
			      struct hisi_sas_cmd_hdr *hdr,
			      struct scatterlist *scatter,
			      int n_elem)
{
	struct hisi_sas_sge_page *sge_page = hisi_sas_sge_addr_mem(slot);
	struct device *dev = hisi_hba->dev;
	struct scatterlist *sg;
	int i;

	if (n_elem > HISI_SAS_SGE_PAGE_CNT) {
		dev_err(dev, "prd err: n_elem(%d) > HISI_SAS_SGE_PAGE_CNT",
			n_elem);
		return -EINVAL;
	}

	for_each_sg(scatter, sg, n_elem, i) {
		struct hisi_sas_sge *entry = &sge_page->sge[i];

		entry->addr = cpu_to_le64(sg_dma_address(sg));
		entry->page_ctrl_0 = entry->page_ctrl_1 = 0;
		entry->data_len = cpu_to_le32(sg_dma_len(sg));
		entry->data_off = 0;
	}

	hdr->prd_table_addr = cpu_to_le64(hisi_sas_sge_addr_dma(slot));

	hdr->sg_len = cpu_to_le32(n_elem << CMD_HDR_DATA_SGL_LEN_OFF);

	return 0;
}

static int prep_smp_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct device *dev = hisi_hba->dev;
	struct hisi_sas_port *port = slot->port;
	struct scatterlist *sg_req, *sg_resp;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	dma_addr_t req_dma_addr;
	unsigned int req_len, resp_len;
	int elem, rc;

	/*
	* DMA-map SMP request, response buffers
	*/
	/* req */
	sg_req = &task->smp_task.smp_req;
	elem = dma_map_sg(dev, sg_req, 1, DMA_TO_DEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);
	req_dma_addr = sg_dma_address(sg_req);

	/* resp */
	sg_resp = &task->smp_task.smp_resp;
	elem = dma_map_sg(dev, sg_resp, 1, DMA_FROM_DEVICE);
	if (!elem) {
		rc = -ENOMEM;
		goto err_out_req;
	}
	resp_len = sg_dma_len(sg_resp);
	if ((req_len & 0x3) || (resp_len & 0x3)) {
		rc = -EINVAL;
		goto err_out_resp;
	}

	/* create header */
	/* dw0 */
	hdr->dw0 = cpu_to_le32((port->id << CMD_HDR_PORT_OFF) |
			       (1 << CMD_HDR_PRIORITY_OFF) | /* high pri */
			       (2 << CMD_HDR_CMD_OFF)); /* smp */

	/* map itct entry */
	hdr->dw1 = cpu_to_le32((sas_dev->device_id << CMD_HDR_DEV_ID_OFF) |
			       (1 << CMD_HDR_FRAME_TYPE_OFF) |
			       (DIR_NO_DATA << CMD_HDR_DIR_OFF));

	/* dw2 */
	hdr->dw2 = cpu_to_le32((((req_len - 4) / 4) << CMD_HDR_CFL_OFF) |
			       (HISI_SAS_MAX_SMP_RESP_SZ / 4 <<
			       CMD_HDR_MRFL_OFF));

	hdr->transfer_tags = cpu_to_le32(slot->idx << CMD_HDR_IPTT_OFF);

	hdr->cmd_table_addr = cpu_to_le64(req_dma_addr);
	hdr->sts_buffer_addr = cpu_to_le64(hisi_sas_status_buf_addr_dma(slot));

	return 0;

err_out_resp:
	dma_unmap_sg(dev, &slot->task->smp_task.smp_resp, 1,
		     DMA_FROM_DEVICE);
err_out_req:
	dma_unmap_sg(dev, &slot->task->smp_task.smp_req, 1,
		     DMA_TO_DEVICE);
	return rc;
}

static int prep_ssp_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot, int is_tmf,
			  struct hisi_sas_tmf_task *tmf)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port = slot->port;
	struct sas_ssp_task *ssp_task = &task->ssp_task;
	struct scsi_cmnd *scsi_cmnd = ssp_task->cmd;
	int has_data = 0, rc, priority = is_tmf;
	u8 *buf_cmd;
	u32 dw1 = 0, dw2 = 0;

	hdr->dw0 = cpu_to_le32((1 << CMD_HDR_RESP_REPORT_OFF) |
			       (2 << CMD_HDR_TLR_CTRL_OFF) |
			       (port->id << CMD_HDR_PORT_OFF) |
			       (priority << CMD_HDR_PRIORITY_OFF) |
			       (1 << CMD_HDR_CMD_OFF)); /* ssp */

	dw1 = 1 << CMD_HDR_VDTL_OFF;
	if (is_tmf) {
		dw1 |= 2 << CMD_HDR_FRAME_TYPE_OFF;
		dw1 |= DIR_NO_DATA << CMD_HDR_DIR_OFF;
	} else {
		dw1 |= 1 << CMD_HDR_FRAME_TYPE_OFF;
		switch (scsi_cmnd->sc_data_direction) {
		case DMA_TO_DEVICE:
			has_data = 1;
			dw1 |= DIR_TO_DEVICE << CMD_HDR_DIR_OFF;
			break;
		case DMA_FROM_DEVICE:
			has_data = 1;
			dw1 |= DIR_TO_INI << CMD_HDR_DIR_OFF;
			break;
		default:
			dw1 &= ~CMD_HDR_DIR_MSK;
		}
	}

	/* map itct entry */
	dw1 |= sas_dev->device_id << CMD_HDR_DEV_ID_OFF;
	hdr->dw1 = cpu_to_le32(dw1);

	dw2 = (((sizeof(struct ssp_command_iu) + sizeof(struct ssp_frame_hdr)
	      + 3) / 4) << CMD_HDR_CFL_OFF) |
	      ((HISI_SAS_MAX_SSP_RESP_SZ / 4) << CMD_HDR_MRFL_OFF) |
	      (2 << CMD_HDR_SG_MOD_OFF);
	hdr->dw2 = cpu_to_le32(dw2);

	hdr->transfer_tags = cpu_to_le32(slot->idx);

	if (has_data) {
		rc = prep_prd_sge_v2_hw(hisi_hba, slot, hdr, task->scatter,
					slot->n_elem);
		if (rc)
			return rc;
	}

	hdr->data_transfer_len = cpu_to_le32(task->total_xfer_len);
	hdr->cmd_table_addr = cpu_to_le64(hisi_sas_cmd_hdr_addr_dma(slot));
	hdr->sts_buffer_addr = cpu_to_le64(hisi_sas_status_buf_addr_dma(slot));

	buf_cmd = hisi_sas_cmd_hdr_addr_mem(slot) +
		sizeof(struct ssp_frame_hdr);

	memcpy(buf_cmd, &task->ssp_task.LUN, 8);
	if (!is_tmf) {
		buf_cmd[9] = task->ssp_task.task_attr |
				(task->ssp_task.task_prio << 3);
		memcpy(buf_cmd + 12, task->ssp_task.cmd->cmnd,
				task->ssp_task.cmd->cmd_len);
	} else {
		buf_cmd[10] = tmf->tmf;
		switch (tmf->tmf) {
		case TMF_ABORT_TASK:
		case TMF_QUERY_TASK:
			buf_cmd[12] =
				(tmf->tag_of_task_to_be_managed >> 8) & 0xff;
			buf_cmd[13] =
				tmf->tag_of_task_to_be_managed & 0xff;
			break;
		default:
			break;
		}
	}

	return 0;
}

#define TRANS_TX_ERR	0
#define TRANS_RX_ERR	1
#define DMA_TX_ERR		2
#define SIPC_RX_ERR		3
#define DMA_RX_ERR		4

#define DMA_TX_ERR_OFF	0
#define DMA_TX_ERR_MSK	(0xffff << DMA_TX_ERR_OFF)
#define SIPC_RX_ERR_OFF	16
#define SIPC_RX_ERR_MSK (0xffff << SIPC_RX_ERR_OFF)

static int parse_trans_tx_err_code_v2_hw(u32 err_msk)
{
	static const u8 trans_tx_err_code_prio[] = {
		TRANS_TX_OPEN_FAIL_WITH_IT_NEXUS_LOSS,
		TRANS_TX_ERR_PHY_NOT_ENABLE,
		TRANS_TX_OPEN_CNX_ERR_WRONG_DESTINATION,
		TRANS_TX_OPEN_CNX_ERR_ZONE_VIOLATION,
		TRANS_TX_OPEN_CNX_ERR_BY_OTHER,
		RESERVED0,
		TRANS_TX_OPEN_CNX_ERR_AIP_TIMEOUT,
		TRANS_TX_OPEN_CNX_ERR_STP_RESOURCES_BUSY,
		TRANS_TX_OPEN_CNX_ERR_PROTOCOL_NOT_SUPPORTED,
		TRANS_TX_OPEN_CNX_ERR_CONNECTION_RATE_NOT_SUPPORTED,
		TRANS_TX_OPEN_CNX_ERR_BAD_DESTINATION,
		TRANS_TX_OPEN_CNX_ERR_BREAK_RCVD,
		TRANS_TX_OPEN_CNX_ERR_LOW_PHY_POWER,
		TRANS_TX_OPEN_CNX_ERR_PATHWAY_BLOCKED,
		TRANS_TX_OPEN_CNX_ERR_OPEN_TIMEOUT,
		TRANS_TX_OPEN_CNX_ERR_NO_DESTINATION,
		TRANS_TX_OPEN_RETRY_ERR_THRESHOLD_REACHED,
		TRANS_TX_ERR_WITH_CLOSE_PHYDISALE,
		TRANS_TX_ERR_WITH_CLOSE_DWS_TIMEOUT,
		TRANS_TX_ERR_WITH_CLOSE_COMINIT,
		TRANS_TX_ERR_WITH_BREAK_TIMEOUT,
		TRANS_TX_ERR_WITH_BREAK_REQUEST,
		TRANS_TX_ERR_WITH_BREAK_RECEVIED,
		TRANS_TX_ERR_WITH_CLOSE_TIMEOUT,
		TRANS_TX_ERR_WITH_CLOSE_NORMAL,
		TRANS_TX_ERR_WITH_NAK_RECEVIED,
		TRANS_TX_ERR_WITH_ACK_NAK_TIMEOUT,
		TRANS_TX_ERR_WITH_CREDIT_TIMEOUT,
		TRANS_TX_ERR_WITH_IPTT_CONFLICT,
		TRANS_TX_ERR_WITH_OPEN_BY_DES_OR_OTHERS,
		TRANS_TX_ERR_WITH_WAIT_RECV_TIMEOUT,
	};
	int index, i;

	for (i = 0; i < ARRAY_SIZE(trans_tx_err_code_prio); i++) {
		index = trans_tx_err_code_prio[i] - TRANS_TX_FAIL_BASE;
		if (err_msk & (1 << index))
			return trans_tx_err_code_prio[i];
	}
	return -1;
}

static int parse_trans_rx_err_code_v2_hw(u32 err_msk)
{
	static const u8 trans_rx_err_code_prio[] = {
		TRANS_RX_ERR_WITH_RXFRAME_CRC_ERR,
		TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR,
		TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM,
		TRANS_RX_ERR_WITH_RXFIS_DECODE_ERROR,
		TRANS_RX_ERR_WITH_RXFIS_CRC_ERR,
		TRANS_RX_ERR_WITH_RXFRAME_LENGTH_OVERRUN,
		TRANS_RX_ERR_WITH_RXFIS_RX_SYNCP,
		TRANS_RX_ERR_WITH_LINK_BUF_OVERRUN,
		TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE,
		TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT,
		TRANS_RX_ERR_WITH_CLOSE_COMINIT,
		TRANS_RX_ERR_WITH_BREAK_TIMEOUT,
		TRANS_RX_ERR_WITH_BREAK_REQUEST,
		TRANS_RX_ERR_WITH_BREAK_RECEVIED,
		RESERVED1,
		TRANS_RX_ERR_WITH_CLOSE_NORMAL,
		TRANS_RX_ERR_WITH_DATA_LEN0,
		TRANS_RX_ERR_WITH_BAD_HASH,
		TRANS_RX_XRDY_WLEN_ZERO_ERR,
		TRANS_RX_SSP_FRM_LEN_ERR,
		RESERVED2,
		RESERVED3,
		RESERVED4,
		RESERVED5,
		TRANS_RX_ERR_WITH_BAD_FRM_TYPE,
		TRANS_RX_SMP_FRM_LEN_ERR,
		TRANS_RX_SMP_RESP_TIMEOUT_ERR,
		RESERVED6,
		RESERVED7,
		RESERVED8,
		RESERVED9,
		TRANS_RX_R_ERR,
	};
	int index, i;

	for (i = 0; i < ARRAY_SIZE(trans_rx_err_code_prio); i++) {
		index = trans_rx_err_code_prio[i] - TRANS_RX_FAIL_BASE;
		if (err_msk & (1 << index))
			return trans_rx_err_code_prio[i];
	}
	return -1;
}

static int parse_dma_tx_err_code_v2_hw(u32 err_msk)
{
	static const u8 dma_tx_err_code_prio[] = {
		DMA_TX_UNEXP_XFER_ERR,
		DMA_TX_UNEXP_RETRANS_ERR,
		DMA_TX_XFER_LEN_OVERFLOW,
		DMA_TX_XFER_OFFSET_ERR,
		DMA_TX_RAM_ECC_ERR,
		DMA_TX_DIF_LEN_ALIGN_ERR,
		DMA_TX_DIF_CRC_ERR,
		DMA_TX_DIF_APP_ERR,
		DMA_TX_DIF_RPP_ERR,
		DMA_TX_DATA_SGL_OVERFLOW,
		DMA_TX_DIF_SGL_OVERFLOW,
	};
	int index, i;

	for (i = 0; i < ARRAY_SIZE(dma_tx_err_code_prio); i++) {
		index = dma_tx_err_code_prio[i] - DMA_TX_ERR_BASE;
		err_msk = err_msk & DMA_TX_ERR_MSK;
		if (err_msk & (1 << index))
			return dma_tx_err_code_prio[i];
	}
	return -1;
}

static int parse_sipc_rx_err_code_v2_hw(u32 err_msk)
{
	static const u8 sipc_rx_err_code_prio[] = {
		SIPC_RX_FIS_STATUS_ERR_BIT_VLD,
		SIPC_RX_PIO_WRSETUP_STATUS_DRQ_ERR,
		SIPC_RX_FIS_STATUS_BSY_BIT_ERR,
		SIPC_RX_WRSETUP_LEN_ODD_ERR,
		SIPC_RX_WRSETUP_LEN_ZERO_ERR,
		SIPC_RX_WRDATA_LEN_NOT_MATCH_ERR,
		SIPC_RX_NCQ_WRSETUP_OFFSET_ERR,
		SIPC_RX_NCQ_WRSETUP_AUTO_ACTIVE_ERR,
		SIPC_RX_SATA_UNEXP_FIS_ERR,
		SIPC_RX_WRSETUP_ESTATUS_ERR,
		SIPC_RX_DATA_UNDERFLOW_ERR,
	};
	int index, i;

	for (i = 0; i < ARRAY_SIZE(sipc_rx_err_code_prio); i++) {
		index = sipc_rx_err_code_prio[i] - SIPC_RX_ERR_BASE;
		err_msk = err_msk & SIPC_RX_ERR_MSK;
		if (err_msk & (1 << (index + 0x10)))
			return sipc_rx_err_code_prio[i];
	}
	return -1;
}

static int parse_dma_rx_err_code_v2_hw(u32 err_msk)
{
	static const u8 dma_rx_err_code_prio[] = {
		DMA_RX_UNKNOWN_FRM_ERR,
		DMA_RX_DATA_LEN_OVERFLOW,
		DMA_RX_DATA_LEN_UNDERFLOW,
		DMA_RX_DATA_OFFSET_ERR,
		RESERVED10,
		DMA_RX_SATA_FRAME_TYPE_ERR,
		DMA_RX_RESP_BUF_OVERFLOW,
		DMA_RX_UNEXP_RETRANS_RESP_ERR,
		DMA_RX_UNEXP_NORM_RESP_ERR,
		DMA_RX_UNEXP_RDFRAME_ERR,
		DMA_RX_PIO_DATA_LEN_ERR,
		DMA_RX_RDSETUP_STATUS_ERR,
		DMA_RX_RDSETUP_STATUS_DRQ_ERR,
		DMA_RX_RDSETUP_STATUS_BSY_ERR,
		DMA_RX_RDSETUP_LEN_ODD_ERR,
		DMA_RX_RDSETUP_LEN_ZERO_ERR,
		DMA_RX_RDSETUP_LEN_OVER_ERR,
		DMA_RX_RDSETUP_OFFSET_ERR,
		DMA_RX_RDSETUP_ACTIVE_ERR,
		DMA_RX_RDSETUP_ESTATUS_ERR,
		DMA_RX_RAM_ECC_ERR,
		DMA_RX_DIF_CRC_ERR,
		DMA_RX_DIF_APP_ERR,
		DMA_RX_DIF_RPP_ERR,
		DMA_RX_DATA_SGL_OVERFLOW,
		DMA_RX_DIF_SGL_OVERFLOW,
	};
	int index, i;

	for (i = 0; i < ARRAY_SIZE(dma_rx_err_code_prio); i++) {
		index = dma_rx_err_code_prio[i] - DMA_RX_ERR_BASE;
		if (err_msk & (1 << index))
			return dma_rx_err_code_prio[i];
	}
	return -1;
}

/* by default, task resp is complete */
static void slot_err_v2_hw(struct hisi_hba *hisi_hba,
			   struct sas_task *task,
			   struct hisi_sas_slot *slot,
			   int err_phase)
{
	struct task_status_struct *ts = &task->task_status;
	struct hisi_sas_err_record_v2 *err_record =
			hisi_sas_status_buf_addr_mem(slot);
	u32 trans_tx_fail_type = cpu_to_le32(err_record->trans_tx_fail_type);
	u32 trans_rx_fail_type = cpu_to_le32(err_record->trans_rx_fail_type);
	u16 dma_tx_err_type = cpu_to_le16(err_record->dma_tx_err_type);
	u16 sipc_rx_err_type = cpu_to_le16(err_record->sipc_rx_err_type);
	u32 dma_rx_err_type = cpu_to_le32(err_record->dma_rx_err_type);
	int error = -1;

	if (err_phase == 1) {
		/* error in TX phase, the priority of error is: DW2 > DW0 */
		error = parse_dma_tx_err_code_v2_hw(dma_tx_err_type);
		if (error == -1)
			error = parse_trans_tx_err_code_v2_hw(
					trans_tx_fail_type);
	} else if (err_phase == 2) {
		/* error in RX phase, the priority is: DW1 > DW3 > DW2 */
		error = parse_trans_rx_err_code_v2_hw(
					trans_rx_fail_type);
		if (error == -1) {
			error = parse_dma_rx_err_code_v2_hw(
					dma_rx_err_type);
			if (error == -1)
				error = parse_sipc_rx_err_code_v2_hw(
						sipc_rx_err_type);
		}
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
	{
		switch (error) {
		case TRANS_TX_OPEN_CNX_ERR_NO_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_NO_DEST;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_PROTOCOL_NOT_SUPPORTED:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_EPROTO;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_CONNECTION_RATE_NOT_SUPPORTED:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_CONN_RATE;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_BAD_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_BAD_DEST;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_WRONG_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
			break;
		}
		case DMA_RX_UNEXP_NORM_RESP_ERR:
		case TRANS_TX_OPEN_CNX_ERR_ZONE_VIOLATION:
		case DMA_RX_RESP_BUF_OVERFLOW:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_UNKNOWN;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_LOW_PHY_POWER:
		{
			/* not sure */
			ts->stat = SAS_DEV_NO_RESPONSE;
			break;
		}
		case DMA_RX_DATA_LEN_OVERFLOW:
		{
			ts->stat = SAS_DATA_OVERRUN;
			ts->residual = 0;
			break;
		}
		case DMA_RX_DATA_LEN_UNDERFLOW:
		{
			ts->residual = trans_tx_fail_type;
			ts->stat = SAS_DATA_UNDERRUN;
			break;
		}
		case TRANS_TX_OPEN_FAIL_WITH_IT_NEXUS_LOSS:
		case TRANS_TX_ERR_PHY_NOT_ENABLE:
		case TRANS_TX_OPEN_CNX_ERR_BY_OTHER:
		case TRANS_TX_OPEN_CNX_ERR_AIP_TIMEOUT:
		case TRANS_TX_OPEN_CNX_ERR_BREAK_RCVD:
		case TRANS_TX_OPEN_CNX_ERR_PATHWAY_BLOCKED:
		case TRANS_TX_OPEN_CNX_ERR_OPEN_TIMEOUT:
		case TRANS_TX_OPEN_RETRY_ERR_THRESHOLD_REACHED:
		case TRANS_TX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_BREAK_REQUEST:
		case TRANS_TX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_TX_ERR_WITH_CLOSE_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_TX_ERR_WITH_CLOSE_PHYDISALE:
		case TRANS_TX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_TX_ERR_WITH_NAK_RECEVIED:
		case TRANS_TX_ERR_WITH_ACK_NAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_CREDIT_TIMEOUT:
		case TRANS_TX_ERR_WITH_IPTT_CONFLICT:
		case TRANS_RX_ERR_WITH_RXFRAME_CRC_ERR:
		case TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR:
		case TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM:
		case TRANS_RX_ERR_WITH_LINK_BUF_OVERRUN:
		case TRANS_RX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_RX_ERR_WITH_BREAK_REQUEST:
		case TRANS_RX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_RX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_RX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_TX_ERR_FRAME_TXED:
		case TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE:
		case TRANS_RX_ERR_WITH_DATA_LEN0:
		case TRANS_RX_ERR_WITH_BAD_HASH:
		case TRANS_RX_XRDY_WLEN_ZERO_ERR:
		case TRANS_RX_SSP_FRM_LEN_ERR:
		case TRANS_RX_ERR_WITH_BAD_FRM_TYPE:
		case DMA_TX_DATA_SGL_OVERFLOW:
		case DMA_TX_UNEXP_XFER_ERR:
		case DMA_TX_UNEXP_RETRANS_ERR:
		case DMA_TX_XFER_LEN_OVERFLOW:
		case DMA_TX_XFER_OFFSET_ERR:
		case SIPC_RX_DATA_UNDERFLOW_ERR:
		case DMA_RX_DATA_SGL_OVERFLOW:
		case DMA_RX_DATA_OFFSET_ERR:
		case DMA_RX_RDSETUP_LEN_ODD_ERR:
		case DMA_RX_RDSETUP_LEN_ZERO_ERR:
		case DMA_RX_RDSETUP_LEN_OVER_ERR:
		case DMA_RX_SATA_FRAME_TYPE_ERR:
		case DMA_RX_UNKNOWN_FRM_ERR:
		{
			/* This will request a retry */
			ts->stat = SAS_QUEUE_FULL;
			slot->abort = 1;
			break;
		}
		default:
			break;
		}
	}
		break;
	case SAS_PROTOCOL_SMP:
		ts->stat = SAM_STAT_CHECK_CONDITION;
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
	{
		switch (error) {
		case TRANS_TX_OPEN_CNX_ERR_NO_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_NO_DEST;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_LOW_PHY_POWER:
		{
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_DEV_NO_RESPONSE;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_PROTOCOL_NOT_SUPPORTED:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_EPROTO;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_CONNECTION_RATE_NOT_SUPPORTED:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_CONN_RATE;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_BAD_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_CONN_RATE;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_WRONG_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
			break;
		}
		case DMA_RX_RESP_BUF_OVERFLOW:
		case DMA_RX_UNEXP_NORM_RESP_ERR:
		case TRANS_TX_OPEN_CNX_ERR_ZONE_VIOLATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_UNKNOWN;
			break;
		}
		case DMA_RX_DATA_LEN_OVERFLOW:
		{
			ts->stat = SAS_DATA_OVERRUN;
			ts->residual = 0;
			break;
		}
		case DMA_RX_DATA_LEN_UNDERFLOW:
		{
			ts->residual = trans_tx_fail_type;
			ts->stat = SAS_DATA_UNDERRUN;
			break;
		}
		case TRANS_TX_OPEN_FAIL_WITH_IT_NEXUS_LOSS:
		case TRANS_TX_ERR_PHY_NOT_ENABLE:
		case TRANS_TX_OPEN_CNX_ERR_BY_OTHER:
		case TRANS_TX_OPEN_CNX_ERR_AIP_TIMEOUT:
		case TRANS_TX_OPEN_CNX_ERR_BREAK_RCVD:
		case TRANS_TX_OPEN_CNX_ERR_PATHWAY_BLOCKED:
		case TRANS_TX_OPEN_CNX_ERR_OPEN_TIMEOUT:
		case TRANS_TX_OPEN_RETRY_ERR_THRESHOLD_REACHED:
		case TRANS_TX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_BREAK_REQUEST:
		case TRANS_TX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_TX_ERR_WITH_CLOSE_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_TX_ERR_WITH_CLOSE_PHYDISALE:
		case TRANS_TX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_TX_ERR_WITH_ACK_NAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_CREDIT_TIMEOUT:
		case TRANS_TX_ERR_WITH_OPEN_BY_DES_OR_OTHERS:
		case TRANS_TX_ERR_WITH_WAIT_RECV_TIMEOUT:
		case TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM:
		case TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR:
		case TRANS_RX_ERR_WITH_RXFIS_DECODE_ERROR:
		case TRANS_RX_ERR_WITH_RXFIS_CRC_ERR:
		case TRANS_RX_ERR_WITH_RXFRAME_LENGTH_OVERRUN:
		case TRANS_RX_ERR_WITH_RXFIS_RX_SYNCP:
		case TRANS_RX_ERR_WITH_LINK_BUF_OVERRUN:
		case TRANS_RX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_RX_ERR_WITH_BREAK_REQUEST:
		case TRANS_RX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_RX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE:
		case TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_RX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_RX_ERR_WITH_DATA_LEN0:
		case TRANS_RX_ERR_WITH_BAD_HASH:
		case TRANS_RX_XRDY_WLEN_ZERO_ERR:
		case TRANS_RX_ERR_WITH_BAD_FRM_TYPE:
		case DMA_TX_DATA_SGL_OVERFLOW:
		case DMA_TX_UNEXP_XFER_ERR:
		case DMA_TX_UNEXP_RETRANS_ERR:
		case DMA_TX_XFER_LEN_OVERFLOW:
		case DMA_TX_XFER_OFFSET_ERR:
		case SIPC_RX_FIS_STATUS_ERR_BIT_VLD:
		case SIPC_RX_PIO_WRSETUP_STATUS_DRQ_ERR:
		case SIPC_RX_FIS_STATUS_BSY_BIT_ERR:
		case SIPC_RX_WRSETUP_LEN_ODD_ERR:
		case SIPC_RX_WRSETUP_LEN_ZERO_ERR:
		case SIPC_RX_WRDATA_LEN_NOT_MATCH_ERR:
		case SIPC_RX_SATA_UNEXP_FIS_ERR:
		case DMA_RX_DATA_SGL_OVERFLOW:
		case DMA_RX_DATA_OFFSET_ERR:
		case DMA_RX_SATA_FRAME_TYPE_ERR:
		case DMA_RX_UNEXP_RDFRAME_ERR:
		case DMA_RX_PIO_DATA_LEN_ERR:
		case DMA_RX_RDSETUP_STATUS_ERR:
		case DMA_RX_RDSETUP_STATUS_DRQ_ERR:
		case DMA_RX_RDSETUP_STATUS_BSY_ERR:
		case DMA_RX_RDSETUP_LEN_ODD_ERR:
		case DMA_RX_RDSETUP_LEN_ZERO_ERR:
		case DMA_RX_RDSETUP_LEN_OVER_ERR:
		case DMA_RX_RDSETUP_OFFSET_ERR:
		case DMA_RX_RDSETUP_ACTIVE_ERR:
		case DMA_RX_RDSETUP_ESTATUS_ERR:
		case DMA_RX_UNKNOWN_FRM_ERR:
		case TRANS_RX_SSP_FRM_LEN_ERR:
		case TRANS_TX_OPEN_CNX_ERR_STP_RESOURCES_BUSY:
		{
			slot->abort = 1;
			ts->stat = SAS_PHY_DOWN;
			break;
		}
		default:
		{
			ts->stat = SAS_PROTO_RESPONSE;
			break;
		}
		}
		hisi_sas_sata_done(task, slot);
	}
		break;
	default:
		break;
	}
}

static int
slot_complete_v2_hw(struct hisi_hba *hisi_hba, struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_device *sas_dev;
	struct device *dev = hisi_hba->dev;
	struct task_status_struct *ts;
	struct domain_device *device;
	enum exec_status sts;
	struct hisi_sas_complete_v2_hdr *complete_queue =
			hisi_hba->complete_hdr[slot->cmplt_queue];
	struct hisi_sas_complete_v2_hdr *complete_hdr =
			&complete_queue[slot->cmplt_queue_slot];
	unsigned long flags;
	int aborted;

	if (unlikely(!task || !task->lldd_task || !task->dev))
		return -EINVAL;

	ts = &task->task_status;
	device = task->dev;
	sas_dev = device->lldd_dev;

	spin_lock_irqsave(&task->task_state_lock, flags);
	aborted = task->task_state_flags & SAS_TASK_STATE_ABORTED;
	task->task_state_flags &=
		~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	memset(ts, 0, sizeof(*ts));
	ts->resp = SAS_TASK_COMPLETE;

	if (unlikely(aborted)) {
		dev_dbg(dev, "slot_complete: task(%p) aborted\n", task);
		ts->stat = SAS_ABORTED_TASK;
		spin_lock_irqsave(&hisi_hba->lock, flags);
		hisi_sas_slot_task_free(hisi_hba, task, slot);
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
		return ts->stat;
	}

	if (unlikely(!sas_dev)) {
		dev_dbg(dev, "slot complete: port has no device\n");
		ts->stat = SAS_PHY_DOWN;
		goto out;
	}

	/* Use SAS+TMF status codes */
	switch ((complete_hdr->dw0 & CMPLT_HDR_ABORT_STAT_MSK)
			>> CMPLT_HDR_ABORT_STAT_OFF) {
	case STAT_IO_ABORTED:
		/* this io has been aborted by abort command */
		ts->stat = SAS_ABORTED_TASK;
		goto out;
	case STAT_IO_COMPLETE:
		/* internal abort command complete */
		ts->stat = TMF_RESP_FUNC_SUCC;
		del_timer(&slot->internal_abort_timer);
		goto out;
	case STAT_IO_NO_DEVICE:
		ts->stat = TMF_RESP_FUNC_COMPLETE;
		del_timer(&slot->internal_abort_timer);
		goto out;
	case STAT_IO_NOT_VALID:
		/* abort single io, controller don't find
		 * the io need to abort
		 */
		ts->stat = TMF_RESP_FUNC_FAILED;
		del_timer(&slot->internal_abort_timer);
		goto out;
	default:
		break;
	}

	if ((complete_hdr->dw0 & CMPLT_HDR_ERX_MSK) &&
		(!(complete_hdr->dw0 & CMPLT_HDR_RSPNS_XFRD_MSK))) {
		u32 err_phase = (complete_hdr->dw0 & CMPLT_HDR_ERR_PHASE_MSK)
				>> CMPLT_HDR_ERR_PHASE_OFF;
		u32 *error_info = hisi_sas_status_buf_addr_mem(slot);

		/* Analyse error happens on which phase TX or RX */
		if (ERR_ON_TX_PHASE(err_phase))
			slot_err_v2_hw(hisi_hba, task, slot, 1);
		else if (ERR_ON_RX_PHASE(err_phase))
			slot_err_v2_hw(hisi_hba, task, slot, 2);

		if (ts->stat != SAS_DATA_UNDERRUN)
			dev_info(dev, "erroneous completion iptt=%d task=%p "
				"CQ hdr: 0x%x 0x%x 0x%x 0x%x "
				"Error info: 0x%x 0x%x 0x%x 0x%x\n",
				slot->idx, task,
				complete_hdr->dw0, complete_hdr->dw1,
				complete_hdr->act, complete_hdr->dw3,
				error_info[0], error_info[1],
				error_info[2], error_info[3]);

		if (unlikely(slot->abort))
			return ts->stat;
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
	{
		struct hisi_sas_status_buffer *status_buffer =
				hisi_sas_status_buf_addr_mem(slot);
		struct ssp_response_iu *iu = (struct ssp_response_iu *)
				&status_buffer->iu[0];

		sas_ssp_task_response(dev, task, iu);
		break;
	}
	case SAS_PROTOCOL_SMP:
	{
		struct scatterlist *sg_resp = &task->smp_task.smp_resp;
		void *to;

		ts->stat = SAM_STAT_GOOD;
		to = kmap_atomic(sg_page(sg_resp));

		dma_unmap_sg(dev, &task->smp_task.smp_resp, 1,
			     DMA_FROM_DEVICE);
		dma_unmap_sg(dev, &task->smp_task.smp_req, 1,
			     DMA_TO_DEVICE);
		memcpy(to + sg_resp->offset,
		       hisi_sas_status_buf_addr_mem(slot) +
		       sizeof(struct hisi_sas_err_record),
		       sg_dma_len(sg_resp));
		kunmap_atomic(to);
		break;
	}
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
	{
		ts->stat = SAM_STAT_GOOD;
		hisi_sas_sata_done(task, slot);
		break;
	}
	default:
		ts->stat = SAM_STAT_CHECK_CONDITION;
		break;
	}

	if (!slot->port->port_attached) {
		dev_warn(dev, "slot complete: port %d has removed\n",
			slot->port->sas_port.id);
		ts->stat = SAS_PHY_DOWN;
	}

out:
	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_sas_slot_task_free(hisi_hba, task, slot);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
	sts = ts->stat;

	if (task->task_done)
		task->task_done(task);

	return sts;
}

static int prep_ata_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct domain_device *device = task->dev;
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct asd_sas_port *sas_port = device->port;
	struct hisi_sas_port *port = to_hisi_sas_port(sas_port);
	u8 *buf_cmd;
	int has_data = 0, rc = 0, hdr_tag = 0;
	u32 dw1 = 0, dw2 = 0;

	/* create header */
	/* dw0 */
	hdr->dw0 = cpu_to_le32(port->id << CMD_HDR_PORT_OFF);
	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type))
		hdr->dw0 |= cpu_to_le32(3 << CMD_HDR_CMD_OFF);
	else
		hdr->dw0 |= cpu_to_le32(4 << CMD_HDR_CMD_OFF);

	/* dw1 */
	switch (task->data_dir) {
	case DMA_TO_DEVICE:
		has_data = 1;
		dw1 |= DIR_TO_DEVICE << CMD_HDR_DIR_OFF;
		break;
	case DMA_FROM_DEVICE:
		has_data = 1;
		dw1 |= DIR_TO_INI << CMD_HDR_DIR_OFF;
		break;
	default:
		dw1 &= ~CMD_HDR_DIR_MSK;
	}

	if ((task->ata_task.fis.command == ATA_CMD_DEV_RESET) &&
			(task->ata_task.fis.control & ATA_SRST))
		dw1 |= 1 << CMD_HDR_RESET_OFF;

	dw1 |= (hisi_sas_get_ata_protocol(
		&task->ata_task.fis, task->data_dir))
		<< CMD_HDR_FRAME_TYPE_OFF;
	dw1 |= sas_dev->device_id << CMD_HDR_DEV_ID_OFF;
	hdr->dw1 = cpu_to_le32(dw1);

	/* dw2 */
	if (task->ata_task.use_ncq && hisi_sas_get_ncq_tag(task, &hdr_tag)) {
		task->ata_task.fis.sector_count |= (u8) (hdr_tag << 3);
		dw2 |= hdr_tag << CMD_HDR_NCQ_TAG_OFF;
	}

	dw2 |= (HISI_SAS_MAX_STP_RESP_SZ / 4) << CMD_HDR_CFL_OFF |
			2 << CMD_HDR_SG_MOD_OFF;
	hdr->dw2 = cpu_to_le32(dw2);

	/* dw3 */
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	if (has_data) {
		rc = prep_prd_sge_v2_hw(hisi_hba, slot, hdr, task->scatter,
					slot->n_elem);
		if (rc)
			return rc;
	}

	hdr->data_transfer_len = cpu_to_le32(task->total_xfer_len);
	hdr->cmd_table_addr = cpu_to_le64(hisi_sas_cmd_hdr_addr_dma(slot));
	hdr->sts_buffer_addr = cpu_to_le64(hisi_sas_status_buf_addr_dma(slot));

	buf_cmd = hisi_sas_cmd_hdr_addr_mem(slot);

	if (likely(!task->ata_task.device_control_reg_update))
		task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));

	return 0;
}

static void hisi_sas_internal_abort_quirk_timeout(struct timer_list *t)
{
	struct hisi_sas_slot *slot = from_timer(slot, t, internal_abort_timer);
	struct hisi_sas_port *port = slot->port;
	struct asd_sas_port *asd_sas_port;
	struct asd_sas_phy *sas_phy;

	if (!port)
		return;

	asd_sas_port = &port->sas_port;

	/* Kick the hardware - send break command */
	list_for_each_entry(sas_phy, &asd_sas_port->phy_list, port_phy_el) {
		struct hisi_sas_phy *phy = sas_phy->lldd_phy;
		struct hisi_hba *hisi_hba = phy->hisi_hba;
		int phy_no = sas_phy->id;
		u32 link_dfx2;

		link_dfx2 = hisi_sas_phy_read32(hisi_hba, phy_no, LINK_DFX2);
		if ((link_dfx2 == LINK_DFX2_RCVR_HOLD_STS_MSK) ||
		    (link_dfx2 & LINK_DFX2_SEND_HOLD_STS_MSK)) {
			u32 txid_auto;

			txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no,
							TXID_AUTO);
			txid_auto |= TXID_AUTO_CTB_MSK;
			hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
					     txid_auto);
			return;
		}
	}
}

static int prep_abort_v2_hw(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	struct sas_task *task = slot->task;
	struct domain_device *dev = task->dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct hisi_sas_port *port = slot->port;
	struct timer_list *timer = &slot->internal_abort_timer;

	/* setup the quirk timer */
	timer_setup(timer, hisi_sas_internal_abort_quirk_timeout, 0);
	/* Set the timeout to 10ms less than internal abort timeout */
	mod_timer(timer, jiffies + msecs_to_jiffies(100));

	/* dw0 */
	hdr->dw0 = cpu_to_le32((5 << CMD_HDR_CMD_OFF) | /*abort*/
			       (port->id << CMD_HDR_PORT_OFF) |
			       (dev_is_sata(dev) <<
				CMD_HDR_ABORT_DEVICE_TYPE_OFF) |
			       (abort_flag << CMD_HDR_ABORT_FLAG_OFF));

	/* dw1 */
	hdr->dw1 = cpu_to_le32(device_id << CMD_HDR_DEV_ID_OFF);

	/* dw7 */
	hdr->dw7 = cpu_to_le32(tag_to_abort << CMD_HDR_ABORT_IPTT_OFF);
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	return 0;
}

static int phy_up_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	int i, res = IRQ_HANDLED;
	u32 port_id, link_rate;
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = hisi_hba->dev;
	u32 *frame_rcvd = (u32 *)sas_phy->frame_rcvd;
	struct sas_identify_frame *id = (struct sas_identify_frame *)frame_rcvd;

	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 1);

	if (is_sata_phy_v2_hw(hisi_hba, phy_no))
		goto end;

	if (phy_no == 8) {
		u32 port_state = hisi_sas_read32(hisi_hba, PORT_STATE);

		port_id = (port_state & PORT_STATE_PHY8_PORT_NUM_MSK) >>
			  PORT_STATE_PHY8_PORT_NUM_OFF;
		link_rate = (port_state & PORT_STATE_PHY8_CONN_RATE_MSK) >>
			    PORT_STATE_PHY8_CONN_RATE_OFF;
	} else {
		port_id = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
		port_id = (port_id >> (4 * phy_no)) & 0xf;
		link_rate = hisi_sas_read32(hisi_hba, PHY_CONN_RATE);
		link_rate = (link_rate >> (phy_no * 4)) & 0xf;
	}

	if (port_id == 0xf) {
		dev_err(dev, "phyup: phy%d invalid portid\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}

	for (i = 0; i < 6; i++) {
		u32 idaf = hisi_sas_phy_read32(hisi_hba, phy_no,
					       RX_IDAF_DWORD0 + (i * 4));
		frame_rcvd[i] = __swab32(idaf);
	}

	sas_phy->linkrate = link_rate;
	sas_phy->oob_mode = SAS_OOB_MODE;
	memcpy(sas_phy->attached_sas_addr, &id->sas_addr, SAS_ADDR_SIZE);
	dev_info(dev, "phyup: phy%d link_rate=%d\n", phy_no, link_rate);
	phy->port_id = port_id;
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	phy->phy_type |= PORT_TYPE_SAS;
	phy->phy_attached = 1;
	phy->identify.device_type = id->dev_type;
	phy->frame_rcvd_size =	sizeof(struct sas_identify_frame);
	if (phy->identify.device_type == SAS_END_DEVICE)
		phy->identify.target_port_protocols =
			SAS_PROTOCOL_SSP;
	else if (phy->identify.device_type != SAS_PHY_UNUSED) {
		phy->identify.target_port_protocols =
			SAS_PROTOCOL_SMP;
		if (!timer_pending(&hisi_hba->timer))
			set_link_timer_quirk(hisi_hba);
	}
	hisi_sas_notify_phy_event(phy, HISI_PHYE_PHY_UP);

end:
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_PHY_ENABLE_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 0);

	return res;
}

static bool check_any_wideports_v2_hw(struct hisi_hba *hisi_hba)
{
	u32 port_state;

	port_state = hisi_sas_read32(hisi_hba, PORT_STATE);
	if (port_state & 0x1ff)
		return true;

	return false;
}

static int phy_down_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	u32 phy_state, sl_ctrl, txid_auto;
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct hisi_sas_port *port = phy->port;
	struct device *dev = hisi_hba->dev;

	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 1);

	phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);
	dev_info(dev, "phydown: phy%d phy_state=0x%x\n", phy_no, phy_state);
	hisi_sas_phy_down(hisi_hba, phy_no, (phy_state & 1 << phy_no) ? 1 : 0);

	sl_ctrl = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL,
			     sl_ctrl & ~SL_CONTROL_CTA_MSK);
	if (port && !get_wideport_bitmap_v2_hw(hisi_hba, port->id))
		if (!check_any_wideports_v2_hw(hisi_hba) &&
				timer_pending(&hisi_hba->timer))
			del_timer(&hisi_hba->timer);

	txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
	hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
			     txid_auto | TXID_AUTO_CT3_MSK);

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0, CHL_INT0_NOT_RDY_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 0);

	return IRQ_HANDLED;
}

static irqreturn_t int_phy_updown_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_msk;
	int phy_no = 0;
	irqreturn_t res = IRQ_NONE;

	irq_msk = (hisi_sas_read32(hisi_hba, HGC_INVLD_DQE_INFO)
		   >> HGC_INVLD_DQE_INFO_FB_CH0_OFF) & 0x1ff;
	while (irq_msk) {
		if (irq_msk  & 1) {
			u32 reg_value = hisi_sas_phy_read32(hisi_hba, phy_no,
					    CHL_INT0);

			switch (reg_value & (CHL_INT0_NOT_RDY_MSK |
					CHL_INT0_SL_PHY_ENABLE_MSK)) {

			case CHL_INT0_SL_PHY_ENABLE_MSK:
				/* phy up */
				if (phy_up_v2_hw(phy_no, hisi_hba) ==
				    IRQ_HANDLED)
					res = IRQ_HANDLED;
				break;

			case CHL_INT0_NOT_RDY_MSK:
				/* phy down */
				if (phy_down_v2_hw(phy_no, hisi_hba) ==
				    IRQ_HANDLED)
					res = IRQ_HANDLED;
				break;

			case (CHL_INT0_NOT_RDY_MSK |
					CHL_INT0_SL_PHY_ENABLE_MSK):
				reg_value = hisi_sas_read32(hisi_hba,
						PHY_STATE);
				if (reg_value & BIT(phy_no)) {
					/* phy up */
					if (phy_up_v2_hw(phy_no, hisi_hba) ==
					    IRQ_HANDLED)
						res = IRQ_HANDLED;
				} else {
					/* phy down */
					if (phy_down_v2_hw(phy_no, hisi_hba) ==
					    IRQ_HANDLED)
						res = IRQ_HANDLED;
				}
				break;

			default:
				break;
			}

		}
		irq_msk >>= 1;
		phy_no++;
	}

	return res;
}

static void phy_bcast_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	u32 bcast_status;

	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 1);
	bcast_status = hisi_sas_phy_read32(hisi_hba, phy_no, RX_PRIMS_STATUS);
	if (bcast_status & RX_BCAST_CHG_MSK)
		sas_ha->notify_port_event(sas_phy, PORTE_BROADCAST_RCVD);
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_RX_BCST_ACK_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 0);
}

static const struct hisi_sas_hw_error port_ecc_axi_error[] = {
	{
		.irq_msk = BIT(CHL_INT1_DMAC_TX_ECC_ERR_OFF),
		.msg = "dmac_tx_ecc_bad_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_RX_ECC_ERR_OFF),
		.msg = "dmac_rx_ecc_bad_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_TX_AXI_WR_ERR_OFF),
		.msg = "dma_tx_axi_wr_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_TX_AXI_RD_ERR_OFF),
		.msg = "dma_tx_axi_rd_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_RX_AXI_WR_ERR_OFF),
		.msg = "dma_rx_axi_wr_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_RX_AXI_RD_ERR_OFF),
		.msg = "dma_rx_axi_rd_err",
	},
};

static irqreturn_t int_chnl_int_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	struct device *dev = hisi_hba->dev;
	u32 ent_msk, ent_tmp, irq_msk;
	int phy_no = 0;

	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK3);
	ent_tmp = ent_msk;
	ent_msk |= ENT_INT_SRC_MSK3_ENT95_MSK_MSK;
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_msk);

	irq_msk = (hisi_sas_read32(hisi_hba, HGC_INVLD_DQE_INFO) >>
			HGC_INVLD_DQE_INFO_FB_CH3_OFF) & 0x1ff;

	while (irq_msk) {
		u32 irq_value0 = hisi_sas_phy_read32(hisi_hba, phy_no,
						     CHL_INT0);
		u32 irq_value1 = hisi_sas_phy_read32(hisi_hba, phy_no,
						     CHL_INT1);
		u32 irq_value2 = hisi_sas_phy_read32(hisi_hba, phy_no,
						     CHL_INT2);

		if ((irq_msk & (1 << phy_no)) && irq_value1) {
			int i;

			for (i = 0; i < ARRAY_SIZE(port_ecc_axi_error); i++) {
				const struct hisi_sas_hw_error *error =
						&port_ecc_axi_error[i];

				if (!(irq_value1 & error->irq_msk))
					continue;

				dev_warn(dev, "%s error (phy%d 0x%x) found!\n",
					error->msg, phy_no, irq_value1);
				queue_work(hisi_hba->wq, &hisi_hba->rst_work);
			}

			hisi_sas_phy_write32(hisi_hba, phy_no,
					     CHL_INT1, irq_value1);
		}

		if ((irq_msk & (1 << phy_no)) && irq_value2) {
			struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];

			if (irq_value2 & BIT(CHL_INT2_SL_IDAF_TOUT_CONF_OFF)) {
				dev_warn(dev, "phy%d identify timeout\n",
						phy_no);
				hisi_sas_notify_phy_event(phy,
						HISI_PHYE_LINK_RESET);
			}

			hisi_sas_phy_write32(hisi_hba, phy_no,
						 CHL_INT2, irq_value2);
		}

		if ((irq_msk & (1 << phy_no)) && irq_value0) {
			if (irq_value0 & CHL_INT0_SL_RX_BCST_ACK_MSK)
				phy_bcast_v2_hw(phy_no, hisi_hba);

			hisi_sas_phy_write32(hisi_hba, phy_no,
					CHL_INT0, irq_value0
					& (~CHL_INT0_HOTPLUG_TOUT_MSK)
					& (~CHL_INT0_SL_PHY_ENABLE_MSK)
					& (~CHL_INT0_NOT_RDY_MSK));
		}
		irq_msk &= ~(1 << phy_no);
		phy_no++;
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_tmp);

	return IRQ_HANDLED;
}

static void
one_bit_ecc_error_process_v2_hw(struct hisi_hba *hisi_hba, u32 irq_value)
{
	struct device *dev = hisi_hba->dev;
	const struct hisi_sas_hw_error *ecc_error;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(one_bit_ecc_errors); i++) {
		ecc_error = &one_bit_ecc_errors[i];
		if (irq_value & ecc_error->irq_msk) {
			val = hisi_sas_read32(hisi_hba, ecc_error->reg);
			val &= ecc_error->msk;
			val >>= ecc_error->shift;
			dev_warn(dev, ecc_error->msg, val);
		}
	}
}

static void multi_bit_ecc_error_process_v2_hw(struct hisi_hba *hisi_hba,
		u32 irq_value)
{
	struct device *dev = hisi_hba->dev;
	const struct hisi_sas_hw_error *ecc_error;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(multi_bit_ecc_errors); i++) {
		ecc_error = &multi_bit_ecc_errors[i];
		if (irq_value & ecc_error->irq_msk) {
			val = hisi_sas_read32(hisi_hba, ecc_error->reg);
			val &= ecc_error->msk;
			val >>= ecc_error->shift;
			dev_err(dev, ecc_error->msg, irq_value, val);
			queue_work(hisi_hba->wq, &hisi_hba->rst_work);
		}
	}

	return;
}

static irqreturn_t fatal_ecc_int_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_value, irq_msk;

	irq_msk = hisi_sas_read32(hisi_hba, SAS_ECC_INTR_MSK);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, irq_msk | 0xffffffff);

	irq_value = hisi_sas_read32(hisi_hba, SAS_ECC_INTR);
	if (irq_value) {
		one_bit_ecc_error_process_v2_hw(hisi_hba, irq_value);
		multi_bit_ecc_error_process_v2_hw(hisi_hba, irq_value);
	}

	hisi_sas_write32(hisi_hba, SAS_ECC_INTR, irq_value);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, irq_msk);

	return IRQ_HANDLED;
}

static const struct hisi_sas_hw_error axi_error[] = {
	{ .msk = BIT(0), .msg = "IOST_AXI_W_ERR" },
	{ .msk = BIT(1), .msg = "IOST_AXI_R_ERR" },
	{ .msk = BIT(2), .msg = "ITCT_AXI_W_ERR" },
	{ .msk = BIT(3), .msg = "ITCT_AXI_R_ERR" },
	{ .msk = BIT(4), .msg = "SATA_AXI_W_ERR" },
	{ .msk = BIT(5), .msg = "SATA_AXI_R_ERR" },
	{ .msk = BIT(6), .msg = "DQE_AXI_R_ERR" },
	{ .msk = BIT(7), .msg = "CQE_AXI_W_ERR" },
	{},
};

static const struct hisi_sas_hw_error fifo_error[] = {
	{ .msk = BIT(8),  .msg = "CQE_WINFO_FIFO" },
	{ .msk = BIT(9),  .msg = "CQE_MSG_FIFIO" },
	{ .msk = BIT(10), .msg = "GETDQE_FIFO" },
	{ .msk = BIT(11), .msg = "CMDP_FIFO" },
	{ .msk = BIT(12), .msg = "AWTCTRL_FIFO" },
	{},
};

static const struct hisi_sas_hw_error fatal_axi_errors[] = {
	{
		.irq_msk = BIT(ENT_INT_SRC3_WP_DEPTH_OFF),
		.msg = "write pointer and depth",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_IPTT_SLOT_NOMATCH_OFF),
		.msg = "iptt no match slot",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_RP_DEPTH_OFF),
		.msg = "read pointer and depth",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_AXI_OFF),
		.reg = HGC_AXI_FIFO_ERR_INFO,
		.sub = axi_error,
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_FIFO_OFF),
		.reg = HGC_AXI_FIFO_ERR_INFO,
		.sub = fifo_error,
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_LM_OFF),
		.msg = "LM add/fetch list",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_ABT_OFF),
		.msg = "SAS_HGC_ABT fetch LM list",
	},
};

static irqreturn_t fatal_axi_int_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_value, irq_msk, err_value;
	struct device *dev = hisi_hba->dev;
	const struct hisi_sas_hw_error *axi_error;
	int i;

	irq_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK3);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, irq_msk | 0xfffffffe);

	irq_value = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);

	for (i = 0; i < ARRAY_SIZE(fatal_axi_errors); i++) {
		axi_error = &fatal_axi_errors[i];
		if (!(irq_value & axi_error->irq_msk))
			continue;

		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 1 << axi_error->shift);
		if (axi_error->sub) {
			const struct hisi_sas_hw_error *sub = axi_error->sub;

			err_value = hisi_sas_read32(hisi_hba, axi_error->reg);
			for (; sub->msk || sub->msg; sub++) {
				if (!(err_value & sub->msk))
					continue;
				dev_err(dev, "%s (0x%x) found!\n",
					 sub->msg, irq_value);
				queue_work(hisi_hba->wq, &hisi_hba->rst_work);
			}
		} else {
			dev_err(dev, "%s (0x%x) found!\n",
				 axi_error->msg, irq_value);
			queue_work(hisi_hba->wq, &hisi_hba->rst_work);
		}
	}

	if (irq_value & BIT(ENT_INT_SRC3_ITC_INT_OFF)) {
		u32 reg_val = hisi_sas_read32(hisi_hba, ITCT_CLR);
		u32 dev_id = reg_val & ITCT_DEV_MSK;
		struct hisi_sas_device *sas_dev = &hisi_hba->devices[dev_id];

		hisi_sas_write32(hisi_hba, ITCT_CLR, 0);
		dev_dbg(dev, "clear ITCT ok\n");
		complete(sas_dev->completion);
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, irq_value);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, irq_msk);

	return IRQ_HANDLED;
}

static void cq_tasklet_v2_hw(unsigned long val)
{
	struct hisi_sas_cq *cq = (struct hisi_sas_cq *)val;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	struct hisi_sas_slot *slot;
	struct hisi_sas_itct *itct;
	struct hisi_sas_complete_v2_hdr *complete_queue;
	u32 rd_point = cq->rd_point, wr_point, dev_id;
	int queue = cq->id;
	struct hisi_sas_dq *dq = &hisi_hba->dq[queue];

	if (unlikely(hisi_hba->reject_stp_links_msk))
		phys_try_accept_stp_links_v2_hw(hisi_hba);

	complete_queue = hisi_hba->complete_hdr[queue];

	spin_lock(&dq->lock);
	wr_point = hisi_sas_read32(hisi_hba, COMPL_Q_0_WR_PTR +
				   (0x14 * queue));

	while (rd_point != wr_point) {
		struct hisi_sas_complete_v2_hdr *complete_hdr;
		int iptt;

		complete_hdr = &complete_queue[rd_point];

		/* Check for NCQ completion */
		if (complete_hdr->act) {
			u32 act_tmp = complete_hdr->act;
			int ncq_tag_count = ffs(act_tmp);

			dev_id = (complete_hdr->dw1 & CMPLT_HDR_DEV_ID_MSK) >>
				 CMPLT_HDR_DEV_ID_OFF;
			itct = &hisi_hba->itct[dev_id];

			/* The NCQ tags are held in the itct header */
			while (ncq_tag_count) {
				__le64 *ncq_tag = &itct->qw4_15[0];

				ncq_tag_count -= 1;
				iptt = (ncq_tag[ncq_tag_count / 5]
					>> (ncq_tag_count % 5) * 12) & 0xfff;

				slot = &hisi_hba->slot_info[iptt];
				slot->cmplt_queue_slot = rd_point;
				slot->cmplt_queue = queue;
				slot_complete_v2_hw(hisi_hba, slot);

				act_tmp &= ~(1 << ncq_tag_count);
				ncq_tag_count = ffs(act_tmp);
			}
		} else {
			iptt = (complete_hdr->dw1) & CMPLT_HDR_IPTT_MSK;
			slot = &hisi_hba->slot_info[iptt];
			slot->cmplt_queue_slot = rd_point;
			slot->cmplt_queue = queue;
			slot_complete_v2_hw(hisi_hba, slot);
		}

		if (++rd_point >= HISI_SAS_QUEUE_SLOTS)
			rd_point = 0;
	}

	/* update rd_point */
	cq->rd_point = rd_point;
	hisi_sas_write32(hisi_hba, COMPL_Q_0_RD_PTR + (0x14 * queue), rd_point);
	spin_unlock(&dq->lock);
}

static irqreturn_t cq_interrupt_v2_hw(int irq_no, void *p)
{
	struct hisi_sas_cq *cq = p;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	int queue = cq->id;

	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 1 << queue);

	tasklet_schedule(&cq->tasklet);

	return IRQ_HANDLED;
}

static irqreturn_t sata_int_v2_hw(int irq_no, void *p)
{
	struct hisi_sas_phy *phy = p;
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = hisi_hba->dev;
	struct	hisi_sas_initial_fis *initial_fis;
	struct dev_to_host_fis *fis;
	u32 ent_tmp, ent_msk, ent_int, port_id, link_rate, hard_phy_linkrate;
	irqreturn_t res = IRQ_HANDLED;
	u8 attached_sas_addr[SAS_ADDR_SIZE] = {0};
	int phy_no, offset;

	phy_no = sas_phy->id;
	initial_fis = &hisi_hba->initial_fis[phy_no];
	fis = &initial_fis->fis;

	offset = 4 * (phy_no / 4);
	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK1 + offset);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1 + offset,
			 ent_msk | 1 << ((phy_no % 4) * 8));

	ent_int = hisi_sas_read32(hisi_hba, ENT_INT_SRC1 + offset);
	ent_tmp = ent_int & (1 << (ENT_INT_SRC1_D2H_FIS_CH1_OFF *
			     (phy_no % 4)));
	ent_int >>= ENT_INT_SRC1_D2H_FIS_CH1_OFF * (phy_no % 4);
	if ((ent_int & ENT_INT_SRC1_D2H_FIS_CH0_MSK) == 0) {
		dev_warn(dev, "sata int: phy%d did not receive FIS\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}

	/* check ERR bit of Status Register */
	if (fis->status & ATA_ERR) {
		dev_warn(dev, "sata int: phy%d FIS status: 0x%x\n", phy_no,
				fis->status);
		disable_phy_v2_hw(hisi_hba, phy_no);
		enable_phy_v2_hw(hisi_hba, phy_no);
		res = IRQ_NONE;
		goto end;
	}

	if (unlikely(phy_no == 8)) {
		u32 port_state = hisi_sas_read32(hisi_hba, PORT_STATE);

		port_id = (port_state & PORT_STATE_PHY8_PORT_NUM_MSK) >>
			  PORT_STATE_PHY8_PORT_NUM_OFF;
		link_rate = (port_state & PORT_STATE_PHY8_CONN_RATE_MSK) >>
			    PORT_STATE_PHY8_CONN_RATE_OFF;
	} else {
		port_id = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
		port_id = (port_id >> (4 * phy_no)) & 0xf;
		link_rate = hisi_sas_read32(hisi_hba, PHY_CONN_RATE);
		link_rate = (link_rate >> (phy_no * 4)) & 0xf;
	}

	if (port_id == 0xf) {
		dev_err(dev, "sata int: phy%d invalid portid\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}

	sas_phy->linkrate = link_rate;
	hard_phy_linkrate = hisi_sas_phy_read32(hisi_hba, phy_no,
						HARD_PHY_LINKRATE);
	phy->maximum_linkrate = hard_phy_linkrate & 0xf;
	phy->minimum_linkrate = (hard_phy_linkrate >> 4) & 0xf;

	sas_phy->oob_mode = SATA_OOB_MODE;
	/* Make up some unique SAS address */
	attached_sas_addr[0] = 0x50;
	attached_sas_addr[7] = phy_no;
	memcpy(sas_phy->attached_sas_addr, attached_sas_addr, SAS_ADDR_SIZE);
	memcpy(sas_phy->frame_rcvd, fis, sizeof(struct dev_to_host_fis));
	dev_info(dev, "sata int phyup: phy%d link_rate=%d\n", phy_no, link_rate);
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	phy->port_id = port_id;
	phy->phy_type |= PORT_TYPE_SATA;
	phy->phy_attached = 1;
	phy->identify.device_type = SAS_SATA_DEV;
	phy->frame_rcvd_size = sizeof(struct dev_to_host_fis);
	phy->identify.target_port_protocols = SAS_PROTOCOL_SATA;
	hisi_sas_notify_phy_event(phy, HISI_PHYE_PHY_UP);

end:
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1 + offset, ent_tmp);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1 + offset, ent_msk);

	return res;
}

static irq_handler_t phy_interrupts[HISI_SAS_PHY_INT_NR] = {
	int_phy_updown_v2_hw,
	int_chnl_int_v2_hw,
};

static irq_handler_t fatal_interrupts[HISI_SAS_FATAL_INT_NR] = {
	fatal_ecc_int_v2_hw,
	fatal_axi_int_v2_hw
};

/**
 * There is a limitation in the hip06 chipset that we need
 * to map in all mbigen interrupts, even if they are not used.
 */
static int interrupt_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct platform_device *pdev = hisi_hba->platform_dev;
	struct device *dev = &pdev->dev;
	int irq, rc, irq_map[128];
	int i, phy_no, fatal_no, queue_no, k;

	for (i = 0; i < 128; i++)
		irq_map[i] = platform_get_irq(pdev, i);

	for (i = 0; i < HISI_SAS_PHY_INT_NR; i++) {
		irq = irq_map[i + 1]; /* Phy up/down is irq1 */
		rc = devm_request_irq(dev, irq, phy_interrupts[i], 0,
				      DRV_NAME " phy", hisi_hba);
		if (rc) {
			dev_err(dev, "irq init: could not request "
				"phy interrupt %d, rc=%d\n",
				irq, rc);
			rc = -ENOENT;
			goto free_phy_int_irqs;
		}
	}

	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];

		irq = irq_map[phy_no + 72];
		rc = devm_request_irq(dev, irq, sata_int_v2_hw, 0,
				      DRV_NAME " sata", phy);
		if (rc) {
			dev_err(dev, "irq init: could not request "
				"sata interrupt %d, rc=%d\n",
				irq, rc);
			rc = -ENOENT;
			goto free_sata_int_irqs;
		}
	}

	for (fatal_no = 0; fatal_no < HISI_SAS_FATAL_INT_NR; fatal_no++) {
		irq = irq_map[fatal_no + 81];
		rc = devm_request_irq(dev, irq, fatal_interrupts[fatal_no], 0,
				      DRV_NAME " fatal", hisi_hba);
		if (rc) {
			dev_err(dev,
				"irq init: could not request fatal interrupt %d, rc=%d\n",
				irq, rc);
			rc = -ENOENT;
			goto free_fatal_int_irqs;
		}
	}

	for (queue_no = 0; queue_no < hisi_hba->queue_count; queue_no++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[queue_no];
		struct tasklet_struct *t = &cq->tasklet;

		irq = irq_map[queue_no + 96];
		rc = devm_request_irq(dev, irq, cq_interrupt_v2_hw, 0,
				      DRV_NAME " cq", cq);
		if (rc) {
			dev_err(dev,
				"irq init: could not request cq interrupt %d, rc=%d\n",
				irq, rc);
			rc = -ENOENT;
			goto free_cq_int_irqs;
		}
		tasklet_init(t, cq_tasklet_v2_hw, (unsigned long)cq);
	}

	return 0;

free_cq_int_irqs:
	for (k = 0; k < queue_no; k++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[k];

		free_irq(irq_map[k + 96], cq);
		tasklet_kill(&cq->tasklet);
	}
free_fatal_int_irqs:
	for (k = 0; k < fatal_no; k++)
		free_irq(irq_map[k + 81], hisi_hba);
free_sata_int_irqs:
	for (k = 0; k < phy_no; k++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[k];

		free_irq(irq_map[k + 72], phy);
	}
free_phy_int_irqs:
	for (k = 0; k < i; k++)
		free_irq(irq_map[k + 1], hisi_hba);
	return rc;
}

static int hisi_sas_v2_init(struct hisi_hba *hisi_hba)
{
	int rc;

	memset(hisi_hba->sata_dev_bitmap, 0, sizeof(hisi_hba->sata_dev_bitmap));

	rc = hw_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	rc = interrupt_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	return 0;
}

static void interrupt_disable_v2_hw(struct hisi_hba *hisi_hba)
{
	struct platform_device *pdev = hisi_hba->platform_dev;
	int i;

	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK + 0x4 * i, 0x1);

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0xffffffff);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xffffffff);

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0xffffffff);
	}

	for (i = 0; i < 128; i++)
		synchronize_irq(platform_get_irq(pdev, i));
}


static u32 get_phys_state_v2_hw(struct hisi_hba *hisi_hba)
{
	return hisi_sas_read32(hisi_hba, PHY_STATE);
}

static int soft_reset_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int rc, cnt;

	interrupt_disable_v2_hw(hisi_hba);
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0x0);
	hisi_sas_kill_tasklets(hisi_hba);

	hisi_sas_stop_phys(hisi_hba);

	mdelay(10);

	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE + AM_CTRL_GLOBAL, 0x1);

	/* wait until bus idle */
	cnt = 0;
	while (1) {
		u32 status = hisi_sas_read32_relaxed(hisi_hba,
				AXI_MASTER_CFG_BASE + AM_CURR_TRANS_RETURN);

		if (status == 0x3)
			break;

		udelay(10);
		if (cnt++ > 10) {
			dev_err(dev, "wait axi bus state to idle timeout!\n");
			return -1;
		}
	}

	hisi_sas_init_mem(hisi_hba);

	rc = hw_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	phys_reject_stp_links_v2_hw(hisi_hba);

	return 0;
}

static int write_gpio_v2_hw(struct hisi_hba *hisi_hba, u8 reg_type,
			u8 reg_index, u8 reg_count, u8 *write_data)
{
	struct device *dev = hisi_hba->dev;
	int phy_no, count;

	if (!hisi_hba->sgpio_regs)
		return -EOPNOTSUPP;

	switch (reg_type) {
	case SAS_GPIO_REG_TX:
		count = reg_count * 4;
		count = min(count, hisi_hba->n_phy);

		for (phy_no = 0; phy_no < count; phy_no++) {
			/*
			 * GPIO_TX[n] register has the highest numbered drive
			 * of the four in the first byte and the lowest
			 * numbered drive in the fourth byte.
			 * See SFF-8485 Rev. 0.7 Table 24.
			 */
			void __iomem  *reg_addr = hisi_hba->sgpio_regs +
					reg_index * 4 + phy_no;
			int data_idx = phy_no + 3 - (phy_no % 4) * 2;

			writeb(write_data[data_idx], reg_addr);
		}

		break;
	default:
		dev_err(dev, "write gpio: unsupported or bad reg type %d\n",
				reg_type);
		return -EINVAL;
	}

	return 0;
}

static const struct hisi_sas_hw hisi_sas_v2_hw = {
	.hw_init = hisi_sas_v2_init,
	.setup_itct = setup_itct_v2_hw,
	.slot_index_alloc = slot_index_alloc_quirk_v2_hw,
	.alloc_dev = alloc_dev_quirk_v2_hw,
	.sl_notify = sl_notify_v2_hw,
	.get_wideport_bitmap = get_wideport_bitmap_v2_hw,
	.clear_itct = clear_itct_v2_hw,
	.free_device = free_device_v2_hw,
	.prep_smp = prep_smp_v2_hw,
	.prep_ssp = prep_ssp_v2_hw,
	.prep_stp = prep_ata_v2_hw,
	.prep_abort = prep_abort_v2_hw,
	.get_free_slot = get_free_slot_v2_hw,
	.start_delivery = start_delivery_v2_hw,
	.slot_complete = slot_complete_v2_hw,
	.phys_init = phys_init_v2_hw,
	.phy_start = start_phy_v2_hw,
	.phy_disable = disable_phy_v2_hw,
	.phy_hard_reset = phy_hard_reset_v2_hw,
	.get_events = phy_get_events_v2_hw,
	.phy_set_linkrate = phy_set_linkrate_v2_hw,
	.phy_get_max_linkrate = phy_get_max_linkrate_v2_hw,
	.max_command_entries = HISI_SAS_COMMAND_ENTRIES_V2_HW,
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v2_hdr),
	.soft_reset = soft_reset_v2_hw,
	.get_phys_state = get_phys_state_v2_hw,
	.write_gpio = write_gpio_v2_hw,
};

static int hisi_sas_v2_probe(struct platform_device *pdev)
{
	/*
	 * Check if we should defer the probe before we probe the
	 * upper layer, as it's hard to defer later on.
	 */
	int ret = platform_get_irq(pdev, 0);

	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "cannot obtain irq\n");
		return ret;
	}

	return hisi_sas_probe(pdev, &hisi_sas_v2_hw);
}

static int hisi_sas_v2_remove(struct platform_device *pdev)
{
	struct sas_ha_struct *sha = platform_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	if (timer_pending(&hisi_hba->timer))
		del_timer(&hisi_hba->timer);

	hisi_sas_kill_tasklets(hisi_hba);

	return hisi_sas_remove(pdev);
}

static const struct of_device_id sas_v2_of_match[] = {
	{ .compatible = "hisilicon,hip06-sas-v2",},
	{ .compatible = "hisilicon,hip07-sas-v2",},
	{},
};
MODULE_DEVICE_TABLE(of, sas_v2_of_match);

static const struct acpi_device_id sas_v2_acpi_match[] = {
	{ "HISI0162", 0 },
	{ }
};

MODULE_DEVICE_TABLE(acpi, sas_v2_acpi_match);

static struct platform_driver hisi_sas_v2_driver = {
	.probe = hisi_sas_v2_probe,
	.remove = hisi_sas_v2_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sas_v2_of_match,
		.acpi_match_table = ACPI_PTR(sas_v2_acpi_match),
	},
};

module_platform_driver(hisi_sas_v2_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v2 hw driver");
MODULE_ALIAS("platform:" DRV_NAME);
