// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2017 Hisilicon Limited.
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v3_hw"

/* global registers need init */
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
#define PHY_CONN_RATE			0x30
#define ITCT_CLR			0x44
#define ITCT_CLR_EN_OFF			16
#define ITCT_CLR_EN_MSK			(0x1 << ITCT_CLR_EN_OFF)
#define ITCT_DEV_OFF			0
#define ITCT_DEV_MSK			(0x7ff << ITCT_DEV_OFF)
#define SAS_AXI_USER3			0x50
#define IO_SATA_BROKEN_MSG_ADDR_LO	0x58
#define IO_SATA_BROKEN_MSG_ADDR_HI	0x5c
#define SATA_INITI_D2H_STORE_ADDR_LO	0x60
#define SATA_INITI_D2H_STORE_ADDR_HI	0x64
#define CFG_MAX_TAG			0x68
#define HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL	0x84
#define HGC_SAS_TXFAIL_RETRY_CTRL	0x88
#define HGC_GET_ITV_TIME		0x90
#define DEVICE_MSG_WORK_MODE		0x94
#define OPENA_WT_CONTI_TIME		0x9c
#define I_T_NEXUS_LOSS_TIME		0xa0
#define MAX_CON_TIME_LIMIT_TIME		0xa4
#define BUS_INACTIVE_LIMIT_TIME		0xa8
#define REJECT_TO_OPEN_LIMIT_TIME	0xac
#define CQ_INT_CONVERGE_EN		0xb0
#define CFG_AGING_TIME			0xbc
#define HGC_DFX_CFG2			0xc0
#define CFG_ABT_SET_QUERY_IPTT	0xd4
#define CFG_SET_ABORTED_IPTT_OFF	0
#define CFG_SET_ABORTED_IPTT_MSK	(0xfff << CFG_SET_ABORTED_IPTT_OFF)
#define CFG_SET_ABORTED_EN_OFF	12
#define CFG_ABT_SET_IPTT_DONE	0xd8
#define CFG_ABT_SET_IPTT_DONE_OFF	0
#define HGC_IOMB_PROC1_STATUS	0x104
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
#define CHNL_INT_STATUS			0x148
#define HGC_ITCT_ECC_ADDR		0x150
#define HGC_ITCT_ECC_1B_ADDR_OFF		0
#define HGC_ITCT_ECC_1B_ADDR_MSK		(0x3ff << \
						 HGC_ITCT_ECC_1B_ADDR_OFF)
#define HGC_ITCT_ECC_MB_ADDR_OFF		16
#define HGC_ITCT_ECC_MB_ADDR_MSK		(0x3ff << \
						 HGC_ITCT_ECC_MB_ADDR_OFF)
#define HGC_AXI_FIFO_ERR_INFO  0x154
#define AXI_ERR_INFO_OFF               0
#define AXI_ERR_INFO_MSK               (0xff << AXI_ERR_INFO_OFF)
#define FIFO_ERR_INFO_OFF              8
#define FIFO_ERR_INFO_MSK              (0xff << FIFO_ERR_INFO_OFF)
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
#define ENT_INT_SRC3_DQE_POISON_OFF	18
#define ENT_INT_SRC3_IOST_POISON_OFF	19
#define ENT_INT_SRC3_ITCT_POISON_OFF	20
#define ENT_INT_SRC3_ITCT_NCQ_POISON_OFF	21
#define ENT_INT_SRC_MSK1		0x1c4
#define ENT_INT_SRC_MSK2		0x1c8
#define ENT_INT_SRC_MSK3		0x1cc
#define ENT_INT_SRC_MSK3_ENT95_MSK_OFF	31
#define CHNL_PHYUPDOWN_INT_MSK		0x1d0
#define CHNL_ENT_INT_MSK			0x1d4
#define HGC_COM_INT_MSK				0x1d8
#define ENT_INT_SRC_MSK3_ENT95_MSK_MSK	(0x1 << ENT_INT_SRC_MSK3_ENT95_MSK_OFF)
#define SAS_ECC_INTR			0x1e8
#define SAS_ECC_INTR_DQE_ECC_1B_OFF		0
#define SAS_ECC_INTR_DQE_ECC_MB_OFF		1
#define SAS_ECC_INTR_IOST_ECC_1B_OFF	2
#define SAS_ECC_INTR_IOST_ECC_MB_OFF	3
#define SAS_ECC_INTR_ITCT_ECC_1B_OFF	4
#define SAS_ECC_INTR_ITCT_ECC_MB_OFF	5
#define SAS_ECC_INTR_ITCTLIST_ECC_1B_OFF	6
#define SAS_ECC_INTR_ITCTLIST_ECC_MB_OFF	7
#define SAS_ECC_INTR_IOSTLIST_ECC_1B_OFF	8
#define SAS_ECC_INTR_IOSTLIST_ECC_MB_OFF	9
#define SAS_ECC_INTR_CQE_ECC_1B_OFF		10
#define SAS_ECC_INTR_CQE_ECC_MB_OFF		11
#define SAS_ECC_INTR_NCQ_MEM0_ECC_1B_OFF	12
#define SAS_ECC_INTR_NCQ_MEM0_ECC_MB_OFF	13
#define SAS_ECC_INTR_NCQ_MEM1_ECC_1B_OFF	14
#define SAS_ECC_INTR_NCQ_MEM1_ECC_MB_OFF	15
#define SAS_ECC_INTR_NCQ_MEM2_ECC_1B_OFF	16
#define SAS_ECC_INTR_NCQ_MEM2_ECC_MB_OFF	17
#define SAS_ECC_INTR_NCQ_MEM3_ECC_1B_OFF	18
#define SAS_ECC_INTR_NCQ_MEM3_ECC_MB_OFF	19
#define SAS_ECC_INTR_OOO_RAM_ECC_1B_OFF		20
#define SAS_ECC_INTR_OOO_RAM_ECC_MB_OFF		21
#define SAS_ECC_INTR_MSK		0x1ec
#define HGC_ERR_STAT_EN			0x238
#define CQE_SEND_CNT			0x248
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
#define HGC_RXM_DFX_STATUS14		0xae8
#define HGC_RXM_DFX_STATUS14_MEM0_OFF	0
#define HGC_RXM_DFX_STATUS14_MEM0_MSK	(0x1ff << \
					 HGC_RXM_DFX_STATUS14_MEM0_OFF)
#define HGC_RXM_DFX_STATUS14_MEM1_OFF	9
#define HGC_RXM_DFX_STATUS14_MEM1_MSK	(0x1ff << \
					 HGC_RXM_DFX_STATUS14_MEM1_OFF)
#define HGC_RXM_DFX_STATUS14_MEM2_OFF	18
#define HGC_RXM_DFX_STATUS14_MEM2_MSK	(0x1ff << \
					 HGC_RXM_DFX_STATUS14_MEM2_OFF)
#define HGC_RXM_DFX_STATUS15		0xaec
#define HGC_RXM_DFX_STATUS15_MEM3_OFF	0
#define HGC_RXM_DFX_STATUS15_MEM3_MSK	(0x1ff << \
					 HGC_RXM_DFX_STATUS15_MEM3_OFF)
#define AWQOS_AWCACHE_CFG	0xc84
#define ARQOS_ARCACHE_CFG	0xc88
#define HILINK_ERR_DFX		0xe04
#define SAS_GPIO_CFG_0		0x1000
#define SAS_GPIO_CFG_1		0x1004
#define SAS_GPIO_TX_0_1	0x1040
#define SAS_CFG_DRIVE_VLD	0x1070

/* phy registers requiring init */
#define PORT_BASE			(0x2000)
#define PHY_CFG				(PORT_BASE + 0x0)
#define HARD_PHY_LINKRATE		(PORT_BASE + 0x4)
#define PHY_CFG_ENA_OFF			0
#define PHY_CFG_ENA_MSK			(0x1 << PHY_CFG_ENA_OFF)
#define PHY_CFG_DC_OPT_OFF		2
#define PHY_CFG_DC_OPT_MSK		(0x1 << PHY_CFG_DC_OPT_OFF)
#define PHY_CFG_PHY_RST_OFF		3
#define PHY_CFG_PHY_RST_MSK		(0x1 << PHY_CFG_PHY_RST_OFF)
#define PROG_PHY_LINK_RATE		(PORT_BASE + 0x8)
#define PHY_CTRL			(PORT_BASE + 0x14)
#define PHY_CTRL_RESET_OFF		0
#define PHY_CTRL_RESET_MSK		(0x1 << PHY_CTRL_RESET_OFF)
#define CMD_HDR_PIR_OFF			8
#define CMD_HDR_PIR_MSK			(0x1 << CMD_HDR_PIR_OFF)
#define SERDES_CFG			(PORT_BASE + 0x1c)
#define SL_CFG				(PORT_BASE + 0x84)
#define AIP_LIMIT			(PORT_BASE + 0x90)
#define SL_CONTROL			(PORT_BASE + 0x94)
#define SL_CONTROL_NOTIFY_EN_OFF	0
#define SL_CONTROL_NOTIFY_EN_MSK	(0x1 << SL_CONTROL_NOTIFY_EN_OFF)
#define SL_CTA_OFF		17
#define SL_CTA_MSK		(0x1 << SL_CTA_OFF)
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
#define TXID_AUTO				(PORT_BASE + 0xb8)
#define CT3_OFF		1
#define CT3_MSK		(0x1 << CT3_OFF)
#define TX_HARDRST_OFF          2
#define TX_HARDRST_MSK          (0x1 << TX_HARDRST_OFF)
#define RX_IDAF_DWORD0			(PORT_BASE + 0xc4)
#define RXOP_CHECK_CFG_H		(PORT_BASE + 0xfc)
#define STP_LINK_TIMER			(PORT_BASE + 0x120)
#define STP_LINK_TIMEOUT_STATE		(PORT_BASE + 0x124)
#define CON_CFG_DRIVER			(PORT_BASE + 0x130)
#define SAS_SSP_CON_TIMER_CFG		(PORT_BASE + 0x134)
#define SAS_SMP_CON_TIMER_CFG		(PORT_BASE + 0x138)
#define SAS_STP_CON_TIMER_CFG		(PORT_BASE + 0x13c)
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
#define CHL_INT1_DMAC_TX_ECC_MB_ERR_OFF	15
#define CHL_INT1_DMAC_TX_ECC_1B_ERR_OFF	16
#define CHL_INT1_DMAC_RX_ECC_MB_ERR_OFF	17
#define CHL_INT1_DMAC_RX_ECC_1B_ERR_OFF	18
#define CHL_INT1_DMAC_TX_AXI_WR_ERR_OFF	19
#define CHL_INT1_DMAC_TX_AXI_RD_ERR_OFF	20
#define CHL_INT1_DMAC_RX_AXI_WR_ERR_OFF	21
#define CHL_INT1_DMAC_RX_AXI_RD_ERR_OFF	22
#define CHL_INT1_DMAC_TX_FIFO_ERR_OFF	23
#define CHL_INT1_DMAC_RX_FIFO_ERR_OFF	24
#define CHL_INT1_DMAC_TX_AXI_RUSER_ERR_OFF	26
#define CHL_INT1_DMAC_RX_AXI_RUSER_ERR_OFF	27
#define CHL_INT2			(PORT_BASE + 0x1bc)
#define CHL_INT2_SL_IDAF_TOUT_CONF_OFF	0
#define CHL_INT2_RX_DISP_ERR_OFF	28
#define CHL_INT2_RX_CODE_ERR_OFF	29
#define CHL_INT2_RX_INVLD_DW_OFF	30
#define CHL_INT2_STP_LINK_TIMEOUT_OFF	31
#define CHL_INT0_MSK			(PORT_BASE + 0x1c0)
#define CHL_INT1_MSK			(PORT_BASE + 0x1c4)
#define CHL_INT2_MSK			(PORT_BASE + 0x1c8)
#define SAS_EC_INT_COAL_TIME		(PORT_BASE + 0x1cc)
#define CHL_INT_COAL_EN			(PORT_BASE + 0x1d0)
#define SAS_RX_TRAIN_TIMER		(PORT_BASE + 0x2a4)
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

#define COARSETUNE_TIME			(PORT_BASE + 0x304)
#define ERR_CNT_DWS_LOST		(PORT_BASE + 0x380)
#define ERR_CNT_RESET_PROB		(PORT_BASE + 0x384)
#define ERR_CNT_INVLD_DW		(PORT_BASE + 0x390)
#define ERR_CNT_CODE_ERR		(PORT_BASE + 0x394)
#define ERR_CNT_DISP_ERR		(PORT_BASE + 0x398)

#define DEFAULT_ITCT_HW		2048 /* reset value, not reprogrammed */
#if (HISI_SAS_MAX_DEVICES > DEFAULT_ITCT_HW)
#error Max ITCT exceeded
#endif

#define AXI_MASTER_CFG_BASE		(0x5000)
#define AM_CTRL_GLOBAL			(0x0)
#define AM_CTRL_SHUTDOWN_REQ_OFF	0
#define AM_CTRL_SHUTDOWN_REQ_MSK	(0x1 << AM_CTRL_SHUTDOWN_REQ_OFF)
#define AM_CURR_TRANS_RETURN	(0x150)

#define AM_CFG_MAX_TRANS		(0x5010)
#define AM_CFG_SINGLE_PORT_MAX_TRANS	(0x5014)
#define AXI_CFG					(0x5100)
#define AM_ROB_ECC_ERR_ADDR		(0x510c)
#define AM_ROB_ECC_ERR_ADDR_OFF	0
#define AM_ROB_ECC_ERR_ADDR_MSK	0xffffffff

