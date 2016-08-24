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
#define HGC_INVLD_DQE_INFO		0x148
#define HGC_INVLD_DQE_INFO_FB_CH0_OFF	9
#define HGC_INVLD_DQE_INFO_FB_CH0_MSK	(0x1 << HGC_INVLD_DQE_INFO_FB_CH0_OFF)
#define HGC_INVLD_DQE_INFO_FB_CH3_OFF	18
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
#define ENT_INT_SRC3_ITC_INT_OFF	15
#define ENT_INT_SRC3_ITC_INT_MSK	(0x1 << ENT_INT_SRC3_ITC_INT_OFF)
#define ENT_INT_SRC_MSK1		0x1c4
#define ENT_INT_SRC_MSK2		0x1c8
#define ENT_INT_SRC_MSK3		0x1cc
#define ENT_INT_SRC_MSK3_ENT95_MSK_OFF	31
#define ENT_INT_SRC_MSK3_ENT95_MSK_MSK	(0x1 << ENT_INT_SRC_MSK3_ENT95_MSK_OFF)
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
#define TX_ID_DWORD0			(PORT_BASE + 0x9c)
#define TX_ID_DWORD1			(PORT_BASE + 0xa0)
#define TX_ID_DWORD2			(PORT_BASE + 0xa4)
#define TX_ID_DWORD3			(PORT_BASE + 0xa8)
#define TX_ID_DWORD4			(PORT_BASE + 0xaC)
#define TX_ID_DWORD5			(PORT_BASE + 0xb0)
#define TX_ID_DWORD6			(PORT_BASE + 0xb4)
#define RX_IDAF_DWORD0			(PORT_BASE + 0xc4)
#define RX_IDAF_DWORD1			(PORT_BASE + 0xc8)
#define RX_IDAF_DWORD2			(PORT_BASE + 0xcc)
#define RX_IDAF_DWORD3			(PORT_BASE + 0xd0)
#define RX_IDAF_DWORD4			(PORT_BASE + 0xd4)
#define RX_IDAF_DWORD5			(PORT_BASE + 0xd8)
#define RX_IDAF_DWORD6			(PORT_BASE + 0xdc)
#define RXOP_CHECK_CFG_H		(PORT_BASE + 0xfc)
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
#define CHL_INT2			(PORT_BASE + 0x1bc)
#define CHL_INT0_MSK			(PORT_BASE + 0x1c0)
#define CHL_INT1_MSK			(PORT_BASE + 0x1c4)
#define CHL_INT2_MSK			(PORT_BASE + 0x1c8)
#define CHL_INT_COAL_EN			(PORT_BASE + 0x1d0)
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

enum {
	HISI_SAS_PHY_PHY_UPDOWN,
	HISI_SAS_PHY_CHNL_INT,
	HISI_SAS_PHY_INT_NR
};

enum {
	TRANS_TX_FAIL_BASE = 0x0, /* dw0 */
	TRANS_RX_FAIL_BASE = 0x100, /* dw1 */
	DMA_TX_ERR_BASE = 0x200, /* dw2 bit 15-0 */
	SIPC_RX_ERR_BASE = 0x300, /* dw2 bit 31-16*/
	DMA_RX_ERR_BASE = 0x400, /* dw3 */

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
	TRANS_RX_ERR_WITH_RXFRAME_CRC_ERR = TRANS_RX_FAIL_BASE, /* 0x100 */
	TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR, /* 0x101 for sata/stp */
	TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM, /* 0x102 for ssp/smp */
	/*IO_ERR_WITH_RXFIS_8B10B_CODE_ERR, [> 0x102 <] for sata/stp */
	TRANS_RX_ERR_WITH_RXFIS_DECODE_ERROR, /* 0x103 for sata/stp */
	TRANS_RX_ERR_WITH_RXFIS_CRC_ERR, /* 0x104 for sata/stp */
	TRANS_RX_ERR_WITH_RXFRAME_LENGTH_OVERRUN, /* 0x105 for smp */
	/*IO_ERR_WITH_RXFIS_TX SYNCP, [> 0x105 <] for sata/stp */
	TRANS_RX_ERR_WITH_RXFIS_RX_SYNCP, /* 0x106 for sata/stp*/
	TRANS_RX_ERR_WITH_LINK_BUF_OVERRUN, /* 0x107 */
	TRANS_RX_ERR_WITH_BREAK_TIMEOUT, /* 0x108 */
	TRANS_RX_ERR_WITH_BREAK_REQUEST, /* 0x109 */
	TRANS_RX_ERR_WITH_BREAK_RECEVIED, /* 0x10a */
	RESERVED1, /* 0x10b */
	TRANS_RX_ERR_WITH_CLOSE_NORMAL, /* 0x10c */
	TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE, /* 0x10d */
	TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT, /* 0x10e */
	TRANS_RX_ERR_WITH_CLOSE_COMINIT, /* 0x10f */
	TRANS_RX_ERR_WITH_DATA_LEN0, /* 0x110 for ssp/smp */
	TRANS_RX_ERR_WITH_BAD_HASH, /* 0x111 for ssp */
	/*IO_RX_ERR_WITH_FIS_TOO_SHORT, [> 0x111 <] for sata/stp */
	TRANS_RX_XRDY_WLEN_ZERO_ERR, /* 0x112 for ssp*/
	/*IO_RX_ERR_WITH_FIS_TOO_LONG, [> 0x112 <] for sata/stp */
	TRANS_RX_SSP_FRM_LEN_ERR, /* 0x113 for ssp */
	/*IO_RX_ERR_WITH_SATA_DEVICE_LOST, [> 0x113 <] for sata */
	RESERVED2, /* 0x114 */
	RESERVED3, /* 0x115 */
	RESERVED4, /* 0x116 */
	RESERVED5, /* 0x117 */
	TRANS_RX_ERR_WITH_BAD_FRM_TYPE, /* 0x118 */
	TRANS_RX_SMP_FRM_LEN_ERR, /* 0x119 */
	TRANS_RX_SMP_RESP_TIMEOUT_ERR, /* 0x11a */
	RESERVED6, /* 0x11b */
	RESERVED7, /* 0x11c */
	RESERVED8, /* 0x11d */
	RESERVED9, /* 0x11e */
	TRANS_RX_R_ERR, /* 0x11f */