/* RAS registers need init */
#define RAS_BASE		(0x6000)
#define SAS_RAS_INTR0			(RAS_BASE)
#define SAS_RAS_INTR1			(RAS_BASE + 0x04)
#define SAS_RAS_INTR0_MASK		(RAS_BASE + 0x08)
#define SAS_RAS_INTR1_MASK		(RAS_BASE + 0x0c)
#define CFG_SAS_RAS_INTR_MASK		(RAS_BASE + 0x1c)
#define SAS_RAS_INTR2			(RAS_BASE + 0x20)
#define SAS_RAS_INTR2_MASK		(RAS_BASE + 0x24)

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
#define CMD_HDR_UNCON_CMD_OFF	3
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
/* dw3 */
#define CMD_HDR_IPTT_OFF		0
#define CMD_HDR_IPTT_MSK		(0xffff << CMD_HDR_IPTT_OFF)
/* dw6 */
#define CMD_HDR_DIF_SGL_LEN_OFF		0
#define CMD_HDR_DIF_SGL_LEN_MSK		(0xffff << CMD_HDR_DIF_SGL_LEN_OFF)
#define CMD_HDR_DATA_SGL_LEN_OFF	16
#define CMD_HDR_DATA_SGL_LEN_MSK	(0xffff << CMD_HDR_DATA_SGL_LEN_OFF)
/* dw7 */
#define CMD_HDR_ADDR_MODE_SEL_OFF		15
#define CMD_HDR_ADDR_MODE_SEL_MSK		(1 << CMD_HDR_ADDR_MODE_SEL_OFF)
#define CMD_HDR_ABORT_IPTT_OFF		16
#define CMD_HDR_ABORT_IPTT_MSK		(0xffff << CMD_HDR_ABORT_IPTT_OFF)

/* Completion header */
/* dw0 */
#define CMPLT_HDR_CMPLT_OFF		0
#define CMPLT_HDR_CMPLT_MSK		(0x3 << CMPLT_HDR_CMPLT_OFF)
#define CMPLT_HDR_ERROR_PHASE_OFF   2
#define CMPLT_HDR_ERROR_PHASE_MSK   (0xff << CMPLT_HDR_ERROR_PHASE_OFF)
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
/* dw3 */
#define CMPLT_HDR_IO_IN_TARGET_OFF	17
#define CMPLT_HDR_IO_IN_TARGET_MSK	(0x1 << CMPLT_HDR_IO_IN_TARGET_OFF)

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
#define ITCT_HDR_AWT_CONTINUE_OFF	25
#define ITCT_HDR_PORT_ID_OFF		28
#define ITCT_HDR_PORT_ID_MSK		(0xf << ITCT_HDR_PORT_ID_OFF)
/* qw2 */
#define ITCT_HDR_INLT_OFF		0
#define ITCT_HDR_INLT_MSK		(0xffffULL << ITCT_HDR_INLT_OFF)
#define ITCT_HDR_RTOLT_OFF		48
#define ITCT_HDR_RTOLT_MSK		(0xffffULL << ITCT_HDR_RTOLT_OFF)

struct hisi_sas_protect_iu_v3_hw {
	u32 dw0;
	u32 lbrtcv;
	u32 lbrtgv;
	u32 dw3;
	u32 dw4;
	u32 dw5;
	u32 rsv;
};

struct hisi_sas_complete_v3_hdr {
	__le32 dw0;
	__le32 dw1;
	__le32 act;
	__le32 dw3;
};

struct hisi_sas_err_record_v3 {
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

#define RX_DATA_LEN_UNDERFLOW_OFF	6
#define RX_DATA_LEN_UNDERFLOW_MSK	(1 << RX_DATA_LEN_UNDERFLOW_OFF)

#define HISI_SAS_COMMAND_ENTRIES_V3_HW 4096
#define HISI_SAS_MSI_COUNT_V3_HW 32

#define DIR_NO_DATA 0
#define DIR_TO_INI 1
#define DIR_TO_DEVICE 2
#define DIR_RESERVED 3

#define FIS_CMD_IS_UNCONSTRAINED(fis) \
	((fis.command == ATA_CMD_READ_LOG_EXT) || \
	(fis.command == ATA_CMD_READ_LOG_DMA_EXT) || \
	((fis.command == ATA_CMD_DEV_RESET) && \
	((fis.control & ATA_SRST) != 0)))

#define T10_INSRT_EN_OFF    0
#define T10_INSRT_EN_MSK    (1 << T10_INSRT_EN_OFF)
#define T10_RMV_EN_OFF	    1
#define T10_RMV_EN_MSK	    (1 << T10_RMV_EN_OFF)
#define T10_RPLC_EN_OFF	    2
#define T10_RPLC_EN_MSK	    (1 << T10_RPLC_EN_OFF)
#define T10_CHK_EN_OFF	    3
#define T10_CHK_EN_MSK	    (1 << T10_CHK_EN_OFF)
#define INCR_LBRT_OFF	    5
#define INCR_LBRT_MSK	    (1 << INCR_LBRT_OFF)
#define USR_DATA_BLOCK_SZ_OFF	20
#define USR_DATA_BLOCK_SZ_MSK	(0x3 << USR_DATA_BLOCK_SZ_OFF)
#define T10_CHK_MSK_OFF	    16
#define T10_CHK_REF_TAG_MSK (0xf0 << T10_CHK_MSK_OFF)
#define T10_CHK_APP_TAG_MSK (0xc << T10_CHK_MSK_OFF)

#define BASE_VECTORS_V3_HW  16
#define MIN_AFFINE_VECTORS_V3_HW  (BASE_VECTORS_V3_HW + 1)

enum {
	DSM_FUNC_ERR_HANDLE_MSI = 0,
};

static bool hisi_sas_intr_conv;
MODULE_PARM_DESC(intr_conv, "interrupt converge enable (0-1)");

/* permit overriding the host protection capabilities mask (EEDP/T10 PI) */
static int prot_mask;
module_param(prot_mask, int, 0);
MODULE_PARM_DESC(prot_mask, " host protection capabilities mask, def=0x0 ");

static bool auto_affine_msi_experimental;
module_param(auto_affine_msi_experimental, bool, 0444);
MODULE_PARM_DESC(auto_affine_msi_experimental, "Enable auto-affinity of MSI IRQs as experimental:\n"
		 "default is off");

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

#define hisi_sas_read32_poll_timeout(off, val, cond, delay_us,		\
				     timeout_us)			\
({									\
	void __iomem *regs = hisi_hba->regs + off;			\
	readl_poll_timeout(regs, val, cond, delay_us, timeout_us);	\
})

#define hisi_sas_read32_poll_timeout_atomic(off, val, cond, delay_us,	\
					    timeout_us)			\
({									\
	void __iomem *regs = hisi_hba->regs + off;			\
	readl_poll_timeout_atomic(regs, val, cond, delay_us, timeout_us);\
})

static void init_reg_v3_hw(struct hisi_hba *hisi_hba)
{
	int i;

	/* Global registers init */
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE,
			 (u32)((1ULL << hisi_hba->queue_count) - 1));
	hisi_sas_write32(hisi_hba, SAS_AXI_USER3, 0);
	hisi_sas_write32(hisi_hba, CFG_MAX_TAG, 0xfff0400);
	hisi_sas_write32(hisi_hba, HGC_SAS_TXFAIL_RETRY_CTRL, 0x108);
	hisi_sas_write32(hisi_hba, CFG_AGING_TIME, 0x1);
	hisi_sas_write32(hisi_hba, INT_COAL_EN, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT, 0x1);
	hisi_sas_write32(hisi_hba, CQ_INT_CONVERGE_EN,
			 hisi_sas_intr_conv);
	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 0xffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0xfefefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0xfefefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0xffc220ff);
	hisi_sas_write32(hisi_hba, CHNL_PHYUPDOWN_INT_MSK, 0x0);
	hisi_sas_write32(hisi_hba, CHNL_ENT_INT_MSK, 0x0);
	hisi_sas_write32(hisi_hba, HGC_COM_INT_MSK, 0x0);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0x155555);
	hisi_sas_write32(hisi_hba, AWQOS_AWCACHE_CFG, 0xf0f0);
	hisi_sas_write32(hisi_hba, ARQOS_ARCACHE_CFG, 0xf0f0);
	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK+0x4*i, 0);

	hisi_sas_write32(hisi_hba, HYPER_STREAM_ID_EN_CFG, 1);

	for (i = 0; i < hisi_hba->n_phy; i++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[i];
		struct asd_sas_phy *sas_phy = &phy->sas_phy;
		u32 prog_phy_link_rate = 0x800;

		if (!sas_phy->phy || (sas_phy->phy->maximum_linkrate <
				SAS_LINK_RATE_1_5_GBPS)) {
			prog_phy_link_rate = 0x855;
		} else {
			enum sas_linkrate max = sas_phy->phy->maximum_linkrate;

			prog_phy_link_rate =
				hisi_sas_get_prog_phy_linkrate_mask(max) |
				0x800;
		}
		hisi_sas_phy_write32(hisi_hba, i, PROG_PHY_LINK_RATE,
			prog_phy_link_rate);
		hisi_sas_phy_write32(hisi_hba, i, SERDES_CFG, 0xffc00);
		hisi_sas_phy_write32(hisi_hba, i, SAS_RX_TRAIN_TIMER, 0x13e80);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, RXOP_CHECK_CFG_H, 0x1000);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xf2057fff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0xffffbfe);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_DWS_RESET_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_OOB_RESTART_MSK, 0x1);
		hisi_sas_phy_write32(hisi_hba, i, STP_LINK_TIMER, 0x7f7a120);
		hisi_sas_phy_write32(hisi_hba, i, CON_CFG_DRIVER, 0x2a0a01);
		hisi_sas_phy_write32(hisi_hba, i, SAS_SSP_CON_TIMER_CFG, 0x32);
		hisi_sas_phy_write32(hisi_hba, i, SAS_EC_INT_COAL_TIME,
				     0x30f4240);
		/* used for 12G negotiate */
		hisi_sas_phy_write32(hisi_hba, i, COARSETUNE_TIME, 0x1e);
		hisi_sas_phy_write32(hisi_hba, i, AIP_LIMIT, 0x2ffff);
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

	/* RAS registers init */
	hisi_sas_write32(hisi_hba, SAS_RAS_INTR0_MASK, 0x0);
	hisi_sas_write32(hisi_hba, SAS_RAS_INTR1_MASK, 0x0);
	hisi_sas_write32(hisi_hba, SAS_RAS_INTR2_MASK, 0x0);
	hisi_sas_write32(hisi_hba, CFG_SAS_RAS_INTR_MASK, 0x0);

	/* LED registers init */
	hisi_sas_write32(hisi_hba, SAS_CFG_DRIVE_VLD, 0x80000ff);
	hisi_sas_write32(hisi_hba, SAS_GPIO_TX_0_1, 0x80808080);
	hisi_sas_write32(hisi_hba, SAS_GPIO_TX_0_1 + 0x4, 0x80808080);
	/* Configure blink generator rate A to 1Hz and B to 4Hz */
	hisi_sas_write32(hisi_hba, SAS_GPIO_CFG_1, 0x121700);
	hisi_sas_write32(hisi_hba, SAS_GPIO_CFG_0, 0x800000);
}

static void config_phy_opt_mode_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg &= ~PHY_CFG_DC_OPT_MSK;
	cfg |= 1 << PHY_CFG_DC_OPT_OFF;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void config_id_frame_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
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

static void setup_itct_v3_hw(struct hisi_hba *hisi_hba,
			     struct hisi_sas_device *sas_dev)
{
	struct domain_device *device = sas_dev->sas_device;
	struct device *dev = hisi_hba->dev;
	u64 qw0, device_id = sas_dev->device_id;
	struct hisi_sas_itct *itct = &hisi_hba->itct[device_id];
	struct domain_device *parent_dev = device->parent;
	struct asd_sas_port *sas_port = device->port;
	struct hisi_sas_port *port = to_hisi_sas_port(sas_port);
	u64 sas_addr;

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
		if (parent_dev && dev_is_expander(parent_dev->dev_type))
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
		(0xfa << ITCT_HDR_SMP_TIMEOUT_OFF) |
		(1 << ITCT_HDR_AWT_CONTINUE_OFF) |
		(port->id << ITCT_HDR_PORT_ID_OFF));
	itct->qw0 = cpu_to_le64(qw0);

	/* qw1 */
	memcpy(&sas_addr, device->sas_addr, SAS_ADDR_SIZE);
	itct->sas_addr = cpu_to_le64(__swab64(sas_addr));

	/* qw2 */
	if (!dev_is_sata(device))
		itct->qw2 = cpu_to_le64((5000ULL << ITCT_HDR_INLT_OFF) |
					(0x1ULL << ITCT_HDR_RTOLT_OFF));
}

static void clear_itct_v3_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_device *sas_dev)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	u64 dev_id = sas_dev->device_id;
	struct hisi_sas_itct *itct = &hisi_hba->itct[dev_id];
	u32 reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);

	sas_dev->completion = &completion;

	/* clear the itct interrupt state */
	if (ENT_INT_SRC3_ITC_INT_MSK & reg_val)
		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 ENT_INT_SRC3_ITC_INT_MSK);

	/* clear the itct table */
	reg_val = ITCT_CLR_EN_MSK | (dev_id & ITCT_DEV_MSK);
	hisi_sas_write32(hisi_hba, ITCT_CLR, reg_val);

	wait_for_completion(sas_dev->completion);
	memset(itct, 0, sizeof(struct hisi_sas_itct));
}

static void dereg_device_v3_hw(struct hisi_hba *hisi_hba,
				struct domain_device *device)
{
	struct hisi_sas_slot *slot, *slot2;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	u32 cfg_abt_set_query_iptt;

	cfg_abt_set_query_iptt = hisi_sas_read32(hisi_hba,
		CFG_ABT_SET_QUERY_IPTT);
	list_for_each_entry_safe(slot, slot2, &sas_dev->list, entry) {
		cfg_abt_set_query_iptt &= ~CFG_SET_ABORTED_IPTT_MSK;
		cfg_abt_set_query_iptt |= (1 << CFG_SET_ABORTED_EN_OFF) |
			(slot->idx << CFG_SET_ABORTED_IPTT_OFF);
		hisi_sas_write32(hisi_hba, CFG_ABT_SET_QUERY_IPTT,
			cfg_abt_set_query_iptt);
	}
	cfg_abt_set_query_iptt &= ~(1 << CFG_SET_ABORTED_EN_OFF);
	hisi_sas_write32(hisi_hba, CFG_ABT_SET_QUERY_IPTT,
		cfg_abt_set_query_iptt);
	hisi_sas_write32(hisi_hba, CFG_ABT_SET_IPTT_DONE,
					1 << CFG_ABT_SET_IPTT_DONE_OFF);
}

static int reset_hw_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int ret;
	u32 val;

	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0);

	/* Disable all of the PHYs */
	hisi_sas_stop_phys(hisi_hba);
	udelay(50);

	/* Ensure axi bus idle */
	ret = hisi_sas_read32_poll_timeout(AXI_CFG, val, !val,
					   20000, 1000000);
	if (ret) {
		dev_err(dev, "axi bus is not idle, ret = %d!\n", ret);
		return -EIO;
	}

	if (ACPI_HANDLE(dev)) {
		acpi_status s;

		s = acpi_evaluate_object(ACPI_HANDLE(dev), "_RST", NULL, NULL);
		if (ACPI_FAILURE(s)) {
			dev_err(dev, "Reset failed\n");
			return -EIO;
		}
	} else {
		dev_err(dev, "no reset method!\n");
		return -EINVAL;
	}

	return 0;
}

static int hw_init_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	union acpi_object *obj;
	guid_t guid;
	int rc;

	rc = reset_hw_v3_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "hisi_sas_reset_hw failed, rc=%d", rc);
		return rc;
	}

	msleep(100);
	init_reg_v3_hw(hisi_hba);

	if (guid_parse("D5918B4B-37AE-4E10-A99F-E5E8A6EF4C1F", &guid)) {
		dev_err(dev, "Parse GUID failed\n");
		return -EINVAL;
	}

	/* Switch over to MSI handling , from PCI AER default */
	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &guid, 0,
				DSM_FUNC_ERR_HANDLE_MSI, NULL);
	if (!obj)
		dev_warn(dev, "Switch over to MSI handling failed\n");
	else
		ACPI_FREE(obj);

	return 0;
}

static void enable_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg |= PHY_CFG_ENA_MSK;
	cfg &= ~PHY_CFG_PHY_RST_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void disable_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);
	u32 irq_msk = hisi_sas_phy_read32(hisi_hba, phy_no, CHL_INT2_MSK);
	static const u32 msk = BIT(CHL_INT2_RX_DISP_ERR_OFF) |
			       BIT(CHL_INT2_RX_CODE_ERR_OFF) |
			       BIT(CHL_INT2_RX_INVLD_DW_OFF);
	u32 state;

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT2_MSK, msk | irq_msk);

	cfg &= ~PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);

	mdelay(50);

	state = hisi_sas_read32(hisi_hba, PHY_STATE);
	if (state & BIT(phy_no)) {
		cfg |= PHY_CFG_PHY_RST_MSK;
		hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
	}

	udelay(1);

	hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_INVLD_DW);
	hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_DISP_ERR);
	hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_CODE_ERR);

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT2, msk);
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT2_MSK, irq_msk);
}

static void start_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	config_id_frame_v3_hw(hisi_hba, phy_no);
	config_phy_opt_mode_v3_hw(hisi_hba, phy_no);
	enable_phy_v3_hw(hisi_hba, phy_no);
}

static void phy_hard_reset_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	u32 txid_auto;

	hisi_sas_phy_enable(hisi_hba, phy_no, 0);
	if (phy->identify.device_type == SAS_END_DEVICE) {
		txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
		hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
					txid_auto | TX_HARDRST_MSK);
	}
	msleep(100);
	hisi_sas_phy_enable(hisi_hba, phy_no, 1);
}

static enum sas_linkrate phy_get_max_linkrate_v3_hw(void)
{
	return SAS_LINK_RATE_12_0_GBPS;
}

static void phys_init_v3_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[i];
		struct asd_sas_phy *sas_phy = &phy->sas_phy;

		if (!sas_phy->phy->enabled)
			continue;

		hisi_sas_phy_enable(hisi_hba, i, 1);
	}
}

static void sl_notify_ssp_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
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

static int get_wideport_bitmap_v3_hw(struct hisi_hba *hisi_hba, int port_id)
{
	int i, bitmap = 0;
	u32 phy_port_num_ma = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
	u32 phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);

	for (i = 0; i < hisi_hba->n_phy; i++)
		if (phy_state & BIT(i))
			if (((phy_port_num_ma >> (i * 4)) & 0xf) == port_id)
				bitmap |= BIT(i);

	return bitmap;
}

/**
 * The callpath to this function and upto writing the write
 * queue pointer should be safe from interruption.
 */
static int
get_free_slot_v3_hw(struct hisi_hba *hisi_hba, struct hisi_sas_dq *dq)
{
	struct device *dev = hisi_hba->dev;
	int queue = dq->id;
	u32 r, w;

	w = dq->wr_point;
	r = hisi_sas_read32_relaxed(hisi_hba,
				DLVRY_Q_0_RD_PTR + (queue * 0x14));
	if (r == (w+1) % HISI_SAS_QUEUE_SLOTS) {
		dev_warn(dev, "full queue=%d r=%d w=%d\n",
			 queue, r, w);
		return -EAGAIN;
	}

	dq->wr_point = (dq->wr_point + 1) % HISI_SAS_QUEUE_SLOTS;

	return w;
}

static void start_delivery_v3_hw(struct hisi_sas_dq *dq)
{
	struct hisi_hba *hisi_hba = dq->hisi_hba;
	struct hisi_sas_slot *s, *s1, *s2 = NULL;
	int dlvry_queue = dq->id;
	int wp;

	list_for_each_entry_safe(s, s1, &dq->list, delivery) {
		if (!s->ready)
			break;
		s2 = s;
		list_del(&s->delivery);
	}

	if (!s2)
		return;

	/*
	 * Ensure that memories for slots built on other CPUs is observed.
	 */
	smp_rmb();
	wp = (s2->dlvry_queue_slot + 1) % HISI_SAS_QUEUE_SLOTS;

	hisi_sas_write32(hisi_hba, DLVRY_Q_0_WR_PTR + (dlvry_queue * 0x14), wp);
}

static void prep_prd_sge_v3_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_slot *slot,
			      struct hisi_sas_cmd_hdr *hdr,
			      struct scatterlist *scatter,
			      int n_elem)
{
	struct hisi_sas_sge_page *sge_page = hisi_sas_sge_addr_mem(slot);
	struct scatterlist *sg;
	int i;

	for_each_sg(scatter, sg, n_elem, i) {
		struct hisi_sas_sge *entry = &sge_page->sge[i];

		entry->addr = cpu_to_le64(sg_dma_address(sg));
		entry->page_ctrl_0 = entry->page_ctrl_1 = 0;
		entry->data_len = cpu_to_le32(sg_dma_len(sg));
		entry->data_off = 0;
	}

	hdr->prd_table_addr = cpu_to_le64(hisi_sas_sge_addr_dma(slot));

	hdr->sg_len |= cpu_to_le32(n_elem << CMD_HDR_DATA_SGL_LEN_OFF);
}

static void prep_prd_sge_dif_v3_hw(struct hisi_hba *hisi_hba,
				   struct hisi_sas_slot *slot,
				   struct hisi_sas_cmd_hdr *hdr,
				   struct scatterlist *scatter,
				   int n_elem)
{
	struct hisi_sas_sge_dif_page *sge_dif_page;
	struct scatterlist *sg;
	int i;

	sge_dif_page = hisi_sas_sge_dif_addr_mem(slot);

	for_each_sg(scatter, sg, n_elem, i) {
		struct hisi_sas_sge *entry = &sge_dif_page->sge[i];

		entry->addr = cpu_to_le64(sg_dma_address(sg));
		entry->page_ctrl_0 = 0;
		entry->page_ctrl_1 = 0;
		entry->data_len = cpu_to_le32(sg_dma_len(sg));
		entry->data_off = 0;
	}

	hdr->dif_prd_table_addr =
		cpu_to_le64(hisi_sas_sge_dif_addr_dma(slot));

	hdr->sg_len |= cpu_to_le32(n_elem << CMD_HDR_DIF_SGL_LEN_OFF);
}

static u32 get_prot_chk_msk_v3_hw(struct scsi_cmnd *scsi_cmnd)
{
	unsigned char prot_flags = scsi_cmnd->prot_flags;

	if (prot_flags & SCSI_PROT_REF_CHECK)
		return T10_CHK_APP_TAG_MSK;
	return T10_CHK_REF_TAG_MSK | T10_CHK_APP_TAG_MSK;
}

static void fill_prot_v3_hw(struct scsi_cmnd *scsi_cmnd,
			    struct hisi_sas_protect_iu_v3_hw *prot)
{
	unsigned char prot_op = scsi_get_prot_op(scsi_cmnd);
	unsigned int interval = scsi_prot_interval(scsi_cmnd);
	u32 lbrt_chk_val = t10_pi_ref_tag(scsi_cmnd->request);

	switch (prot_op) {
	case SCSI_PROT_READ_INSERT:
		prot->dw0 |= T10_INSRT_EN_MSK;
		prot->lbrtgv = lbrt_chk_val;
		break;
	case SCSI_PROT_READ_STRIP:
		prot->dw0 |= (T10_RMV_EN_MSK | T10_CHK_EN_MSK);
		prot->lbrtcv = lbrt_chk_val;
		prot->dw4 |= get_prot_chk_msk_v3_hw(scsi_cmnd);
		break;
	case SCSI_PROT_READ_PASS:
		prot->dw0 |= T10_CHK_EN_MSK;
		prot->lbrtcv = lbrt_chk_val;
		prot->dw4 |= get_prot_chk_msk_v3_hw(scsi_cmnd);
		break;
	case SCSI_PROT_WRITE_INSERT:
		prot->dw0 |= T10_INSRT_EN_MSK;
		prot->lbrtgv = lbrt_chk_val;
		break;
	case SCSI_PROT_WRITE_STRIP:
		prot->dw0 |= (T10_RMV_EN_MSK | T10_CHK_EN_MSK);
		prot->lbrtcv = lbrt_chk_val;
		break;
	case SCSI_PROT_WRITE_PASS:
		prot->dw0 |= T10_CHK_EN_MSK;
		prot->lbrtcv = lbrt_chk_val;
		prot->dw4 |= get_prot_chk_msk_v3_hw(scsi_cmnd);
		break;
	default:
		WARN(1, "prot_op(0x%x) is not valid\n", prot_op);
		break;
	}

	switch (interval) {
	case 512:
		break;
	case 4096:
		prot->dw0 |= (0x1 << USR_DATA_BLOCK_SZ_OFF);
		break;
	case 520:
		prot->dw0 |= (0x2 << USR_DATA_BLOCK_SZ_OFF);
		break;
	default:
		WARN(1, "protection interval (0x%x) invalid\n",
		     interval);
		break;
	}

	prot->dw0 |= INCR_LBRT_MSK;
}