	/* dma tx */
	DMA_TX_DIF_CRC_ERR = DMA_TX_ERR_BASE, /* 0x200 */
	DMA_TX_DIF_APP_ERR, /* 0x201 */
	DMA_TX_DIF_RPP_ERR, /* 0x202 */
	DMA_TX_DATA_SGL_OVERFLOW, /* 0x203 */
	DMA_TX_DIF_SGL_OVERFLOW, /* 0x204 */
	DMA_TX_UNEXP_XFER_ERR, /* 0x205 */
	DMA_TX_UNEXP_RETRANS_ERR, /* 0x206 */
	DMA_TX_XFER_LEN_OVERFLOW, /* 0x207 */
	DMA_TX_XFER_OFFSET_ERR, /* 0x208 */
	DMA_TX_RAM_ECC_ERR, /* 0x209 */
	DMA_TX_DIF_LEN_ALIGN_ERR, /* 0x20a */

	/* sipc rx */
	SIPC_RX_FIS_STATUS_ERR_BIT_VLD = SIPC_RX_ERR_BASE, /* 0x300 */
	SIPC_RX_PIO_WRSETUP_STATUS_DRQ_ERR, /* 0x301 */
	SIPC_RX_FIS_STATUS_BSY_BIT_ERR, /* 0x302 */
	SIPC_RX_WRSETUP_LEN_ODD_ERR, /* 0x303 */
	SIPC_RX_WRSETUP_LEN_ZERO_ERR, /* 0x304 */
	SIPC_RX_WRDATA_LEN_NOT_MATCH_ERR, /* 0x305 */
	SIPC_RX_NCQ_WRSETUP_OFFSET_ERR, /* 0x306 */
	SIPC_RX_NCQ_WRSETUP_AUTO_ACTIVE_ERR, /* 0x307 */
	SIPC_RX_SATA_UNEXP_FIS_ERR, /* 0x308 */
	SIPC_RX_WRSETUP_ESTATUS_ERR, /* 0x309 */
	SIPC_RX_DATA_UNDERFLOW_ERR, /* 0x30a */

	/* dma rx */
	DMA_RX_DIF_CRC_ERR = DMA_RX_ERR_BASE, /* 0x400 */
	DMA_RX_DIF_APP_ERR, /* 0x401 */
	DMA_RX_DIF_RPP_ERR, /* 0x402 */
	DMA_RX_DATA_SGL_OVERFLOW, /* 0x403 */
	DMA_RX_DIF_SGL_OVERFLOW, /* 0x404 */
	DMA_RX_DATA_LEN_OVERFLOW, /* 0x405 */
	DMA_RX_DATA_LEN_UNDERFLOW, /* 0x406 */
	DMA_RX_DATA_OFFSET_ERR, /* 0x407 */
	RESERVED10, /* 0x408 */
	DMA_RX_SATA_FRAME_TYPE_ERR, /* 0x409 */
	DMA_RX_RESP_BUF_OVERFLOW, /* 0x40a */
	DMA_RX_UNEXP_RETRANS_RESP_ERR, /* 0x40b */
	DMA_RX_UNEXP_NORM_RESP_ERR, /* 0x40c */
	DMA_RX_UNEXP_RDFRAME_ERR, /* 0x40d */
	DMA_RX_PIO_DATA_LEN_ERR, /* 0x40e */
	DMA_RX_RDSETUP_STATUS_ERR, /* 0x40f */
	DMA_RX_RDSETUP_STATUS_DRQ_ERR, /* 0x410 */
	DMA_RX_RDSETUP_STATUS_BSY_ERR, /* 0x411 */
	DMA_RX_RDSETUP_LEN_ODD_ERR, /* 0x412 */
	DMA_RX_RDSETUP_LEN_ZERO_ERR, /* 0x413 */
	DMA_RX_RDSETUP_LEN_OVER_ERR, /* 0x414 */
	DMA_RX_RDSETUP_OFFSET_ERR, /* 0x415 */
	DMA_RX_RDSETUP_ACTIVE_ERR, /* 0x416 */
	DMA_RX_RDSETUP_ESTATUS_ERR, /* 0x417 */
	DMA_RX_RAM_ECC_ERR, /* 0x418 */
	DMA_RX_UNKNOWN_FRM_ERR, /* 0x419 */
};

#define HISI_SAS_COMMAND_ENTRIES_V2_HW 4096

#define DIR_NO_DATA 0
#define DIR_TO_INI 1
#define DIR_TO_DEVICE 2
#define DIR_RESERVED 3

#define SATA_PROTOCOL_NONDATA		0x1
#define SATA_PROTOCOL_PIO		0x2
#define SATA_PROTOCOL_DMA		0x4
#define SATA_PROTOCOL_FPDMA		0x8
#define SATA_PROTOCOL_ATAPI		0x10

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
	unsigned int index = 0;
	void *bitmap = hisi_hba->slot_index_tags;
	int sata_dev = dev_is_sata(device);

	while (1) {
		index = find_next_zero_bit(bitmap, hisi_hba->slot_index_count,
					   index);
		if (index >= hisi_hba->slot_index_count)
			return -SAS_QUEUE_FULL;
		/*
		 * SAS IPTT bit0 should be 1
		 */
		if (sata_dev || (index & 1))
			break;
		index++;
	}

	set_bit(index, bitmap);
	*slot_idx = index;
	return 0;
}

static struct
hisi_sas_device *alloc_dev_quirk_v2_hw(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = device->port->ha->lldd_ha;
	struct hisi_sas_device *sas_dev = NULL;
	int i, sata_dev = dev_is_sata(device);

	spin_lock(&hisi_hba->lock);
	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		/*
		 * SATA device id bit0 should be 0
		 */
		if (sata_dev && (i & 1))
			continue;
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
			hisi_hba->devices[i].device_id = i;
			sas_dev = &hisi_hba->devices[i];
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
			sas_dev->dev_type = device->dev_type;
			sas_dev->hisi_hba = hisi_hba;
			sas_dev->sas_device = device;
			break;
		}
	}
	spin_unlock(&hisi_hba->lock);

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
			identify_buffer[2]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD2,
			identify_buffer[1]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD3,
			identify_buffer[4]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD4,
			identify_buffer[3]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD5,
			__swab32(identify_buffer[5]));
}

static void init_id_frame_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		config_id_frame_v2_hw(hisi_hba, i);
}

static void setup_itct_v2_hw(struct hisi_hba *hisi_hba,
			     struct hisi_sas_device *sas_dev)
{
	struct domain_device *device = sas_dev->sas_device;
	struct device *dev = &hisi_hba->pdev->dev;
	u64 qw0, device_id = sas_dev->device_id;
	struct hisi_sas_itct *itct = &hisi_hba->itct[device_id];
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_port *port = device->port->lldd_port;

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
		(port->id << ITCT_HDR_PORT_ID_OFF));
	itct->qw0 = cpu_to_le64(qw0);

	/* qw1 */
	memcpy(&itct->sas_addr, device->sas_addr, SAS_ADDR_SIZE);
	itct->sas_addr = __swab64(itct->sas_addr);

	/* qw2 */
	if (!dev_is_sata(device))
		itct->qw2 = cpu_to_le64((500ULL << ITCT_HDR_INLT_OFF) |
					(0x1ULL << ITCT_HDR_BITLT_OFF) |
					(0x32ULL << ITCT_HDR_MCTLT_OFF) |
					(0x1ULL << ITCT_HDR_RTOLT_OFF));
}

static void free_device_v2_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_device *sas_dev)
{
	u64 qw0, dev_id = sas_dev->device_id;
	struct device *dev = &hisi_hba->pdev->dev;
	struct hisi_sas_itct *itct = &hisi_hba->itct[dev_id];
	u32 reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
	int i;

	/* clear the itct interrupt state */
	if (ENT_INT_SRC3_ITC_INT_MSK & reg_val)
		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 ENT_INT_SRC3_ITC_INT_MSK);

	/* clear the itct int*/
	for (i = 0; i < 2; i++) {
		/* clear the itct table*/
		reg_val = hisi_sas_read32(hisi_hba, ITCT_CLR);
		reg_val |= ITCT_CLR_EN_MSK | (dev_id & ITCT_DEV_MSK);
		hisi_sas_write32(hisi_hba, ITCT_CLR, reg_val);

		udelay(10);
		reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
		if (ENT_INT_SRC3_ITC_INT_MSK & reg_val) {
			dev_dbg(dev, "got clear ITCT done interrupt\n");

			/* invalid the itct state*/
			qw0 = cpu_to_le64(itct->qw0);
			qw0 &= ~(1 << ITCT_HDR_VALID_OFF);
			hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
					 ENT_INT_SRC3_ITC_INT_MSK);
			hisi_hba->devices[dev_id].dev_type = SAS_PHY_UNUSED;
			hisi_hba->devices[dev_id].dev_status = HISI_SAS_DEV_NORMAL;

			/* clear the itct */
			hisi_sas_write32(hisi_hba, ITCT_CLR, 0);
			dev_dbg(dev, "clear ITCT ok\n");
			break;
		}
	}
}

static int reset_hw_v2_hw(struct hisi_hba *hisi_hba)
{
	int i, reset_val;
	u32 val;
	unsigned long end_time;
	struct device *dev = &hisi_hba->pdev->dev;

	/* The mask needs to be set depending on the number of phys */
	if (hisi_hba->n_phy == 9)
		reset_val = 0x1fffff;
	else
		reset_val = 0x7ffff;

	/* Disable all of the DQ */
	for (i = 0; i < HISI_SAS_MAX_QUEUES; i++)
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
	} else
		dev_warn(dev, "no reset method\n");

	return 0;
}

static void init_reg_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
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
	hisi_sas_write32(hisi_hba, HGC_SAS_TXFAIL_RETRY_CTRL, 0x108);
	hisi_sas_write32(hisi_hba, HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL, 0x7FF);
	hisi_sas_write32(hisi_hba, OPENA_WT_CONTI_TIME, 0x1);
	hisi_sas_write32(hisi_hba, I_T_NEXUS_LOSS_TIME, 0x1F4);
	hisi_sas_write32(hisi_hba, MAX_CON_TIME_LIMIT_TIME, 0x32);
	hisi_sas_write32(hisi_hba, BUS_INACTIVE_LIMIT_TIME, 0x1);
	hisi_sas_write32(hisi_hba, CFG_AGING_TIME, 0x1);
	hisi_sas_write32(hisi_hba, HGC_ERR_STAT_EN, 0x1);
	hisi_sas_write32(hisi_hba, HGC_GET_ITV_TIME, 0x1);
	hisi_sas_write32(hisi_hba, INT_COAL_EN, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT, 0x1);
	hisi_sas_write32(hisi_hba, ENT_INT_COAL_TIME, 0x1);
	hisi_sas_write32(hisi_hba, ENT_INT_COAL_CNT, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 0x0);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0x7efefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0x7efefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0x7ffffffe);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xfffff3c0);
	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK+0x4*i, 0);

	hisi_sas_write32(hisi_hba, AXI_AHB_CLK_CFG, 1);
	hisi_sas_write32(hisi_hba, HYPER_STREAM_ID_EN_CFG, 1);

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, PROG_PHY_LINK_RATE, 0x855);
		hisi_sas_phy_write32(hisi_hba, i, SAS_PHY_CTRL, 0x30b9908);
		hisi_sas_phy_write32(hisi_hba, i, SL_TOUT_CFG, 0x7d7d7d7d);
		hisi_sas_phy_write32(hisi_hba, i, DONE_RECEIVED_TIME, 0x10);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, RXOP_CHECK_CFG_H, 0x1000);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0x8ffffbff);
		hisi_sas_phy_write32(hisi_hba, i, SL_CFG, 0x23f801fc);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_DWS_RESET_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT_COAL_EN, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_OOB_RESTART_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL, 0x199B694);
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

static int hw_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	int rc;

	rc = reset_hw_v2_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "hisi_sas_reset_hw failed, rc=%d", rc);
		return rc;
	}

	msleep(100);
	init_reg_v2_hw(hisi_hba);

	init_id_frame_v2_hw(hisi_hba);

	return 0;
}

static void enable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg |= PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void disable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg &= ~PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void start_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	config_id_frame_v2_hw(hisi_hba, phy_no);
	config_phy_opt_mode_v2_hw(hisi_hba, phy_no);
	enable_phy_v2_hw(hisi_hba, phy_no);
}