static void prep_ssp_v3_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port = slot->port;
	struct sas_ssp_task *ssp_task = &task->ssp_task;
	struct scsi_cmnd *scsi_cmnd = ssp_task->cmd;
	struct hisi_sas_tmf_task *tmf = slot->tmf;
	int has_data = 0, priority = !!tmf;
	unsigned char prot_op;
	u8 *buf_cmd;
	u32 dw1 = 0, dw2 = 0, len = 0;

	hdr->dw0 = cpu_to_le32((1 << CMD_HDR_RESP_REPORT_OFF) |
			       (2 << CMD_HDR_TLR_CTRL_OFF) |
			       (port->id << CMD_HDR_PORT_OFF) |
			       (priority << CMD_HDR_PRIORITY_OFF) |
			       (1 << CMD_HDR_CMD_OFF)); /* ssp */

	dw1 = 1 << CMD_HDR_VDTL_OFF;
	if (tmf) {
		dw1 |= 2 << CMD_HDR_FRAME_TYPE_OFF;
		dw1 |= DIR_NO_DATA << CMD_HDR_DIR_OFF;
	} else {
		prot_op = scsi_get_prot_op(scsi_cmnd);
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

	dw2 = (((sizeof(struct ssp_command_iu) + sizeof(struct ssp_frame_hdr)
	      + 3) / 4) << CMD_HDR_CFL_OFF) |
	      ((HISI_SAS_MAX_SSP_RESP_SZ / 4) << CMD_HDR_MRFL_OFF) |
	      (2 << CMD_HDR_SG_MOD_OFF);
	hdr->dw2 = cpu_to_le32(dw2);
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	if (has_data) {
		prep_prd_sge_v3_hw(hisi_hba, slot, hdr, task->scatter,
				   slot->n_elem);

		if (scsi_prot_sg_count(scsi_cmnd))
			prep_prd_sge_dif_v3_hw(hisi_hba, slot, hdr,
					       scsi_prot_sglist(scsi_cmnd),
					       slot->n_elem_dif);
	}

	hdr->cmd_table_addr = cpu_to_le64(hisi_sas_cmd_hdr_addr_dma(slot));
	hdr->sts_buffer_addr = cpu_to_le64(hisi_sas_status_buf_addr_dma(slot));

	buf_cmd = hisi_sas_cmd_hdr_addr_mem(slot) +
		sizeof(struct ssp_frame_hdr);

	memcpy(buf_cmd, &task->ssp_task.LUN, 8);
	if (!tmf) {
		buf_cmd[9] = ssp_task->task_attr | (ssp_task->task_prio << 3);
		memcpy(buf_cmd + 12, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);
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

	if (has_data && (prot_op != SCSI_PROT_NORMAL)) {
		struct hisi_sas_protect_iu_v3_hw prot;
		u8 *buf_cmd_prot;

		hdr->dw7 |= cpu_to_le32(1 << CMD_HDR_ADDR_MODE_SEL_OFF);
		dw1 |= CMD_HDR_PIR_MSK;
		buf_cmd_prot = hisi_sas_cmd_hdr_addr_mem(slot) +
			       sizeof(struct ssp_frame_hdr) +
			       sizeof(struct ssp_command_iu);

		memset(&prot, 0, sizeof(struct hisi_sas_protect_iu_v3_hw));
		fill_prot_v3_hw(scsi_cmnd, &prot);
		memcpy(buf_cmd_prot, &prot,
		       sizeof(struct hisi_sas_protect_iu_v3_hw));
		/*
		 * For READ, we need length of info read to memory, while for
		 * WRITE we need length of data written to the disk.
		 */
		if (prot_op == SCSI_PROT_WRITE_INSERT ||
		    prot_op == SCSI_PROT_READ_INSERT ||
		    prot_op == SCSI_PROT_WRITE_PASS ||
		    prot_op == SCSI_PROT_READ_PASS) {
			unsigned int interval = scsi_prot_interval(scsi_cmnd);
			unsigned int ilog2_interval = ilog2(interval);

			len = (task->total_xfer_len >> ilog2_interval) * 8;
		}
	}

	hdr->dw1 = cpu_to_le32(dw1);

	hdr->data_transfer_len = cpu_to_le32(task->total_xfer_len + len);
}

static void prep_smp_v3_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct hisi_sas_port *port = slot->port;
	struct scatterlist *sg_req;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	dma_addr_t req_dma_addr;
	unsigned int req_len;

	/* req */
	sg_req = &task->smp_task.smp_req;
	req_len = sg_dma_len(sg_req);
	req_dma_addr = sg_dma_address(sg_req);

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

}

static void prep_ata_v3_hw(struct hisi_hba *hisi_hba,
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
	int has_data = 0, hdr_tag = 0;
	u32 dw1 = 0, dw2 = 0;

	hdr->dw0 = cpu_to_le32(port->id << CMD_HDR_PORT_OFF);
	if (parent_dev && dev_is_expander(parent_dev->dev_type))
		hdr->dw0 |= cpu_to_le32(3 << CMD_HDR_CMD_OFF);
	else
		hdr->dw0 |= cpu_to_le32(4U << CMD_HDR_CMD_OFF);

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

	if (FIS_CMD_IS_UNCONSTRAINED(task->ata_task.fis))
		dw1 |= 1 << CMD_HDR_UNCON_CMD_OFF;

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

	if (has_data)
		prep_prd_sge_v3_hw(hisi_hba, slot, hdr, task->scatter,
					slot->n_elem);

	hdr->data_transfer_len = cpu_to_le32(task->total_xfer_len);
	hdr->cmd_table_addr = cpu_to_le64(hisi_sas_cmd_hdr_addr_dma(slot));
	hdr->sts_buffer_addr = cpu_to_le64(hisi_sas_status_buf_addr_dma(slot));

	buf_cmd = hisi_sas_cmd_hdr_addr_mem(slot);

	if (likely(!task->ata_task.device_control_reg_update))
		task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));
}

static void prep_abort_v3_hw(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	struct sas_task *task = slot->task;
	struct domain_device *dev = task->dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct hisi_sas_port *port = slot->port;

	/* dw0 */
	hdr->dw0 = cpu_to_le32((5U << CMD_HDR_CMD_OFF) | /*abort*/
			       (port->id << CMD_HDR_PORT_OFF) |
				   (dev_is_sata(dev)
					<< CMD_HDR_ABORT_DEVICE_TYPE_OFF) |
					(abort_flag
					 << CMD_HDR_ABORT_FLAG_OFF));

	/* dw1 */
	hdr->dw1 = cpu_to_le32(device_id
			<< CMD_HDR_DEV_ID_OFF);

	/* dw7 */
	hdr->dw7 = cpu_to_le32(tag_to_abort << CMD_HDR_ABORT_IPTT_OFF);
	hdr->transfer_tags = cpu_to_le32(slot->idx);

}

static irqreturn_t phy_up_v3_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	int i;
	irqreturn_t res;
	u32 context, port_id, link_rate;
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = hisi_hba->dev;
	unsigned long flags;

	del_timer(&phy->timer);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 1);

	port_id = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
	port_id = (port_id >> (4 * phy_no)) & 0xf;
	link_rate = hisi_sas_read32(hisi_hba, PHY_CONN_RATE);
	link_rate = (link_rate >> (phy_no * 4)) & 0xf;

	if (port_id == 0xf) {
		dev_err(dev, "phyup: phy%d invalid portid\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}
	sas_phy->linkrate = link_rate;
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);

	/* Check for SATA dev */
	context = hisi_sas_read32(hisi_hba, PHY_CONTEXT);
	if (context & (1 << phy_no)) {
		struct hisi_sas_initial_fis *initial_fis;
		struct dev_to_host_fis *fis;
		u8 attached_sas_addr[SAS_ADDR_SIZE] = {0};
		struct Scsi_Host *shost = hisi_hba->shost;

		dev_info(dev, "phyup: phy%d link_rate=%d(sata)\n", phy_no, link_rate);
		initial_fis = &hisi_hba->initial_fis[phy_no];
		fis = &initial_fis->fis;

		/* check ERR bit of Status Register */
		if (fis->status & ATA_ERR) {
			dev_warn(dev, "sata int: phy%d FIS status: 0x%x\n",
				 phy_no, fis->status);
			hisi_sas_notify_phy_event(phy, HISI_PHYE_LINK_RESET);
			res = IRQ_NONE;
			goto end;
		}

		sas_phy->oob_mode = SATA_OOB_MODE;
		attached_sas_addr[0] = 0x50;
		attached_sas_addr[6] = shost->host_no;
		attached_sas_addr[7] = phy_no;
		memcpy(sas_phy->attached_sas_addr,
		       attached_sas_addr,
		       SAS_ADDR_SIZE);
		memcpy(sas_phy->frame_rcvd, fis,
		       sizeof(struct dev_to_host_fis));
		phy->phy_type |= PORT_TYPE_SATA;
		phy->identify.device_type = SAS_SATA_DEV;
		phy->frame_rcvd_size = sizeof(struct dev_to_host_fis);
		phy->identify.target_port_protocols = SAS_PROTOCOL_SATA;
	} else {
		u32 *frame_rcvd = (u32 *)sas_phy->frame_rcvd;
		struct sas_identify_frame *id =
			(struct sas_identify_frame *)frame_rcvd;

		dev_info(dev, "phyup: phy%d link_rate=%d\n", phy_no, link_rate);
		for (i = 0; i < 6; i++) {
			u32 idaf = hisi_sas_phy_read32(hisi_hba, phy_no,
					       RX_IDAF_DWORD0 + (i * 4));
			frame_rcvd[i] = __swab32(idaf);
		}
		sas_phy->oob_mode = SAS_OOB_MODE;
		memcpy(sas_phy->attached_sas_addr,
		       &id->sas_addr,
		       SAS_ADDR_SIZE);
		phy->phy_type |= PORT_TYPE_SAS;
		phy->identify.device_type = id->dev_type;
		phy->frame_rcvd_size = sizeof(struct sas_identify_frame);
		if (phy->identify.device_type == SAS_END_DEVICE)
			phy->identify.target_port_protocols =
				SAS_PROTOCOL_SSP;
		else if (phy->identify.device_type != SAS_PHY_UNUSED)
			phy->identify.target_port_protocols =
				SAS_PROTOCOL_SMP;
	}

	phy->port_id = port_id;
	phy->phy_attached = 1;
	hisi_sas_notify_phy_event(phy, HISI_PHYE_PHY_UP);
	res = IRQ_HANDLED;
	spin_lock_irqsave(&phy->lock, flags);
	if (phy->reset_completion) {
		phy->in_reset = 0;
		complete(phy->reset_completion);
	}
	spin_unlock_irqrestore(&phy->lock, flags);
end:
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_PHY_ENABLE_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 0);

	return res;
}

static irqreturn_t phy_down_v3_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	u32 phy_state, sl_ctrl, txid_auto;
	struct device *dev = hisi_hba->dev;

	del_timer(&phy->timer);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 1);

	phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);
	dev_info(dev, "phydown: phy%d phy_state=0x%x\n", phy_no, phy_state);
	hisi_sas_phy_down(hisi_hba, phy_no, (phy_state & 1 << phy_no) ? 1 : 0);

	sl_ctrl = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL,
						sl_ctrl&(~SL_CTA_MSK));

	txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
	hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
						txid_auto | CT3_MSK);

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0, CHL_INT0_NOT_RDY_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 0);

	return IRQ_HANDLED;
}

static irqreturn_t phy_bcast_v3_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	u32 bcast_status;

	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 1);
	bcast_status = hisi_sas_phy_read32(hisi_hba, phy_no, RX_PRIMS_STATUS);
	if ((bcast_status & RX_BCAST_CHG_MSK) &&
	    !test_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags))
		sas_ha->notify_port_event(sas_phy, PORTE_BROADCAST_RCVD);
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_RX_BCST_ACK_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 0);

	return IRQ_HANDLED;
}

static irqreturn_t int_phy_up_down_bcast_v3_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_msk;
	int phy_no = 0;
	irqreturn_t res = IRQ_NONE;

	irq_msk = hisi_sas_read32(hisi_hba, CHNL_INT_STATUS)
				& 0x11111111;
	while (irq_msk) {
		if (irq_msk  & 1) {
			u32 irq_value = hisi_sas_phy_read32(hisi_hba, phy_no,
							    CHL_INT0);
			u32 phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);
			int rdy = phy_state & (1 << phy_no);

			if (rdy) {
				if (irq_value & CHL_INT0_SL_PHY_ENABLE_MSK)
					/* phy up */
					if (phy_up_v3_hw(phy_no, hisi_hba)
							== IRQ_HANDLED)
						res = IRQ_HANDLED;
				if (irq_value & CHL_INT0_SL_RX_BCST_ACK_MSK)
					/* phy bcast */
					if (phy_bcast_v3_hw(phy_no, hisi_hba)
							== IRQ_HANDLED)
						res = IRQ_HANDLED;
			} else {
				if (irq_value & CHL_INT0_NOT_RDY_MSK)
					/* phy down */
					if (phy_down_v3_hw(phy_no, hisi_hba)
							== IRQ_HANDLED)
						res = IRQ_HANDLED;
			}
		}
		irq_msk >>= 4;
		phy_no++;
	}

	return res;
}

static const struct hisi_sas_hw_error port_axi_error[] = {
	{
		.irq_msk = BIT(CHL_INT1_DMAC_TX_ECC_MB_ERR_OFF),
		.msg = "dmac_tx_ecc_bad_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_RX_ECC_MB_ERR_OFF),
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
	{
		.irq_msk = BIT(CHL_INT1_DMAC_TX_FIFO_ERR_OFF),
		.msg = "dma_tx_fifo_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_RX_FIFO_ERR_OFF),
		.msg = "dma_rx_fifo_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_TX_AXI_RUSER_ERR_OFF),
		.msg = "dma_tx_axi_ruser_err",
	},
	{
		.irq_msk = BIT(CHL_INT1_DMAC_RX_AXI_RUSER_ERR_OFF),
		.msg = "dma_rx_axi_ruser_err",
	},
};

static void handle_chl_int1_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 irq_value = hisi_sas_phy_read32(hisi_hba, phy_no, CHL_INT1);
	u32 irq_msk = hisi_sas_phy_read32(hisi_hba, phy_no, CHL_INT1_MSK);
	struct device *dev = hisi_hba->dev;
	int i;

	irq_value &= ~irq_msk;
	if (!irq_value)
		return;

	for (i = 0; i < ARRAY_SIZE(port_axi_error); i++) {
		const struct hisi_sas_hw_error *error = &port_axi_error[i];

		if (!(irq_value & error->irq_msk))
			continue;

		dev_err(dev, "%s error (phy%d 0x%x) found!\n",
			error->msg, phy_no, irq_value);
		queue_work(hisi_hba->wq, &hisi_hba->rst_work);
	}

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT1, irq_value);
}