static void stop_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	disable_phy_v2_hw(hisi_hba, phy_no);
}

static void phy_hard_reset_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	stop_phy_v2_hw(hisi_hba, phy_no);
	msleep(100);
	start_phy_v2_hw(hisi_hba, phy_no);
}

static void start_phys_v2_hw(unsigned long data)
{
	struct hisi_hba *hisi_hba = (struct hisi_hba *)data;
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		start_phy_v2_hw(hisi_hba, i);
}

static void phys_init_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;
	struct timer_list *timer = &hisi_hba->timer;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0x6a);
		hisi_sas_phy_read32(hisi_hba, i, CHL_INT2_MSK);
	}

	setup_timer(timer, start_phys_v2_hw, (unsigned long)hisi_hba);
	mod_timer(timer, jiffies + HZ);
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

/**
 * This function allocates across all queues to load balance.
 * Slots are allocated from queues in a round-robin fashion.
 *
 * The callpath to this function and upto writing the write
 * queue pointer should be safe from interruption.
 */
static int get_free_slot_v2_hw(struct hisi_hba *hisi_hba, int *q, int *s)
{
	struct device *dev = &hisi_hba->pdev->dev;
	u32 r, w;
	int queue = hisi_hba->queue;

	while (1) {
		w = hisi_sas_read32_relaxed(hisi_hba,
					    DLVRY_Q_0_WR_PTR + (queue * 0x14));
		r = hisi_sas_read32_relaxed(hisi_hba,
					    DLVRY_Q_0_RD_PTR + (queue * 0x14));
		if (r == (w+1) % HISI_SAS_QUEUE_SLOTS) {
			queue = (queue + 1) % hisi_hba->queue_count;
			if (queue == hisi_hba->queue) {
				dev_warn(dev, "could not find free slot\n");
				return -EAGAIN;
			}
			continue;
		}
		break;
	}
	hisi_hba->queue = (queue + 1) % hisi_hba->queue_count;
	*q = queue;
	*s = w;
	return 0;
}

static void start_delivery_v2_hw(struct hisi_hba *hisi_hba)
{
	int dlvry_queue = hisi_hba->slot_prep->dlvry_queue;
	int dlvry_queue_slot = hisi_hba->slot_prep->dlvry_queue_slot;

	hisi_sas_write32(hisi_hba, DLVRY_Q_0_WR_PTR + (dlvry_queue * 0x14),
			 ++dlvry_queue_slot % HISI_SAS_QUEUE_SLOTS);
}

static int prep_prd_sge_v2_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_slot *slot,
			      struct hisi_sas_cmd_hdr *hdr,
			      struct scatterlist *scatter,
			      int n_elem)
{
	struct device *dev = &hisi_hba->pdev->dev;
	struct scatterlist *sg;
	int i;

	if (n_elem > HISI_SAS_SGE_PAGE_CNT) {
		dev_err(dev, "prd err: n_elem(%d) > HISI_SAS_SGE_PAGE_CNT",
			n_elem);
		return -EINVAL;
	}

	slot->sge_page = dma_pool_alloc(hisi_hba->sge_page_pool, GFP_ATOMIC,
					&slot->sge_page_dma);
	if (!slot->sge_page)
		return -ENOMEM;

	for_each_sg(scatter, sg, n_elem, i) {
		struct hisi_sas_sge *entry = &slot->sge_page->sge[i];

		entry->addr = cpu_to_le64(sg_dma_address(sg));
		entry->page_ctrl_0 = entry->page_ctrl_1 = 0;
		entry->data_len = cpu_to_le32(sg_dma_len(sg));
		entry->data_off = 0;
	}

	hdr->prd_table_addr = cpu_to_le64(slot->sge_page_dma);

	hdr->sg_len = cpu_to_le32(n_elem << CMD_HDR_DATA_SGL_LEN_OFF);

	return 0;
}

static int prep_smp_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct device *dev = &hisi_hba->pdev->dev;
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
	hdr->sts_buffer_addr = cpu_to_le64(slot->status_buffer_dma);

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
	hdr->cmd_table_addr = cpu_to_le64(slot->command_table_dma);
	hdr->sts_buffer_addr = cpu_to_le64(slot->status_buffer_dma);

	buf_cmd = slot->command_table + sizeof(struct ssp_frame_hdr);

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

static void sata_done_v2_hw(struct hisi_hba *hisi_hba, struct sas_task *task,
			    struct hisi_sas_slot *slot)
{
	struct task_status_struct *ts = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)ts->buf;
	struct dev_to_host_fis *d2h = slot->status_buffer +
				      sizeof(struct hisi_sas_err_record);

	resp->frame_len = sizeof(struct dev_to_host_fis);
	memcpy(&resp->ending_fis[0], d2h, sizeof(struct dev_to_host_fis));

	ts->buf_valid_size = sizeof(*resp);
}

/* by default, task resp is complete */
static void slot_err_v2_hw(struct hisi_hba *hisi_hba,
			   struct sas_task *task,
			   struct hisi_sas_slot *slot)
{
	struct task_status_struct *ts = &task->task_status;
	struct hisi_sas_err_record_v2 *err_record = slot->status_buffer;
	u32 trans_tx_fail_type = cpu_to_le32(err_record->trans_tx_fail_type);
	u32 trans_rx_fail_type = cpu_to_le32(err_record->trans_rx_fail_type);
	u16 dma_tx_err_type = cpu_to_le16(err_record->dma_tx_err_type);
	u16 sipc_rx_err_type = cpu_to_le16(err_record->sipc_rx_err_type);
	u32 dma_rx_err_type = cpu_to_le32(err_record->dma_rx_err_type);
	int error = -1;

	if (dma_rx_err_type) {
		error = ffs(dma_rx_err_type)
			- 1 + DMA_RX_ERR_BASE;
	} else if (sipc_rx_err_type) {
		error = ffs(sipc_rx_err_type)
			- 1 + SIPC_RX_ERR_BASE;
	}  else if (dma_tx_err_type) {
		error = ffs(dma_tx_err_type)
			- 1 + DMA_TX_ERR_BASE;
	} else if (trans_rx_fail_type) {
		error = ffs(trans_rx_fail_type)
			- 1 + TRANS_RX_FAIL_BASE;
	} else if (trans_tx_fail_type) {
		error = ffs(trans_tx_fail_type)
			- 1 + TRANS_TX_FAIL_BASE;
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
		case TRANS_TX_OPEN_CNX_ERR_PATHWAY_BLOCKED:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_PATH_BLOCKED;
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
		case TRANS_TX_OPEN_CNX_ERR_BREAK_RCVD:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_WRONG_DESTINATION:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_ZONE_VIOLATION:
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
		case TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE:
		{
			ts->stat = SAS_PHY_DOWN;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_OPEN_TIMEOUT:
		{
			ts->stat = SAS_OPEN_TO;
			break;
		}
		case DMA_RX_DATA_LEN_OVERFLOW:
		{
			ts->stat = SAS_DATA_OVERRUN;
			ts->residual = 0;
			break;
		}
		case DMA_RX_DATA_LEN_UNDERFLOW:
		case SIPC_RX_DATA_UNDERFLOW_ERR:
		{
			ts->residual = trans_tx_fail_type;
			ts->stat = SAS_DATA_UNDERRUN;
			break;
		}
		case TRANS_TX_ERR_FRAME_TXED:
		{
			/* This will request a retry */
			ts->stat = SAS_QUEUE_FULL;
			slot->abort = 1;
			break;
		}
		case TRANS_TX_OPEN_FAIL_WITH_IT_NEXUS_LOSS:
		case TRANS_TX_ERR_PHY_NOT_ENABLE:
		case TRANS_TX_OPEN_CNX_ERR_BY_OTHER:
		case TRANS_TX_OPEN_CNX_ERR_AIP_TIMEOUT:
		case TRANS_TX_OPEN_RETRY_ERR_THRESHOLD_REACHED:
		case TRANS_TX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_BREAK_REQUEST:
		case TRANS_TX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_TX_ERR_WITH_CLOSE_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_TX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_TX_ERR_WITH_NAK_RECEVIED:
		case TRANS_TX_ERR_WITH_ACK_NAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_IPTT_CONFLICT:
		case TRANS_TX_ERR_WITH_CREDIT_TIMEOUT:
		case TRANS_RX_ERR_WITH_RXFRAME_CRC_ERR:
		case TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR:
		case TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM:
		case TRANS_RX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_RX_ERR_WITH_BREAK_REQUEST:
		case TRANS_RX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_RX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_RX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_RX_ERR_WITH_DATA_LEN0:
		case TRANS_RX_ERR_WITH_BAD_HASH:
		case TRANS_RX_XRDY_WLEN_ZERO_ERR:
		case TRANS_RX_SSP_FRM_LEN_ERR:
		case TRANS_RX_ERR_WITH_BAD_FRM_TYPE:
		case DMA_TX_UNEXP_XFER_ERR:
		case DMA_TX_UNEXP_RETRANS_ERR:
		case DMA_TX_XFER_LEN_OVERFLOW:
		case DMA_TX_XFER_OFFSET_ERR:
		case DMA_RX_DATA_OFFSET_ERR:
		case DMA_RX_UNEXP_NORM_RESP_ERR:
		case DMA_RX_UNEXP_RDFRAME_ERR:
		case DMA_RX_UNKNOWN_FRM_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_UNKNOWN;
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
		case TRANS_TX_OPEN_CNX_ERR_LOW_PHY_POWER:
		case TRANS_TX_OPEN_CNX_ERR_PATHWAY_BLOCKED:
		case TRANS_TX_OPEN_CNX_ERR_NO_DESTINATION:
		{
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_DEV_NO_RESPONSE;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_PROTOCOL_NOT_SUPPORTED:
		case TRANS_TX_OPEN_CNX_ERR_CONNECTION_RATE_NOT_SUPPORTED:
		case TRANS_TX_OPEN_CNX_ERR_BAD_DESTINATION:
		case TRANS_TX_OPEN_CNX_ERR_BREAK_RCVD:
		case TRANS_TX_OPEN_CNX_ERR_WRONG_DESTINATION:
		case TRANS_TX_OPEN_CNX_ERR_ZONE_VIOLATION:
		case TRANS_TX_OPEN_CNX_ERR_STP_RESOURCES_BUSY:
		{
			ts->stat = SAS_OPEN_REJECT;
			break;
		}
		case TRANS_TX_OPEN_CNX_ERR_OPEN_TIMEOUT:
		{
			ts->stat = SAS_OPEN_TO;
			break;
		}
		case DMA_RX_DATA_LEN_OVERFLOW:
		{
			ts->stat = SAS_DATA_OVERRUN;
			break;
		}
		case TRANS_TX_OPEN_FAIL_WITH_IT_NEXUS_LOSS:
		case TRANS_TX_ERR_PHY_NOT_ENABLE:
		case TRANS_TX_OPEN_CNX_ERR_BY_OTHER:
		case TRANS_TX_OPEN_CNX_ERR_AIP_TIMEOUT:
		case TRANS_TX_OPEN_RETRY_ERR_THRESHOLD_REACHED:
		case TRANS_TX_ERR_WITH_BREAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_BREAK_REQUEST:
		case TRANS_TX_ERR_WITH_BREAK_RECEVIED:
		case TRANS_TX_ERR_WITH_CLOSE_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_TX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_TX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_TX_ERR_WITH_NAK_RECEVIED:
		case TRANS_TX_ERR_WITH_ACK_NAK_TIMEOUT:
		case TRANS_TX_ERR_WITH_CREDIT_TIMEOUT:
		case TRANS_TX_ERR_WITH_WAIT_RECV_TIMEOUT:
		case TRANS_RX_ERR_WITH_RXFIS_8B10B_DISP_ERR:
		case TRANS_RX_ERR_WITH_RXFRAME_HAVE_ERRPRM:
		case TRANS_RX_ERR_WITH_RXFIS_DECODE_ERROR:
		case TRANS_RX_ERR_WITH_RXFIS_CRC_ERR:
		case TRANS_RX_ERR_WITH_RXFRAME_LENGTH_OVERRUN:
		case TRANS_RX_ERR_WITH_RXFIS_RX_SYNCP:
		case TRANS_RX_ERR_WITH_CLOSE_NORMAL:
		case TRANS_RX_ERR_WITH_CLOSE_PHY_DISABLE:
		case TRANS_RX_ERR_WITH_CLOSE_DWS_TIMEOUT:
		case TRANS_RX_ERR_WITH_CLOSE_COMINIT:
		case TRANS_RX_ERR_WITH_DATA_LEN0:
		case TRANS_RX_ERR_WITH_BAD_HASH:
		case TRANS_RX_XRDY_WLEN_ZERO_ERR:
		case TRANS_RX_SSP_FRM_LEN_ERR:
		case SIPC_RX_FIS_STATUS_ERR_BIT_VLD:
		case SIPC_RX_PIO_WRSETUP_STATUS_DRQ_ERR:
		case SIPC_RX_FIS_STATUS_BSY_BIT_ERR:
		case SIPC_RX_WRSETUP_LEN_ODD_ERR:
		case SIPC_RX_WRSETUP_LEN_ZERO_ERR:
		case SIPC_RX_WRDATA_LEN_NOT_MATCH_ERR:
		case SIPC_RX_SATA_UNEXP_FIS_ERR:
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
		{
			ts->stat = SAS_OPEN_REJECT;
			break;
		}
		default:
		{
			ts->stat = SAS_PROTO_RESPONSE;
			break;
		}
		}
		sata_done_v2_hw(hisi_hba, task, slot);
	}
		break;
	default:
		break;
	}
}