static void phy_get_events_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_phy *sphy = sas_phy->phy;
	unsigned long flags;
	u32 reg_value;

	spin_lock_irqsave(&phy->lock, flags);

	/* loss dword sync */
	reg_value = hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_DWS_LOST);
	sphy->loss_of_dword_sync_count += reg_value;

	/* phy reset problem */
	reg_value = hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_RESET_PROB);
	sphy->phy_reset_problem_count += reg_value;

	/* invalid dword */
	reg_value = hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_INVLD_DW);
	sphy->invalid_dword_count += reg_value;

	/* disparity err */
	reg_value = hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_DISP_ERR);
	sphy->running_disparity_error_count += reg_value;

	/* code violation error */
	reg_value = hisi_sas_phy_read32(hisi_hba, phy_no, ERR_CNT_CODE_ERR);
	phy->code_violation_err_count += reg_value;

	spin_unlock_irqrestore(&phy->lock, flags);
}

static void handle_chl_int2_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 irq_msk = hisi_sas_phy_read32(hisi_hba, phy_no, CHL_INT2_MSK);
	u32 irq_value = hisi_sas_phy_read32(hisi_hba, phy_no, CHL_INT2);
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct pci_dev *pci_dev = hisi_hba->pci_dev;
	struct device *dev = hisi_hba->dev;
	static const u32 msk = BIT(CHL_INT2_RX_DISP_ERR_OFF) |
			BIT(CHL_INT2_RX_CODE_ERR_OFF) |
			BIT(CHL_INT2_RX_INVLD_DW_OFF);

	irq_value &= ~irq_msk;
	if (!irq_value)
		return;

	if (irq_value & BIT(CHL_INT2_SL_IDAF_TOUT_CONF_OFF)) {
		dev_warn(dev, "phy%d identify timeout\n", phy_no);
		hisi_sas_notify_phy_event(phy, HISI_PHYE_LINK_RESET);
	}

	if (irq_value & BIT(CHL_INT2_STP_LINK_TIMEOUT_OFF)) {
		u32 reg_value = hisi_sas_phy_read32(hisi_hba, phy_no,
				STP_LINK_TIMEOUT_STATE);

		dev_warn(dev, "phy%d stp link timeout (0x%x)\n",
			 phy_no, reg_value);
		if (reg_value & BIT(4))
			hisi_sas_notify_phy_event(phy, HISI_PHYE_LINK_RESET);
	}

	if (pci_dev->revision > 0x20 && (irq_value & msk)) {
		struct asd_sas_phy *sas_phy = &phy->sas_phy;
		struct sas_phy *sphy = sas_phy->phy;

		phy_get_events_v3_hw(hisi_hba, phy_no);

		if (irq_value & BIT(CHL_INT2_RX_INVLD_DW_OFF))
			dev_info(dev, "phy%d invalid dword cnt:   %u\n", phy_no,
				 sphy->invalid_dword_count);

		if (irq_value & BIT(CHL_INT2_RX_CODE_ERR_OFF))
			dev_info(dev, "phy%d code violation cnt:  %u\n", phy_no,
				 phy->code_violation_err_count);

		if (irq_value & BIT(CHL_INT2_RX_DISP_ERR_OFF))
			dev_info(dev, "phy%d disparity error cnt: %u\n", phy_no,
				 sphy->running_disparity_error_count);
	}

	if ((irq_value & BIT(CHL_INT2_RX_INVLD_DW_OFF)) &&
	    (pci_dev->revision == 0x20)) {
		u32 reg_value;
		int rc;

		rc = hisi_sas_read32_poll_timeout_atomic(
				HILINK_ERR_DFX, reg_value,
				!((reg_value >> 8) & BIT(phy_no)),
				1000, 10000);
		if (rc)
			hisi_sas_notify_phy_event(phy, HISI_PHYE_LINK_RESET);
	}

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT2, irq_value);
}

static void handle_chl_int0_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 irq_value0 = hisi_sas_phy_read32(hisi_hba, phy_no, CHL_INT0);

	if (irq_value0 & CHL_INT0_PHY_RDY_MSK)
		hisi_sas_phy_oob_ready(hisi_hba, phy_no);

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     irq_value0 & (~CHL_INT0_SL_RX_BCST_ACK_MSK)
			     & (~CHL_INT0_SL_PHY_ENABLE_MSK)
			     & (~CHL_INT0_NOT_RDY_MSK));
}

static irqreturn_t int_chnl_int_v3_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_msk;
	int phy_no = 0;

	irq_msk = hisi_sas_read32(hisi_hba, CHNL_INT_STATUS)
				& 0xeeeeeeee;

	while (irq_msk) {
		if (irq_msk & (2 << (phy_no * 4)))
			handle_chl_int0_v3_hw(hisi_hba, phy_no);

		if (irq_msk & (4 << (phy_no * 4)))
			handle_chl_int1_v3_hw(hisi_hba, phy_no);

		if (irq_msk & (8 << (phy_no * 4)))
			handle_chl_int2_v3_hw(hisi_hba, phy_no);

		irq_msk &= ~(0xe << (phy_no * 4));
		phy_no++;
	}

	return IRQ_HANDLED;
}

static const struct hisi_sas_hw_error multi_bit_ecc_errors[] = {
	{
		.irq_msk = BIT(SAS_ECC_INTR_DQE_ECC_MB_OFF),
		.msk = HGC_DQE_ECC_MB_ADDR_MSK,
		.shift = HGC_DQE_ECC_MB_ADDR_OFF,
		.msg = "hgc_dqe_eccbad_intr",
		.reg = HGC_DQE_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_IOST_ECC_MB_OFF),
		.msk = HGC_IOST_ECC_MB_ADDR_MSK,
		.shift = HGC_IOST_ECC_MB_ADDR_OFF,
		.msg = "hgc_iost_eccbad_intr",
		.reg = HGC_IOST_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_ITCT_ECC_MB_OFF),
		.msk = HGC_ITCT_ECC_MB_ADDR_MSK,
		.shift = HGC_ITCT_ECC_MB_ADDR_OFF,
		.msg = "hgc_itct_eccbad_intr",
		.reg = HGC_ITCT_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_IOSTLIST_ECC_MB_OFF),
		.msk = HGC_LM_DFX_STATUS2_IOSTLIST_MSK,
		.shift = HGC_LM_DFX_STATUS2_IOSTLIST_OFF,
		.msg = "hgc_iostl_eccbad_intr",
		.reg = HGC_LM_DFX_STATUS2,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_ITCTLIST_ECC_MB_OFF),
		.msk = HGC_LM_DFX_STATUS2_ITCTLIST_MSK,
		.shift = HGC_LM_DFX_STATUS2_ITCTLIST_OFF,
		.msg = "hgc_itctl_eccbad_intr",
		.reg = HGC_LM_DFX_STATUS2,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_CQE_ECC_MB_OFF),
		.msk = HGC_CQE_ECC_MB_ADDR_MSK,
		.shift = HGC_CQE_ECC_MB_ADDR_OFF,
		.msg = "hgc_cqe_eccbad_intr",
		.reg = HGC_CQE_ECC_ADDR,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM0_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM0_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM0_OFF,
		.msg = "rxm_mem0_eccbad_intr",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM1_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM1_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM1_OFF,
		.msg = "rxm_mem1_eccbad_intr",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM2_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS14_MEM2_MSK,
		.shift = HGC_RXM_DFX_STATUS14_MEM2_OFF,
		.msg = "rxm_mem2_eccbad_intr",
		.reg = HGC_RXM_DFX_STATUS14,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_NCQ_MEM3_ECC_MB_OFF),
		.msk = HGC_RXM_DFX_STATUS15_MEM3_MSK,
		.shift = HGC_RXM_DFX_STATUS15_MEM3_OFF,
		.msg = "rxm_mem3_eccbad_intr",
		.reg = HGC_RXM_DFX_STATUS15,
	},
	{
		.irq_msk = BIT(SAS_ECC_INTR_OOO_RAM_ECC_MB_OFF),
		.msk = AM_ROB_ECC_ERR_ADDR_MSK,
		.shift = AM_ROB_ECC_ERR_ADDR_OFF,
		.msg = "ooo_ram_eccbad_intr",
		.reg = AM_ROB_ECC_ERR_ADDR,
	},
};

static void multi_bit_ecc_error_process_v3_hw(struct hisi_hba *hisi_hba,
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
			dev_err(dev, "%s (0x%x) found: mem addr is 0x%08X\n",
				ecc_error->msg, irq_value, val);
			queue_work(hisi_hba->wq, &hisi_hba->rst_work);
		}
	}
}

static void fatal_ecc_int_v3_hw(struct hisi_hba *hisi_hba)
{
	u32 irq_value, irq_msk;

	irq_msk = hisi_sas_read32(hisi_hba, SAS_ECC_INTR_MSK);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, irq_msk | 0xffffffff);

	irq_value = hisi_sas_read32(hisi_hba, SAS_ECC_INTR);
	if (irq_value)
		multi_bit_ecc_error_process_v3_hw(hisi_hba, irq_value);

	hisi_sas_write32(hisi_hba, SAS_ECC_INTR, irq_value);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, irq_msk);
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
	{}
};

static const struct hisi_sas_hw_error fifo_error[] = {
	{ .msk = BIT(8),  .msg = "CQE_WINFO_FIFO" },
	{ .msk = BIT(9),  .msg = "CQE_MSG_FIFIO" },
	{ .msk = BIT(10), .msg = "GETDQE_FIFO" },
	{ .msk = BIT(11), .msg = "CMDP_FIFO" },
	{ .msk = BIT(12), .msg = "AWTCTRL_FIFO" },
	{}
};

static const struct hisi_sas_hw_error fatal_axi_error[] = {
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
	{
		.irq_msk = BIT(ENT_INT_SRC3_DQE_POISON_OFF),
		.msg = "read dqe poison",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_IOST_POISON_OFF),
		.msg = "read iost poison",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_ITCT_POISON_OFF),
		.msg = "read itct poison",
	},
	{
		.irq_msk = BIT(ENT_INT_SRC3_ITCT_NCQ_POISON_OFF),
		.msg = "read itct ncq poison",
	},

};

static irqreturn_t fatal_axi_int_v3_hw(int irq_no, void *p)
{
	u32 irq_value, irq_msk;
	struct hisi_hba *hisi_hba = p;
	struct device *dev = hisi_hba->dev;
	struct pci_dev *pdev = hisi_hba->pci_dev;
	int i;

	irq_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK3);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, irq_msk | 0x1df00);

	irq_value = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
	irq_value &= ~irq_msk;

	for (i = 0; i < ARRAY_SIZE(fatal_axi_error); i++) {
		const struct hisi_sas_hw_error *error = &fatal_axi_error[i];

		if (!(irq_value & error->irq_msk))
			continue;

		if (error->sub) {
			const struct hisi_sas_hw_error *sub = error->sub;
			u32 err_value = hisi_sas_read32(hisi_hba, error->reg);

			for (; sub->msk || sub->msg; sub++) {
				if (!(err_value & sub->msk))
					continue;

				dev_err(dev, "%s error (0x%x) found!\n",
					sub->msg, irq_value);
				queue_work(hisi_hba->wq, &hisi_hba->rst_work);
			}
		} else {
			dev_err(dev, "%s error (0x%x) found!\n",
				error->msg, irq_value);
			queue_work(hisi_hba->wq, &hisi_hba->rst_work);
		}

		if (pdev->revision < 0x21) {
			u32 reg_val;

			reg_val = hisi_sas_read32(hisi_hba,
						  AXI_MASTER_CFG_BASE +
						  AM_CTRL_GLOBAL);
			reg_val |= AM_CTRL_SHUTDOWN_REQ_MSK;
			hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE +
					 AM_CTRL_GLOBAL, reg_val);
		}
	}

	fatal_ecc_int_v3_hw(hisi_hba);

	if (irq_value & BIT(ENT_INT_SRC3_ITC_INT_OFF)) {
		u32 reg_val = hisi_sas_read32(hisi_hba, ITCT_CLR);
		u32 dev_id = reg_val & ITCT_DEV_MSK;
		struct hisi_sas_device *sas_dev =
				&hisi_hba->devices[dev_id];

		hisi_sas_write32(hisi_hba, ITCT_CLR, 0);
		dev_dbg(dev, "clear ITCT ok\n");
		complete(sas_dev->completion);
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, irq_value & 0x1df00);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, irq_msk);

	return IRQ_HANDLED;
}

static void
slot_err_v3_hw(struct hisi_hba *hisi_hba, struct sas_task *task,
	       struct hisi_sas_slot *slot)
{
	struct task_status_struct *ts = &task->task_status;
	struct hisi_sas_complete_v3_hdr *complete_queue =
			hisi_hba->complete_hdr[slot->cmplt_queue];
	struct hisi_sas_complete_v3_hdr *complete_hdr =
			&complete_queue[slot->cmplt_queue_slot];
	struct hisi_sas_err_record_v3 *record =
			hisi_sas_status_buf_addr_mem(slot);
	u32 dma_rx_err_type = le32_to_cpu(record->dma_rx_err_type);
	u32 trans_tx_fail_type = le32_to_cpu(record->trans_tx_fail_type);
	u32 dw3 = le32_to_cpu(complete_hdr->dw3);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		if (dma_rx_err_type & RX_DATA_LEN_UNDERFLOW_MSK) {
			ts->residual = trans_tx_fail_type;
			ts->stat = SAS_DATA_UNDERRUN;
		} else if (dw3 & CMPLT_HDR_IO_IN_TARGET_MSK) {
			ts->stat = SAS_QUEUE_FULL;
			slot->abort = 1;
		} else {
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		}
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		if (dma_rx_err_type & RX_DATA_LEN_UNDERFLOW_MSK) {
			ts->residual = trans_tx_fail_type;
			ts->stat = SAS_DATA_UNDERRUN;
		} else if (dw3 & CMPLT_HDR_IO_IN_TARGET_MSK) {
			ts->stat = SAS_PHY_DOWN;
			slot->abort = 1;
		} else {
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_RSVD_RETRY;
		}
		hisi_sas_sata_done(task, slot);
		break;
	case SAS_PROTOCOL_SMP:
		ts->stat = SAM_STAT_CHECK_CONDITION;
		break;
	default:
		break;
	}
}