static int
slot_complete_v2_hw(struct hisi_hba *hisi_hba, struct hisi_sas_slot *slot,
		    int abort)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_device *sas_dev;
	struct device *dev = &hisi_hba->pdev->dev;
	struct task_status_struct *ts;
	struct domain_device *device;
	enum exec_status sts;
	struct hisi_sas_complete_v2_hdr *complete_queue =
			hisi_hba->complete_hdr[slot->cmplt_queue];
	struct hisi_sas_complete_v2_hdr *complete_hdr =
			&complete_queue[slot->cmplt_queue_slot];

	if (unlikely(!task || !task->lldd_task || !task->dev))
		return -EINVAL;

	ts = &task->task_status;
	device = task->dev;
	sas_dev = device->lldd_dev;

	task->task_state_flags &=
		~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
	task->task_state_flags |= SAS_TASK_STATE_DONE;

	memset(ts, 0, sizeof(*ts));
	ts->resp = SAS_TASK_COMPLETE;

	if (unlikely(!sas_dev || abort)) {
		if (!sas_dev)
			dev_dbg(dev, "slot complete: port has not device\n");
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
		ts->stat = TMF_RESP_FUNC_COMPLETE;
		goto out;
	case STAT_IO_NO_DEVICE:
		ts->stat = TMF_RESP_FUNC_COMPLETE;
		goto out;
	case STAT_IO_NOT_VALID:
		/* abort single io, controller don't find
		 * the io need to abort
		 */
		ts->stat = TMF_RESP_FUNC_FAILED;
		goto out;
	default:
		break;
	}

	if ((complete_hdr->dw0 & CMPLT_HDR_ERX_MSK) &&
		(!(complete_hdr->dw0 & CMPLT_HDR_RSPNS_XFRD_MSK))) {

		slot_err_v2_hw(hisi_hba, task, slot);
		if (unlikely(slot->abort)) {
			queue_work(hisi_hba->wq, &slot->abort_slot);
			/* immediately return and do not complete */
			return ts->stat;
		}
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
	{
		struct ssp_response_iu *iu = slot->status_buffer +
			sizeof(struct hisi_sas_err_record);

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
		       slot->status_buffer +
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
		sata_done_v2_hw(hisi_hba, task, slot);
		break;
	}
	default:
		ts->stat = SAM_STAT_CHECK_CONDITION;
		break;
	}

	if (!slot->port->port_attached) {
		dev_err(dev, "slot complete: port %d has removed\n",
			slot->port->sas_port.id);
		ts->stat = SAS_PHY_DOWN;
	}

out:
	if (sas_dev && sas_dev->running_req)
		sas_dev->running_req--;

	hisi_sas_slot_task_free(hisi_hba, task, slot);
	sts = ts->stat;

	if (task->task_done)
		task->task_done(task);

	return sts;
}

static u8 get_ata_protocol(u8 cmd, int direction)
{
	switch (cmd) {
	case ATA_CMD_FPDMA_WRITE:
	case ATA_CMD_FPDMA_READ:
	case ATA_CMD_FPDMA_RECV:
	case ATA_CMD_FPDMA_SEND:
	case ATA_CMD_NCQ_NON_DATA:
	return SATA_PROTOCOL_FPDMA;

	case ATA_CMD_ID_ATA:
	case ATA_CMD_PMP_READ:
	case ATA_CMD_READ_LOG_EXT:
	case ATA_CMD_PIO_READ:
	case ATA_CMD_PIO_READ_EXT:
	case ATA_CMD_PMP_WRITE:
	case ATA_CMD_WRITE_LOG_EXT:
	case ATA_CMD_PIO_WRITE:
	case ATA_CMD_PIO_WRITE_EXT:
	return SATA_PROTOCOL_PIO;

	case ATA_CMD_READ:
	case ATA_CMD_READ_EXT:
	case ATA_CMD_READ_LOG_DMA_EXT:
	case ATA_CMD_WRITE:
	case ATA_CMD_WRITE_EXT:
	case ATA_CMD_WRITE_QUEUED:
	case ATA_CMD_WRITE_LOG_DMA_EXT:
	return SATA_PROTOCOL_DMA;

	case ATA_CMD_DOWNLOAD_MICRO:
	case ATA_CMD_DEV_RESET:
	case ATA_CMD_CHK_POWER:
	case ATA_CMD_FLUSH:
	case ATA_CMD_FLUSH_EXT:
	case ATA_CMD_VERIFY:
	case ATA_CMD_VERIFY_EXT:
	case ATA_CMD_SET_FEATURES:
	case ATA_CMD_STANDBY:
	case ATA_CMD_STANDBYNOW1:
	return SATA_PROTOCOL_NONDATA;
	default:
		if (direction == DMA_NONE)
			return SATA_PROTOCOL_NONDATA;
		return SATA_PROTOCOL_PIO;
	}
}

static int get_ncq_tag_v2_hw(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
			qc->tf.command == ATA_CMD_FPDMA_READ) {
			*tag = qc->tag;
			return 1;
		}
	}
	return 0;
}

static int prep_ata_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct domain_device *device = task->dev;
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct hisi_sas_port *port = device->port->lldd_port;
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

	if (0 == task->ata_task.fis.command)
		dw1 |= 1 << CMD_HDR_RESET_OFF;

	dw1 |= (get_ata_protocol(task->ata_task.fis.command, task->data_dir))
		<< CMD_HDR_FRAME_TYPE_OFF;
	dw1 |= sas_dev->device_id << CMD_HDR_DEV_ID_OFF;
	hdr->dw1 = cpu_to_le32(dw1);

	/* dw2 */
	if (task->ata_task.use_ncq && get_ncq_tag_v2_hw(task, &hdr_tag)) {
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
	hdr->cmd_table_addr = cpu_to_le64(slot->command_table_dma);
	hdr->sts_buffer_addr = cpu_to_le64(slot->status_buffer_dma);

	buf_cmd = slot->command_table;

	if (likely(!task->ata_task.device_control_reg_update))
		task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));

	return 0;
}

static int prep_abort_v2_hw(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	struct sas_task *task = slot->task;
	struct domain_device *dev = task->dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct hisi_sas_port *port = slot->port;

	/* dw0 */
	hdr->dw0 = cpu_to_le32((5 << CMD_HDR_CMD_OFF) | /*abort*/
			       (port->id << CMD_HDR_PORT_OFF) |
			       ((dev_is_sata(dev) ? 1:0) <<
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
	int i, res = 0;
	u32 context, port_id, link_rate, hard_phy_linkrate;
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = &hisi_hba->pdev->dev;
	u32 *frame_rcvd = (u32 *)sas_phy->frame_rcvd;
	struct sas_identify_frame *id = (struct sas_identify_frame *)frame_rcvd;

	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 1);

	/* Check for SATA dev */
	context = hisi_sas_read32(hisi_hba, PHY_CONTEXT);
	if (context & (1 << phy_no))
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

	/* Get the linkrates */
	link_rate = hisi_sas_read32(hisi_hba, PHY_CONN_RATE);
	link_rate = (link_rate >> (phy_no * 4)) & 0xf;
	sas_phy->linkrate = link_rate;
	hard_phy_linkrate = hisi_sas_phy_read32(hisi_hba, phy_no,
						HARD_PHY_LINKRATE);
	phy->maximum_linkrate = hard_phy_linkrate & 0xf;
	phy->minimum_linkrate = (hard_phy_linkrate >> 4) & 0xf;

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
	else if (phy->identify.device_type != SAS_PHY_UNUSED)
		phy->identify.target_port_protocols =
			SAS_PROTOCOL_SMP;
	queue_work(hisi_hba->wq, &phy->phyup_ws);

end:
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_PHY_ENABLE_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 0);

	return res;
}

static int phy_down_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	int res = 0;
	u32 phy_cfg, phy_state;

	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 1);

	phy_cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);

	hisi_sas_phy_down(hisi_hba, phy_no, (phy_state & 1 << phy_no) ? 1 : 0);

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0, CHL_INT0_NOT_RDY_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 0);

	return res;
}

static irqreturn_t int_phy_updown_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_msk;
	int phy_no = 0;
	irqreturn_t res = IRQ_HANDLED;

	irq_msk = (hisi_sas_read32(hisi_hba, HGC_INVLD_DQE_INFO)
		   >> HGC_INVLD_DQE_INFO_FB_CH0_OFF) & 0x1ff;
	while (irq_msk) {
		if (irq_msk  & 1) {
			u32 irq_value = hisi_sas_phy_read32(hisi_hba, phy_no,
							    CHL_INT0);

			if (irq_value & CHL_INT0_SL_PHY_ENABLE_MSK)
				/* phy up */
				if (phy_up_v2_hw(phy_no, hisi_hba)) {
					res = IRQ_NONE;
					goto end;
				}

			if (irq_value & CHL_INT0_NOT_RDY_MSK)
				/* phy down */
				if (phy_down_v2_hw(phy_no, hisi_hba)) {
					res = IRQ_NONE;
					goto end;
				}
		}
		irq_msk >>= 1;
		phy_no++;
	}

end:
	return res;
}

static void phy_bcast_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;

	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 1);
	sas_ha->notify_port_event(sas_phy, PORTE_BROADCAST_RCVD);
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_RX_BCST_ACK_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 0);
}

static irqreturn_t int_chnl_int_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	struct device *dev = &hisi_hba->pdev->dev;
	u32 ent_msk, ent_tmp, irq_msk;
	int phy_no = 0;

	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK3);
	ent_tmp = ent_msk;
	ent_msk |= ENT_INT_SRC_MSK3_ENT95_MSK_MSK;
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_msk);

	irq_msk = (hisi_sas_read32(hisi_hba, HGC_INVLD_DQE_INFO) >>
			HGC_INVLD_DQE_INFO_FB_CH3_OFF) & 0x1ff;

	while (irq_msk) {
		if (irq_msk & (1 << phy_no)) {
			u32 irq_value0 = hisi_sas_phy_read32(hisi_hba, phy_no,
							     CHL_INT0);
			u32 irq_value1 = hisi_sas_phy_read32(hisi_hba, phy_no,
							     CHL_INT1);
			u32 irq_value2 = hisi_sas_phy_read32(hisi_hba, phy_no,
							     CHL_INT2);

			if (irq_value1) {
				if (irq_value1 & (CHL_INT1_DMAC_RX_ECC_ERR_MSK |
						  CHL_INT1_DMAC_TX_ECC_ERR_MSK))
					panic("%s: DMAC RX/TX ecc bad error! (0x%x)",
						dev_name(dev), irq_value1);

				hisi_sas_phy_write32(hisi_hba, phy_no,
						     CHL_INT1, irq_value1);
			}

			if (irq_value2)
				hisi_sas_phy_write32(hisi_hba, phy_no,
						     CHL_INT2, irq_value2);


			if (irq_value0) {
				if (irq_value0 & CHL_INT0_SL_RX_BCST_ACK_MSK)
					phy_bcast_v2_hw(phy_no, hisi_hba);

				hisi_sas_phy_write32(hisi_hba, phy_no,
						CHL_INT0, irq_value0
						& (~CHL_INT0_HOTPLUG_TOUT_MSK)
						& (~CHL_INT0_SL_PHY_ENABLE_MSK)
						& (~CHL_INT0_NOT_RDY_MSK));
			}
		}
		irq_msk &= ~(1 << phy_no);
		phy_no++;
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_tmp);

	return IRQ_HANDLED;
}