static int
slot_complete_v3_hw(struct hisi_hba *hisi_hba, struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_device *sas_dev;
	struct device *dev = hisi_hba->dev;
	struct task_status_struct *ts;
	struct domain_device *device;
	struct sas_ha_struct *ha;
	enum exec_status sts;
	struct hisi_sas_complete_v3_hdr *complete_queue =
			hisi_hba->complete_hdr[slot->cmplt_queue];
	struct hisi_sas_complete_v3_hdr *complete_hdr =
			&complete_queue[slot->cmplt_queue_slot];
	unsigned long flags;
	bool is_internal = slot->is_internal;
	u32 dw0, dw1, dw3;

	if (unlikely(!task || !task->lldd_task || !task->dev))
		return -EINVAL;

	ts = &task->task_status;
	device = task->dev;
	ha = device->port->ha;
	sas_dev = device->lldd_dev;

	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags &=
		~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	memset(ts, 0, sizeof(*ts));
	ts->resp = SAS_TASK_COMPLETE;

	if (unlikely(!sas_dev)) {
		dev_dbg(dev, "slot complete: port has not device\n");
		ts->stat = SAS_PHY_DOWN;
		goto out;
	}

	dw0 = le32_to_cpu(complete_hdr->dw0);
	dw1 = le32_to_cpu(complete_hdr->dw1);
	dw3 = le32_to_cpu(complete_hdr->dw3);

	/*
	 * Use SAS+TMF status codes
	 */
	switch ((dw0 & CMPLT_HDR_ABORT_STAT_MSK) >> CMPLT_HDR_ABORT_STAT_OFF) {
	case STAT_IO_ABORTED:
		/* this IO has been aborted by abort command */
		ts->stat = SAS_ABORTED_TASK;
		goto out;
	case STAT_IO_COMPLETE:
		/* internal abort command complete */
		ts->stat = TMF_RESP_FUNC_SUCC;
		goto out;
	case STAT_IO_NO_DEVICE:
		ts->stat = TMF_RESP_FUNC_COMPLETE;
		goto out;
	case STAT_IO_NOT_VALID:
		/*
		 * abort single IO, the controller can't find the IO
		 */
		ts->stat = TMF_RESP_FUNC_FAILED;
		goto out;
	default:
		break;
	}

	/* check for erroneous completion */
	if ((dw0 & CMPLT_HDR_CMPLT_MSK) == 0x3) {
		u32 *error_info = hisi_sas_status_buf_addr_mem(slot);

		slot_err_v3_hw(hisi_hba, task, slot);
		if (ts->stat != SAS_DATA_UNDERRUN)
			dev_info(dev, "erroneous completion iptt=%d task=%p dev id=%d CQ hdr: 0x%x 0x%x 0x%x 0x%x Error info: 0x%x 0x%x 0x%x 0x%x\n",
				 slot->idx, task, sas_dev->device_id,
				 dw0, dw1, complete_hdr->act, dw3,
				 error_info[0], error_info[1],
				 error_info[2], error_info[3]);
		if (unlikely(slot->abort))
			return ts->stat;
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP: {
		struct ssp_response_iu *iu =
			hisi_sas_status_buf_addr_mem(slot) +
			sizeof(struct hisi_sas_err_record);

		sas_ssp_task_response(dev, task, iu);
		break;
	}
	case SAS_PROTOCOL_SMP: {
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
		ts->stat = SAM_STAT_GOOD;
		hisi_sas_sata_done(task, slot);
		break;
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
	sts = ts->stat;
	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_ABORTED) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		dev_info(dev, "slot complete: task(%p) aborted\n", task);
		return SAS_ABORTED_TASK;
	}
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	hisi_sas_slot_task_free(hisi_hba, task, slot);

	if (!is_internal && (task->task_proto != SAS_PROTOCOL_SMP)) {
		spin_lock_irqsave(&device->done_lock, flags);
		if (test_bit(SAS_HA_FROZEN, &ha->state)) {
			spin_unlock_irqrestore(&device->done_lock, flags);
			dev_info(dev, "slot complete: task(%p) ignored\n ",
				 task);
			return sts;
		}
		spin_unlock_irqrestore(&device->done_lock, flags);
	}

	if (task->task_done)
		task->task_done(task);

	return sts;
}

static void cq_tasklet_v3_hw(unsigned long val)
{
	struct hisi_sas_cq *cq = (struct hisi_sas_cq *)val;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	struct hisi_sas_slot *slot;
	struct hisi_sas_complete_v3_hdr *complete_queue;
	u32 rd_point = cq->rd_point, wr_point;
	int queue = cq->id;

	complete_queue = hisi_hba->complete_hdr[queue];

	wr_point = hisi_sas_read32(hisi_hba, COMPL_Q_0_WR_PTR +
				   (0x14 * queue));

	while (rd_point != wr_point) {
		struct hisi_sas_complete_v3_hdr *complete_hdr;
		struct device *dev = hisi_hba->dev;
		u32 dw1;
		int iptt;

		complete_hdr = &complete_queue[rd_point];
		dw1 = le32_to_cpu(complete_hdr->dw1);

		iptt = dw1 & CMPLT_HDR_IPTT_MSK;
		if (likely(iptt < HISI_SAS_COMMAND_ENTRIES_V3_HW)) {
			slot = &hisi_hba->slot_info[iptt];
			slot->cmplt_queue_slot = rd_point;
			slot->cmplt_queue = queue;
			slot_complete_v3_hw(hisi_hba, slot);
		} else
			dev_err(dev, "IPTT %d is invalid, discard it.\n", iptt);

		if (++rd_point >= HISI_SAS_QUEUE_SLOTS)
			rd_point = 0;
	}

	/* update rd_point */
	cq->rd_point = rd_point;
	hisi_sas_write32(hisi_hba, COMPL_Q_0_RD_PTR + (0x14 * queue), rd_point);
}

static irqreturn_t cq_interrupt_v3_hw(int irq_no, void *p)
{
	struct hisi_sas_cq *cq = p;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	int queue = cq->id;

	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 1 << queue);

	tasklet_schedule(&cq->tasklet);

	return IRQ_HANDLED;
}

static void setup_reply_map_v3_hw(struct hisi_hba *hisi_hba, int nvecs)
{
	const struct cpumask *mask;
	int queue, cpu;

	for (queue = 0; queue < nvecs; queue++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[queue];

		mask = pci_irq_get_affinity(hisi_hba->pci_dev, queue +
					    BASE_VECTORS_V3_HW);
		if (!mask)
			goto fallback;
		cq->pci_irq_mask = mask;
		for_each_cpu(cpu, mask)
			hisi_hba->reply_map[cpu] = queue;
	}
	return;

fallback:
	for_each_possible_cpu(cpu)
		hisi_hba->reply_map[cpu] = cpu % hisi_hba->queue_count;
	/* Don't clean all CQ masks */
}

static int interrupt_init_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	struct pci_dev *pdev = hisi_hba->pci_dev;
	int vectors, rc;
	int i, k;
	int max_msi = HISI_SAS_MSI_COUNT_V3_HW, min_msi;

	if (auto_affine_msi_experimental) {
		struct irq_affinity desc = {
			.pre_vectors = BASE_VECTORS_V3_HW,
		};

		min_msi = MIN_AFFINE_VECTORS_V3_HW;

		hisi_hba->reply_map = devm_kcalloc(dev, nr_cpu_ids,
						   sizeof(unsigned int),
						   GFP_KERNEL);
		if (!hisi_hba->reply_map)
			return -ENOMEM;
		vectors = pci_alloc_irq_vectors_affinity(hisi_hba->pci_dev,
							 min_msi, max_msi,
							 PCI_IRQ_MSI |
							 PCI_IRQ_AFFINITY,
							 &desc);
		if (vectors < 0)
			return -ENOENT;
		setup_reply_map_v3_hw(hisi_hba, vectors - BASE_VECTORS_V3_HW);
	} else {
		min_msi = max_msi;
		vectors = pci_alloc_irq_vectors(hisi_hba->pci_dev, min_msi,
						max_msi, PCI_IRQ_MSI);
		if (vectors < 0)
			return vectors;
	}

	hisi_hba->cq_nvecs = vectors - BASE_VECTORS_V3_HW;

	rc = devm_request_irq(dev, pci_irq_vector(pdev, 1),
			      int_phy_up_down_bcast_v3_hw, 0,
			      DRV_NAME " phy", hisi_hba);
	if (rc) {
		dev_err(dev, "could not request phy interrupt, rc=%d\n", rc);
		rc = -ENOENT;
		goto free_irq_vectors;
	}

	rc = devm_request_irq(dev, pci_irq_vector(pdev, 2),
			      int_chnl_int_v3_hw, 0,
			      DRV_NAME " channel", hisi_hba);
	if (rc) {
		dev_err(dev, "could not request chnl interrupt, rc=%d\n", rc);
		rc = -ENOENT;
		goto free_phy_irq;
	}

	rc = devm_request_irq(dev, pci_irq_vector(pdev, 11),
			      fatal_axi_int_v3_hw, 0,
			      DRV_NAME " fatal", hisi_hba);
	if (rc) {
		dev_err(dev, "could not request fatal interrupt, rc=%d\n", rc);
		rc = -ENOENT;
		goto free_chnl_interrupt;
	}

	/* Init tasklets for cq only */
	for (i = 0; i < hisi_hba->cq_nvecs; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct tasklet_struct *t = &cq->tasklet;
		int nr = hisi_sas_intr_conv ? 16 : 16 + i;
		unsigned long irqflags = hisi_sas_intr_conv ? IRQF_SHARED : 0;

		rc = devm_request_irq(dev, pci_irq_vector(pdev, nr),
				      cq_interrupt_v3_hw, irqflags,
				      DRV_NAME " cq", cq);
		if (rc) {
			dev_err(dev, "could not request cq%d interrupt, rc=%d\n",
				i, rc);
			rc = -ENOENT;
			goto free_cq_irqs;
		}

		tasklet_init(t, cq_tasklet_v3_hw, (unsigned long)cq);
	}

	return 0;

free_cq_irqs:
	for (k = 0; k < i; k++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[k];
		int nr = hisi_sas_intr_conv ? 16 : 16 + k;

		free_irq(pci_irq_vector(pdev, nr), cq);
	}
	free_irq(pci_irq_vector(pdev, 11), hisi_hba);
free_chnl_interrupt:
	free_irq(pci_irq_vector(pdev, 2), hisi_hba);
free_phy_irq:
	free_irq(pci_irq_vector(pdev, 1), hisi_hba);
free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return rc;
}

static int hisi_sas_v3_init(struct hisi_hba *hisi_hba)
{
	int rc;

	rc = hw_init_v3_hw(hisi_hba);
	if (rc)
		return rc;

	rc = interrupt_init_v3_hw(hisi_hba);
	if (rc)
		return rc;

	return 0;
}

static void phy_set_linkrate_v3_hw(struct hisi_hba *hisi_hba, int phy_no,
		struct sas_phy_linkrates *r)
{
	enum sas_linkrate max = r->maximum_linkrate;
	u32 prog_phy_link_rate = 0x800;

	prog_phy_link_rate |= hisi_sas_get_prog_phy_linkrate_mask(max);
	hisi_sas_phy_write32(hisi_hba, phy_no, PROG_PHY_LINK_RATE,
			     prog_phy_link_rate);
}

static void interrupt_disable_v3_hw(struct hisi_hba *hisi_hba)
{
	struct pci_dev *pdev = hisi_hba->pci_dev;
	int i;

	synchronize_irq(pci_irq_vector(pdev, 1));
	synchronize_irq(pci_irq_vector(pdev, 2));
	synchronize_irq(pci_irq_vector(pdev, 11));
	for (i = 0; i < hisi_hba->queue_count; i++) {
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK + 0x4 * i, 0x1);
		synchronize_irq(pci_irq_vector(pdev, i + 16));
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0xffffffff);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xffffffff);

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x1);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x1);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x1);
	}
}

static u32 get_phys_state_v3_hw(struct hisi_hba *hisi_hba)
{
	return hisi_sas_read32(hisi_hba, PHY_STATE);
}

static int disable_host_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	u32 status, reg_val;
	int rc;

	interrupt_disable_v3_hw(hisi_hba);
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0x0);
	hisi_sas_kill_tasklets(hisi_hba);

	hisi_sas_stop_phys(hisi_hba);

	mdelay(10);

	reg_val = hisi_sas_read32(hisi_hba, AXI_MASTER_CFG_BASE +
				  AM_CTRL_GLOBAL);
	reg_val |= AM_CTRL_SHUTDOWN_REQ_MSK;
	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE +
			 AM_CTRL_GLOBAL, reg_val);

	/* wait until bus idle */
	rc = hisi_sas_read32_poll_timeout(AXI_MASTER_CFG_BASE +
					  AM_CURR_TRANS_RETURN, status,
					  status == 0x3, 10, 100);
	if (rc) {
		dev_err(dev, "axi bus is not idle, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int soft_reset_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int rc;

	rc = disable_host_v3_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "soft reset: disable host failed rc=%d\n", rc);
		return rc;
	}

	hisi_sas_init_mem(hisi_hba);

	return hw_init_v3_hw(hisi_hba);
}