static irqreturn_t cq_interrupt_v2_hw(int irq_no, void *p)
{
	struct hisi_sas_cq *cq = p;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	struct hisi_sas_slot *slot;
	struct hisi_sas_itct *itct;
	struct hisi_sas_complete_v2_hdr *complete_queue;
	u32 irq_value, rd_point, wr_point, dev_id;
	int queue = cq->id;

	complete_queue = hisi_hba->complete_hdr[queue];
	irq_value = hisi_sas_read32(hisi_hba, OQ_INT_SRC);

	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 1 << queue);

	rd_point = hisi_sas_read32(hisi_hba, COMPL_Q_0_RD_PTR +
				   (0x14 * queue));
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
				slot_complete_v2_hw(hisi_hba, slot, 0);

				act_tmp &= ~(1 << ncq_tag_count);
				ncq_tag_count = ffs(act_tmp);
			}
		} else {
			iptt = (complete_hdr->dw1) & CMPLT_HDR_IPTT_MSK;
			slot = &hisi_hba->slot_info[iptt];
			slot->cmplt_queue_slot = rd_point;
			slot->cmplt_queue = queue;
			slot_complete_v2_hw(hisi_hba, slot, 0);
		}

		if (++rd_point >= HISI_SAS_QUEUE_SLOTS)
			rd_point = 0;
	}

	/* update rd_point */
	hisi_sas_write32(hisi_hba, COMPL_Q_0_RD_PTR + (0x14 * queue), rd_point);
	return IRQ_HANDLED;
}

static irqreturn_t sata_int_v2_hw(int irq_no, void *p)
{
	struct hisi_sas_phy *phy = p;
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = &hisi_hba->pdev->dev;
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
	queue_work(hisi_hba->wq, &phy->phyup_ws);

end:
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1 + offset, ent_tmp);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1 + offset, ent_msk);

	return res;
}

static irq_handler_t phy_interrupts[HISI_SAS_PHY_INT_NR] = {
	int_phy_updown_v2_hw,
	int_chnl_int_v2_hw,
};

/**
 * There is a limitation in the hip06 chipset that we need
 * to map in all mbigen interrupts, even if they are not used.
 */
static int interrupt_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct platform_device *pdev = hisi_hba->pdev;
	struct device *dev = &pdev->dev;
	int i, irq, rc, irq_map[128];


	for (i = 0; i < 128; i++)
		irq_map[i] = platform_get_irq(pdev, i);

	for (i = 0; i < HISI_SAS_PHY_INT_NR; i++) {
		int idx = i;

		irq = irq_map[idx + 1]; /* Phy up/down is irq1 */
		if (!irq) {
			dev_err(dev, "irq init: fail map phy interrupt %d\n",
				idx);
			return -ENOENT;
		}

		rc = devm_request_irq(dev, irq, phy_interrupts[i], 0,
				      DRV_NAME " phy", hisi_hba);
		if (rc) {
			dev_err(dev, "irq init: could not request "
				"phy interrupt %d, rc=%d\n",
				irq, rc);
			return -ENOENT;
		}
	}

	for (i = 0; i < hisi_hba->n_phy; i++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[i];
		int idx = i + 72; /* First SATA interrupt is irq72 */

		irq = irq_map[idx];
		if (!irq) {
			dev_err(dev, "irq init: fail map phy interrupt %d\n",
				idx);
			return -ENOENT;
		}

		rc = devm_request_irq(dev, irq, sata_int_v2_hw, 0,
				      DRV_NAME " sata", phy);
		if (rc) {
			dev_err(dev, "irq init: could not request "
				"sata interrupt %d, rc=%d\n",
				irq, rc);
			return -ENOENT;
		}
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		int idx = i + 96; /* First cq interrupt is irq96 */

		irq = irq_map[idx];
		if (!irq) {
			dev_err(dev,
				"irq init: could not map cq interrupt %d\n",
				idx);
			return -ENOENT;
		}
		rc = devm_request_irq(dev, irq, cq_interrupt_v2_hw, 0,
				      DRV_NAME " cq", &hisi_hba->cq[i]);
		if (rc) {
			dev_err(dev,
				"irq init: could not request cq interrupt %d, rc=%d\n",
				irq, rc);
			return -ENOENT;
		}
	}

	return 0;
}

static int hisi_sas_v2_init(struct hisi_hba *hisi_hba)
{
	int rc;

	rc = hw_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	rc = interrupt_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	phys_init_v2_hw(hisi_hba);

	return 0;
}

static const struct hisi_sas_hw hisi_sas_v2_hw = {
	.hw_init = hisi_sas_v2_init,
	.setup_itct = setup_itct_v2_hw,
	.slot_index_alloc = slot_index_alloc_quirk_v2_hw,
	.alloc_dev = alloc_dev_quirk_v2_hw,
	.sl_notify = sl_notify_v2_hw,
	.get_wideport_bitmap = get_wideport_bitmap_v2_hw,
	.free_device = free_device_v2_hw,
	.prep_smp = prep_smp_v2_hw,
	.prep_ssp = prep_ssp_v2_hw,
	.prep_stp = prep_ata_v2_hw,
	.prep_abort = prep_abort_v2_hw,
	.get_free_slot = get_free_slot_v2_hw,
	.start_delivery = start_delivery_v2_hw,
	.slot_complete = slot_complete_v2_hw,
	.phy_enable = enable_phy_v2_hw,
	.phy_disable = disable_phy_v2_hw,
	.phy_hard_reset = phy_hard_reset_v2_hw,
	.max_command_entries = HISI_SAS_COMMAND_ENTRIES_V2_HW,
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v2_hdr),
};

static int hisi_sas_v2_probe(struct platform_device *pdev)
{
	return hisi_sas_probe(pdev, &hisi_sas_v2_hw);
}

static int hisi_sas_v2_remove(struct platform_device *pdev)
{
	return hisi_sas_remove(pdev);
}

static const struct of_device_id sas_v2_of_match[] = {
	{ .compatible = "hisilicon,hip06-sas-v2",},
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