static int write_gpio_v3_hw(struct hisi_hba *hisi_hba, u8 reg_type,
			u8 reg_index, u8 reg_count, u8 *write_data)
{
	struct device *dev = hisi_hba->dev;
	u32 *data = (u32 *)write_data;
	int i;

	switch (reg_type) {
	case SAS_GPIO_REG_TX:
		if ((reg_index + reg_count) > ((hisi_hba->n_phy + 3) / 4)) {
			dev_err(dev, "write gpio: invalid reg range[%d, %d]\n",
				reg_index, reg_index + reg_count - 1);
			return -EINVAL;
		}

		for (i = 0; i < reg_count; i++)
			hisi_sas_write32(hisi_hba,
					 SAS_GPIO_TX_0_1 + (reg_index + i) * 4,
					 data[i]);
		break;
	default:
		dev_err(dev, "write gpio: unsupported or bad reg type %d\n",
			reg_type);
		return -EINVAL;
	}

	return 0;
}

static int wait_cmds_complete_timeout_v3_hw(struct hisi_hba *hisi_hba,
					    int delay_ms, int timeout_ms)
{
	struct device *dev = hisi_hba->dev;
	int entries, entries_old = 0, time;

	for (time = 0; time < timeout_ms; time += delay_ms) {
		entries = hisi_sas_read32(hisi_hba, CQE_SEND_CNT);
		if (entries == entries_old)
			break;

		entries_old = entries;
		msleep(delay_ms);
	}

	if (time >= timeout_ms)
		return -ETIMEDOUT;

	dev_dbg(dev, "wait commands complete %dms\n", time);

	return 0;
}

static ssize_t intr_conv_v3_hw_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", hisi_sas_intr_conv);
}
static DEVICE_ATTR_RO(intr_conv_v3_hw);

static void config_intr_coal_v3_hw(struct hisi_hba *hisi_hba)
{
	/* config those registers between enable and disable PHYs */
	hisi_sas_stop_phys(hisi_hba);

	if (hisi_hba->intr_coal_ticks == 0 ||
	    hisi_hba->intr_coal_count == 0) {
		hisi_sas_write32(hisi_hba, INT_COAL_EN, 0x1);
		hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME, 0x1);
		hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT, 0x1);
	} else {
		hisi_sas_write32(hisi_hba, INT_COAL_EN, 0x3);
		hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME,
				 hisi_hba->intr_coal_ticks);
		hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT,
				 hisi_hba->intr_coal_count);
	}
	phys_init_v3_hw(hisi_hba);
}

static ssize_t intr_coal_ticks_v3_hw_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct hisi_hba *hisi_hba = shost_priv(shost);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 hisi_hba->intr_coal_ticks);
}

static ssize_t intr_coal_ticks_v3_hw_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct hisi_hba *hisi_hba = shost_priv(shost);
	u32 intr_coal_ticks;
	int ret;

	ret = kstrtou32(buf, 10, &intr_coal_ticks);
	if (ret) {
		dev_err(dev, "Input data of interrupt coalesce unmatch\n");
		return -EINVAL;
	}

	if (intr_coal_ticks >= BIT(24)) {
		dev_err(dev, "intr_coal_ticks must be less than 2^24!\n");
		return -EINVAL;
	}

	hisi_hba->intr_coal_ticks = intr_coal_ticks;

	config_intr_coal_v3_hw(hisi_hba);

	return count;
}
static DEVICE_ATTR_RW(intr_coal_ticks_v3_hw);

static ssize_t intr_coal_count_v3_hw_show(struct device *dev,
					  struct device_attribute
					  *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct hisi_hba *hisi_hba = shost_priv(shost);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 hisi_hba->intr_coal_count);
}

static ssize_t intr_coal_count_v3_hw_store(struct device *dev,
		struct device_attribute
		*attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct hisi_hba *hisi_hba = shost_priv(shost);
	u32 intr_coal_count;
	int ret;

	ret = kstrtou32(buf, 10, &intr_coal_count);
	if (ret) {
		dev_err(dev, "Input data of interrupt coalesce unmatch\n");
		return -EINVAL;
	}

	if (intr_coal_count >= BIT(8)) {
		dev_err(dev, "intr_coal_count must be less than 2^8!\n");
		return -EINVAL;
	}

	hisi_hba->intr_coal_count = intr_coal_count;

	config_intr_coal_v3_hw(hisi_hba);

	return count;
}
static DEVICE_ATTR_RW(intr_coal_count_v3_hw);

static struct device_attribute *host_attrs_v3_hw[] = {
	&dev_attr_phy_event_threshold,
	&dev_attr_intr_conv_v3_hw,
	&dev_attr_intr_coal_ticks_v3_hw,
	&dev_attr_intr_coal_count_v3_hw,
	NULL
};

static const struct hisi_sas_debugfs_reg_lu debugfs_port_reg_lu[] = {
	HISI_SAS_DEBUGFS_REG(PHY_CFG),
	HISI_SAS_DEBUGFS_REG(HARD_PHY_LINKRATE),
	HISI_SAS_DEBUGFS_REG(PROG_PHY_LINK_RATE),
	HISI_SAS_DEBUGFS_REG(PHY_CTRL),
	HISI_SAS_DEBUGFS_REG(SL_CFG),
	HISI_SAS_DEBUGFS_REG(AIP_LIMIT),
	HISI_SAS_DEBUGFS_REG(SL_CONTROL),
	HISI_SAS_DEBUGFS_REG(RX_PRIMS_STATUS),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD0),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD1),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD2),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD3),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD4),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD5),
	HISI_SAS_DEBUGFS_REG(TX_ID_DWORD6),
	HISI_SAS_DEBUGFS_REG(TXID_AUTO),
	HISI_SAS_DEBUGFS_REG(RX_IDAF_DWORD0),
	HISI_SAS_DEBUGFS_REG(RXOP_CHECK_CFG_H),
	HISI_SAS_DEBUGFS_REG(STP_LINK_TIMER),
	HISI_SAS_DEBUGFS_REG(STP_LINK_TIMEOUT_STATE),
	HISI_SAS_DEBUGFS_REG(CON_CFG_DRIVER),
	HISI_SAS_DEBUGFS_REG(SAS_SSP_CON_TIMER_CFG),
	HISI_SAS_DEBUGFS_REG(SAS_SMP_CON_TIMER_CFG),
	HISI_SAS_DEBUGFS_REG(SAS_STP_CON_TIMER_CFG),
	HISI_SAS_DEBUGFS_REG(CHL_INT0),
	HISI_SAS_DEBUGFS_REG(CHL_INT1),
	HISI_SAS_DEBUGFS_REG(CHL_INT2),
	HISI_SAS_DEBUGFS_REG(CHL_INT0_MSK),
	HISI_SAS_DEBUGFS_REG(CHL_INT1_MSK),
	HISI_SAS_DEBUGFS_REG(CHL_INT2_MSK),
	HISI_SAS_DEBUGFS_REG(SAS_EC_INT_COAL_TIME),
	HISI_SAS_DEBUGFS_REG(CHL_INT_COAL_EN),
	HISI_SAS_DEBUGFS_REG(SAS_RX_TRAIN_TIMER),
	HISI_SAS_DEBUGFS_REG(PHY_CTRL_RDY_MSK),
	HISI_SAS_DEBUGFS_REG(PHYCTRL_NOT_RDY_MSK),
	HISI_SAS_DEBUGFS_REG(PHYCTRL_DWS_RESET_MSK),
	HISI_SAS_DEBUGFS_REG(PHYCTRL_PHY_ENA_MSK),
	HISI_SAS_DEBUGFS_REG(SL_RX_BCAST_CHK_MSK),
	HISI_SAS_DEBUGFS_REG(PHYCTRL_OOB_RESTART_MSK),
	HISI_SAS_DEBUGFS_REG(DMA_TX_STATUS),
	HISI_SAS_DEBUGFS_REG(DMA_RX_STATUS),
	HISI_SAS_DEBUGFS_REG(COARSETUNE_TIME),
	HISI_SAS_DEBUGFS_REG(ERR_CNT_DWS_LOST),
	HISI_SAS_DEBUGFS_REG(ERR_CNT_RESET_PROB),
	HISI_SAS_DEBUGFS_REG(ERR_CNT_INVLD_DW),
	HISI_SAS_DEBUGFS_REG(ERR_CNT_CODE_ERR),
	HISI_SAS_DEBUGFS_REG(ERR_CNT_DISP_ERR),
	{}
};

static const struct hisi_sas_debugfs_reg debugfs_port_reg = {
	.lu = debugfs_port_reg_lu,
	.count = 0x100,
	.base_off = PORT_BASE,
	.read_port_reg = hisi_sas_phy_read32,
};

static const struct hisi_sas_debugfs_reg_lu debugfs_global_reg_lu[] = {
	HISI_SAS_DEBUGFS_REG(DLVRY_QUEUE_ENABLE),
	HISI_SAS_DEBUGFS_REG(PHY_CONTEXT),
	HISI_SAS_DEBUGFS_REG(PHY_STATE),
	HISI_SAS_DEBUGFS_REG(PHY_PORT_NUM_MA),
	HISI_SAS_DEBUGFS_REG(PHY_CONN_RATE),
	HISI_SAS_DEBUGFS_REG(ITCT_CLR),
	HISI_SAS_DEBUGFS_REG(IO_SATA_BROKEN_MSG_ADDR_LO),
	HISI_SAS_DEBUGFS_REG(IO_SATA_BROKEN_MSG_ADDR_HI),
	HISI_SAS_DEBUGFS_REG(SATA_INITI_D2H_STORE_ADDR_LO),
	HISI_SAS_DEBUGFS_REG(SATA_INITI_D2H_STORE_ADDR_HI),
	HISI_SAS_DEBUGFS_REG(CFG_MAX_TAG),
	HISI_SAS_DEBUGFS_REG(HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL),
	HISI_SAS_DEBUGFS_REG(HGC_SAS_TXFAIL_RETRY_CTRL),
	HISI_SAS_DEBUGFS_REG(HGC_GET_ITV_TIME),
	HISI_SAS_DEBUGFS_REG(DEVICE_MSG_WORK_MODE),
	HISI_SAS_DEBUGFS_REG(OPENA_WT_CONTI_TIME),
	HISI_SAS_DEBUGFS_REG(I_T_NEXUS_LOSS_TIME),
	HISI_SAS_DEBUGFS_REG(MAX_CON_TIME_LIMIT_TIME),
	HISI_SAS_DEBUGFS_REG(BUS_INACTIVE_LIMIT_TIME),
	HISI_SAS_DEBUGFS_REG(REJECT_TO_OPEN_LIMIT_TIME),
	HISI_SAS_DEBUGFS_REG(CQ_INT_CONVERGE_EN),
	HISI_SAS_DEBUGFS_REG(CFG_AGING_TIME),
	HISI_SAS_DEBUGFS_REG(HGC_DFX_CFG2),
	HISI_SAS_DEBUGFS_REG(CFG_ABT_SET_QUERY_IPTT),
	HISI_SAS_DEBUGFS_REG(CFG_ABT_SET_IPTT_DONE),
	HISI_SAS_DEBUGFS_REG(HGC_IOMB_PROC1_STATUS),
	HISI_SAS_DEBUGFS_REG(CHNL_INT_STATUS),
	HISI_SAS_DEBUGFS_REG(HGC_AXI_FIFO_ERR_INFO),
	HISI_SAS_DEBUGFS_REG(INT_COAL_EN),
	HISI_SAS_DEBUGFS_REG(OQ_INT_COAL_TIME),
	HISI_SAS_DEBUGFS_REG(OQ_INT_COAL_CNT),
	HISI_SAS_DEBUGFS_REG(ENT_INT_COAL_TIME),
	HISI_SAS_DEBUGFS_REG(ENT_INT_COAL_CNT),
	HISI_SAS_DEBUGFS_REG(OQ_INT_SRC),
	HISI_SAS_DEBUGFS_REG(OQ_INT_SRC_MSK),
	HISI_SAS_DEBUGFS_REG(ENT_INT_SRC1),
	HISI_SAS_DEBUGFS_REG(ENT_INT_SRC2),
	HISI_SAS_DEBUGFS_REG(ENT_INT_SRC3),
	HISI_SAS_DEBUGFS_REG(ENT_INT_SRC_MSK1),
	HISI_SAS_DEBUGFS_REG(ENT_INT_SRC_MSK2),
	HISI_SAS_DEBUGFS_REG(ENT_INT_SRC_MSK3),
	HISI_SAS_DEBUGFS_REG(CHNL_PHYUPDOWN_INT_MSK),
	HISI_SAS_DEBUGFS_REG(CHNL_ENT_INT_MSK),
	HISI_SAS_DEBUGFS_REG(HGC_COM_INT_MSK),
	HISI_SAS_DEBUGFS_REG(SAS_ECC_INTR),
	HISI_SAS_DEBUGFS_REG(SAS_ECC_INTR_MSK),
	HISI_SAS_DEBUGFS_REG(HGC_ERR_STAT_EN),
	HISI_SAS_DEBUGFS_REG(CQE_SEND_CNT),
	HISI_SAS_DEBUGFS_REG(DLVRY_Q_0_DEPTH),
	HISI_SAS_DEBUGFS_REG(DLVRY_Q_0_WR_PTR),
	HISI_SAS_DEBUGFS_REG(DLVRY_Q_0_RD_PTR),
	HISI_SAS_DEBUGFS_REG(HYPER_STREAM_ID_EN_CFG),
	HISI_SAS_DEBUGFS_REG(OQ0_INT_SRC_MSK),
	HISI_SAS_DEBUGFS_REG(COMPL_Q_0_DEPTH),
	HISI_SAS_DEBUGFS_REG(COMPL_Q_0_WR_PTR),
	HISI_SAS_DEBUGFS_REG(COMPL_Q_0_RD_PTR),
	HISI_SAS_DEBUGFS_REG(AWQOS_AWCACHE_CFG),
	HISI_SAS_DEBUGFS_REG(ARQOS_ARCACHE_CFG),
	HISI_SAS_DEBUGFS_REG(HILINK_ERR_DFX),
	HISI_SAS_DEBUGFS_REG(SAS_GPIO_CFG_0),
	HISI_SAS_DEBUGFS_REG(SAS_GPIO_CFG_1),
	HISI_SAS_DEBUGFS_REG(SAS_GPIO_TX_0_1),
	HISI_SAS_DEBUGFS_REG(SAS_CFG_DRIVE_VLD),
	{}
};

static const struct hisi_sas_debugfs_reg debugfs_global_reg = {
	.lu = debugfs_global_reg_lu,
	.count = 0x800,
	.read_global_reg = hisi_sas_read32,
};

static void debugfs_snapshot_prepare_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;

	set_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);

	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0);

	if (wait_cmds_complete_timeout_v3_hw(hisi_hba, 100, 5000) == -ETIMEDOUT)
		dev_dbg(dev, "Wait commands complete timeout!\n");

	hisi_sas_kill_tasklets(hisi_hba);
}

static void debugfs_snapshot_restore_v3_hw(struct hisi_hba *hisi_hba)
{
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE,
			 (u32)((1ULL << hisi_hba->queue_count) - 1));

	clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
}

static struct scsi_host_template sht_v3_hw = {
	.name			= DRV_NAME,
	.module			= THIS_MODULE,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= hisi_sas_slave_configure,
	.scan_finished		= hisi_sas_scan_finished,
	.scan_start		= hisi_sas_scan_start,
	.change_queue_depth	= sas_change_queue_depth,
	.bios_param		= sas_bios_param,
	.this_id		= -1,
	.sg_tablesize		= HISI_SAS_SGE_PAGE_CNT,
	.sg_prot_tablesize	= HISI_SAS_SGE_PAGE_CNT,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_target_reset_handler = sas_eh_target_reset_handler,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
	.shost_attrs		= host_attrs_v3_hw,
	.tag_alloc_policy	= BLK_TAG_ALLOC_RR,
	.host_reset             = hisi_sas_host_reset,
};

static const struct hisi_sas_hw hisi_sas_v3_hw = {
	.hw_init = hisi_sas_v3_init,
	.setup_itct = setup_itct_v3_hw,
	.max_command_entries = HISI_SAS_COMMAND_ENTRIES_V3_HW,
	.get_wideport_bitmap = get_wideport_bitmap_v3_hw,
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v3_hdr),
	.clear_itct = clear_itct_v3_hw,
	.sl_notify_ssp = sl_notify_ssp_v3_hw,
	.prep_ssp = prep_ssp_v3_hw,
	.prep_smp = prep_smp_v3_hw,
	.prep_stp = prep_ata_v3_hw,
	.prep_abort = prep_abort_v3_hw,
	.get_free_slot = get_free_slot_v3_hw,
	.start_delivery = start_delivery_v3_hw,
	.slot_complete = slot_complete_v3_hw,
	.phys_init = phys_init_v3_hw,
	.phy_start = start_phy_v3_hw,
	.phy_disable = disable_phy_v3_hw,
	.phy_hard_reset = phy_hard_reset_v3_hw,
	.phy_get_max_linkrate = phy_get_max_linkrate_v3_hw,
	.phy_set_linkrate = phy_set_linkrate_v3_hw,
	.dereg_device = dereg_device_v3_hw,
	.soft_reset = soft_reset_v3_hw,
	.get_phys_state = get_phys_state_v3_hw,
	.get_events = phy_get_events_v3_hw,
	.write_gpio = write_gpio_v3_hw,
	.wait_cmds_complete_timeout = wait_cmds_complete_timeout_v3_hw,
	.debugfs_reg_global = &debugfs_global_reg,
	.debugfs_reg_port = &debugfs_port_reg,
	.snapshot_prepare = debugfs_snapshot_prepare_v3_hw,
	.snapshot_restore = debugfs_snapshot_restore_v3_hw,
};

static struct Scsi_Host *
hisi_sas_shost_alloc_pci(struct pci_dev *pdev)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;

	shost = scsi_host_alloc(&sht_v3_hw, sizeof(*hisi_hba));
	if (!shost) {
		dev_err(dev, "shost alloc failed\n");
		return NULL;
	}
	hisi_hba = shost_priv(shost);

	INIT_WORK(&hisi_hba->rst_work, hisi_sas_rst_work_handler);
	INIT_WORK(&hisi_hba->debugfs_work, hisi_sas_debugfs_work_handler);
	hisi_hba->hw = &hisi_sas_v3_hw;
	hisi_hba->pci_dev = pdev;
	hisi_hba->dev = dev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	if (prot_mask & ~HISI_SAS_PROT_MASK)
		dev_err(dev, "unsupported protection mask 0x%x, using default (0x0)\n",
			prot_mask);
	else
		hisi_hba->prot_mask = prot_mask;

	timer_setup(&hisi_hba->timer, NULL, 0);

	if (hisi_sas_get_fw_info(hisi_hba) < 0)
		goto err_out;

	if (hisi_sas_alloc(hisi_hba)) {
		hisi_sas_free(hisi_hba);
		goto err_out;
	}

	return shost;
err_out:
	scsi_host_put(shost);
	dev_err(dev, "shost alloc failed\n");
	return NULL;
}

static int
hisi_sas_v3_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha;
	int rc, phy_nr, port_nr, i;

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out;

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_disable_device;

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "No usable DMA addressing method\n");
		rc = -ENODEV;
		goto err_out_regions;
	}

	shost = hisi_sas_shost_alloc_pci(pdev);
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	sha = SHOST_TO_SAS_HA(shost);
	hisi_hba = shost_priv(shost);
	dev_set_drvdata(dev, sha);

	hisi_hba->regs = pcim_iomap(pdev, 5, 0);
	if (!hisi_hba->regs) {
		dev_err(dev, "cannot map register\n");
		rc = -ENOMEM;
		goto err_out_ha;
	}

	phy_nr = port_nr = hisi_hba->n_phy;

	arr_phy = devm_kcalloc(dev, phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = devm_kcalloc(dev, port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port) {
		rc = -ENOMEM;
		goto err_out_ha;
	}

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->core.shost = shost;
	sha->lldd_ha = hisi_hba;

	shost->transportt = hisi_sas_stt;
	shost->max_id = HISI_SAS_MAX_DEVICES;
	shost->max_lun = ~0;
	shost->max_channel = 1;
	shost->max_cmd_len = 16;
	shost->can_queue = hisi_hba->hw->max_command_entries -
		HISI_SAS_RESERVED_IPTT_CNT;
	shost->cmd_per_lun = hisi_hba->hw->max_command_entries -
		HISI_SAS_RESERVED_IPTT_CNT;

	sha->sas_ha_name = DRV_NAME;
	sha->dev = dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &hisi_hba->sas_addr[0];
	sha->num_phys = hisi_hba->n_phy;
	sha->core.shost = hisi_hba->shost;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		sha->sas_phy[i] = &hisi_hba->phy[i].sas_phy;
		sha->sas_port[i] = &hisi_hba->port[i].sas_port;
	}

	if (hisi_hba->prot_mask) {
		dev_info(dev, "Registering for DIF/DIX prot_mask=0x%x\n",
			 prot_mask);
		scsi_host_set_prot(hisi_hba->shost, prot_mask);
		if (hisi_hba->prot_mask & HISI_SAS_DIX_PROT_MASK)
			scsi_host_set_guard(hisi_hba->shost,
					    SHOST_DIX_GUARD_CRC);
	}

	if (hisi_sas_debugfs_enable)
		hisi_sas_debugfs_init(hisi_hba);

	rc = scsi_add_host(shost, dev);
	if (rc)
		goto err_out_ha;

	rc = sas_register_ha(sha);
	if (rc)
		goto err_out_register_ha;

	rc = hisi_hba->hw->hw_init(hisi_hba);
	if (rc)
		goto err_out_register_ha;

	scsi_scan_host(shost);

	return 0;

err_out_register_ha:
	scsi_remove_host(shost);
err_out_ha:
	scsi_host_put(shost);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable_device:
	pci_disable_device(pdev);
err_out:
	return rc;
}

static void
hisi_sas_v3_destroy_irqs(struct pci_dev *pdev, struct hisi_hba *hisi_hba)
{
	int i;

	free_irq(pci_irq_vector(pdev, 1), hisi_hba);
	free_irq(pci_irq_vector(pdev, 2), hisi_hba);
	free_irq(pci_irq_vector(pdev, 11), hisi_hba);
	for (i = 0; i < hisi_hba->cq_nvecs; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		int nr = hisi_sas_intr_conv ? 16 : 16 + i;

		free_irq(pci_irq_vector(pdev, nr), cq);
	}
	pci_free_irq_vectors(pdev);
}

static void hisi_sas_v3_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sas_ha_struct *sha = dev_get_drvdata(dev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct Scsi_Host *shost = sha->core.shost;

	hisi_sas_debugfs_exit(hisi_hba);

	if (timer_pending(&hisi_hba->timer))
		del_timer(&hisi_hba->timer);

	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_v3_destroy_irqs(pdev, hisi_hba);
	hisi_sas_kill_tasklets(hisi_hba);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	hisi_sas_free(hisi_hba);
	scsi_host_put(shost);
}

static void hisi_sas_reset_prepare_v3_hw(struct pci_dev *pdev)
{
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct device *dev = hisi_hba->dev;
	int rc;

	dev_info(dev, "FLR prepare\n");
	set_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags);
	hisi_sas_controller_reset_prepare(hisi_hba);

	rc = disable_host_v3_hw(hisi_hba);
	if (rc)
		dev_err(dev, "FLR: disable host failed rc=%d\n", rc);
}

static void hisi_sas_reset_done_v3_hw(struct pci_dev *pdev)
{
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct device *dev = hisi_hba->dev;
	int rc;

	hisi_sas_init_mem(hisi_hba);

	rc = hw_init_v3_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "FLR: hw init failed rc=%d\n", rc);
		return;
	}

	hisi_sas_controller_reset_done(hisi_hba);
	dev_info(dev, "FLR done\n");
}

enum {
	/* instances of the controller */
	hip08,
};

static int hisi_sas_v3_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct device *dev = hisi_hba->dev;
	struct Scsi_Host *shost = hisi_hba->shost;
	pci_power_t device_state;
	int rc;

	if (!pdev->pm_cap) {
		dev_err(dev, "PCI PM not supported\n");
		return -ENODEV;
	}

	if (test_and_set_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags))
		return -1;

	scsi_block_requests(shost);
	set_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
	flush_workqueue(hisi_hba->wq);

	rc = disable_host_v3_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "PM suspend: disable host failed rc=%d\n", rc);
		clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
		clear_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags);
		scsi_unblock_requests(shost);
		return rc;
	}

	hisi_sas_init_mem(hisi_hba);

	device_state = pci_choose_state(pdev, state);
	dev_warn(dev, "entering operating state [D%d]\n",
			device_state);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, device_state);

	hisi_sas_release_tasks(hisi_hba);

	sas_suspend_ha(sha);
	return 0;
}

static int hisi_sas_v3_resume(struct pci_dev *pdev)
{
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct Scsi_Host *shost = hisi_hba->shost;
	struct device *dev = hisi_hba->dev;
	unsigned int rc;
	pci_power_t device_state = pdev->current_state;

	dev_warn(dev, "resuming from operating state [D%d]\n",
		 device_state);
	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	rc = pci_enable_device(pdev);
	if (rc)
		dev_err(dev, "enable device failed during resume (%d)\n", rc);

	pci_set_master(pdev);
	scsi_unblock_requests(shost);
	clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);

	sas_prep_resume_ha(sha);
	init_reg_v3_hw(hisi_hba);
	hisi_hba->hw->phys_init(hisi_hba);
	sas_resume_ha(sha);
	clear_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags);

	return 0;
}

static const struct pci_device_id sas_v3_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, 0xa230), hip08 },
	{}
};
MODULE_DEVICE_TABLE(pci, sas_v3_pci_table);

static const struct pci_error_handlers hisi_sas_err_handler = {
	.reset_prepare	= hisi_sas_reset_prepare_v3_hw,
	.reset_done	= hisi_sas_reset_done_v3_hw,
};

static struct pci_driver sas_v3_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= sas_v3_pci_table,
	.probe		= hisi_sas_v3_probe,
	.remove		= hisi_sas_v3_remove,
	.suspend	= hisi_sas_v3_suspend,
	.resume		= hisi_sas_v3_resume,
	.err_handler	= &hisi_sas_err_handler,
};

module_pci_driver(sas_v3_pci_driver);
module_param_named(intr_conv, hisi_sas_intr_conv, bool, 0444);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v3 hw driver based on pci device");
MODULE_ALIAS("pci:" DRV_NAME);
