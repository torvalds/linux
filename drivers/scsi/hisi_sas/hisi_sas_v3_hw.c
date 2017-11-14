/*
 * Copyright (c) 2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v3_hw"

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
#define PHY_CONN_RATE			0x30
#define ITCT_CLR			0x44
#define ITCT_CLR_EN_OFF			16
#define ITCT_CLR_EN_MSK			(0x1 << ITCT_CLR_EN_OFF)
#define ITCT_DEV_OFF			0
#define ITCT_DEV_MSK			(0x7ff << ITCT_DEV_OFF)
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
#define CFG_AGING_TIME			0xbc
#define HGC_DFX_CFG2			0xc0
#define CFG_ABT_SET_QUERY_IPTT	0xd4
#define CFG_SET_ABORTED_IPTT_OFF	0
#define CFG_SET_ABORTED_IPTT_MSK	(0xfff << CFG_SET_ABORTED_IPTT_OFF)
#define CFG_SET_ABORTED_EN_OFF	12
#define CFG_ABT_SET_IPTT_DONE	0xd8
#define CFG_ABT_SET_IPTT_DONE_OFF	0
#define HGC_IOMB_PROC1_STATUS	0x104
#define CFG_1US_TIMER_TRSH		0xcc
#define CHNL_INT_STATUS			0x148
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
#define CHNL_PHYUPDOWN_INT_MSK		0x1d0
#define CHNL_ENT_INT_MSK			0x1d4
#define HGC_COM_INT_MSK				0x1d8
#define ENT_INT_SRC_MSK3_ENT95_MSK_MSK	(0x1 << ENT_INT_SRC_MSK3_ENT95_MSK_OFF)
#define SAS_ECC_INTR			0x1e8
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
#define AWQOS_AWCACHE_CFG	0xc84
#define ARQOS_ARCACHE_CFG	0xc88

/* phy registers requiring init */
#define PORT_BASE			(0x2000)
#define PHY_CFG				(PORT_BASE + 0x0)
#define HARD_PHY_LINKRATE		(PORT_BASE + 0x4)
#define PHY_CFG_ENA_OFF			0
#define PHY_CFG_ENA_MSK			(0x1 << PHY_CFG_ENA_OFF)
#define PHY_CFG_DC_OPT_OFF		2
#define PHY_CFG_DC_OPT_MSK		(0x1 << PHY_CFG_DC_OPT_OFF)
#define PROG_PHY_LINK_RATE		(PORT_BASE + 0x8)
#define PHY_CTRL			(PORT_BASE + 0x14)
#define PHY_CTRL_RESET_OFF		0
#define PHY_CTRL_RESET_MSK		(0x1 << PHY_CTRL_RESET_OFF)
#define SL_CFG				(PORT_BASE + 0x84)
#define SL_CONTROL			(PORT_BASE + 0x94)
#define SL_CONTROL_NOTIFY_EN_OFF	0
#define SL_CONTROL_NOTIFY_EN_MSK	(0x1 << SL_CONTROL_NOTIFY_EN_OFF)
#define SL_CTA_OFF		17
#define SL_CTA_MSK		(0x1 << SL_CTA_OFF)
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

#define MAX_ITCT_HW			4096 /* max the hw can support */
#define DEFAULT_ITCT_HW		2048 /* reset value, not reprogrammed */
#if (HISI_SAS_MAX_DEVICES > DEFAULT_ITCT_HW)
#error Max ITCT exceeded
#endif

#define AXI_MASTER_CFG_BASE		(0x5000)
#define AM_CTRL_GLOBAL			(0x0)
#define AM_CURR_TRANS_RETURN	(0x150)

#define AM_CFG_MAX_TRANS		(0x5010)
#define AM_CFG_SINGLE_PORT_MAX_TRANS	(0x5014)
#define AXI_CFG					(0x5100)
#define AM_ROB_ECC_ERR_ADDR		(0x510c)
#define AM_ROB_ECC_ONEBIT_ERR_ADDR_OFF	0
#define AM_ROB_ECC_ONEBIT_ERR_ADDR_MSK	(0xff << AM_ROB_ECC_ONEBIT_ERR_ADDR_OFF)
#define AM_ROB_ECC_MULBIT_ERR_ADDR_OFF	8
#define AM_ROB_ECC_MULBIT_ERR_ADDR_MSK	(0xff << AM_ROB_ECC_MULBIT_ERR_ADDR_OFF)

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

enum {
	HISI_SAS_PHY_PHY_UPDOWN,
	HISI_SAS_PHY_CHNL_INT,
	HISI_SAS_PHY_INT_NR
};

#define DIR_NO_DATA 0
#define DIR_TO_INI 1
#define DIR_TO_DEVICE 2
#define DIR_RESERVED 3

#define CMD_IS_UNCONSTRAINT(cmd) \
	((cmd == ATA_CMD_READ_LOG_EXT) || \
	(cmd == ATA_CMD_READ_LOG_DMA_EXT) || \
	(cmd == ATA_CMD_DEV_RESET))

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

static void init_reg_v3_hw(struct hisi_hba *hisi_hba)
{
	int i;

	/* Global registers init */
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE,
			 (u32)((1ULL << hisi_hba->queue_count) - 1));
	hisi_sas_write32(hisi_hba, HGC_SAS_TXFAIL_RETRY_CTRL, 0x108);
	hisi_sas_write32(hisi_hba, CFG_1US_TIMER_TRSH, 0xd);
	hisi_sas_write32(hisi_hba, INT_COAL_EN, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 0xffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0xfefefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0xfefefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0xffffffff);
	hisi_sas_write32(hisi_hba, CHNL_PHYUPDOWN_INT_MSK, 0x0);
	hisi_sas_write32(hisi_hba, CHNL_ENT_INT_MSK, 0x0);
	hisi_sas_write32(hisi_hba, HGC_COM_INT_MSK, 0x0);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0x0);
	hisi_sas_write32(hisi_hba, AWQOS_AWCACHE_CFG, 0xf0f0);
	hisi_sas_write32(hisi_hba, ARQOS_ARCACHE_CFG, 0xf0f0);
	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK+0x4*i, 0);

	hisi_sas_write32(hisi_hba, HYPER_STREAM_ID_EN_CFG, 1);
	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE, 0x30000);

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, PROG_PHY_LINK_RATE, 0x801);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, RXOP_CHECK_CFG_H, 0x1000);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0x8ffffbff);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_DWS_RESET_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_OOB_RESTART_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL, 0x199b4fa);
		hisi_sas_phy_write32(hisi_hba, i, SAS_SSP_CON_TIMER_CFG,
				     0xa03e8);
		hisi_sas_phy_write32(hisi_hba, i, SAS_STP_CON_TIMER_CFG,
				     0xa03e8);
		hisi_sas_phy_write32(hisi_hba, i, STP_LINK_TIMER,
				     0x7f7a120);
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
		(0xfa << ITCT_HDR_SMP_TIMEOUT_OFF) |
		(1 << ITCT_HDR_AWT_CONTINUE_OFF) |
		(port->id << ITCT_HDR_PORT_ID_OFF));
	itct->qw0 = cpu_to_le64(qw0);

	/* qw1 */
	memcpy(&itct->sas_addr, device->sas_addr, SAS_ADDR_SIZE);
	itct->sas_addr = __swab64(itct->sas_addr);

	/* qw2 */
	if (!dev_is_sata(device))
		itct->qw2 = cpu_to_le64((5000ULL << ITCT_HDR_INLT_OFF) |
					(0x1ULL << ITCT_HDR_RTOLT_OFF));
}

static void free_device_v3_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_device *sas_dev)
{
	u64 dev_id = sas_dev->device_id;
	struct device *dev = hisi_hba->dev;
	struct hisi_sas_itct *itct = &hisi_hba->itct[dev_id];
	u32 reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);

	/* clear the itct interrupt state */
	if (ENT_INT_SRC3_ITC_INT_MSK & reg_val)
		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 ENT_INT_SRC3_ITC_INT_MSK);

	/* clear the itct table*/
	reg_val = hisi_sas_read32(hisi_hba, ITCT_CLR);
	reg_val |= ITCT_CLR_EN_MSK | (dev_id & ITCT_DEV_MSK);
	hisi_sas_write32(hisi_hba, ITCT_CLR, reg_val);

	udelay(10);
	reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
	if (ENT_INT_SRC3_ITC_INT_MSK & reg_val) {
		dev_dbg(dev, "got clear ITCT done interrupt\n");

		/* invalid the itct state*/
		memset(itct, 0, sizeof(struct hisi_sas_itct));
		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 ENT_INT_SRC3_ITC_INT_MSK);

		/* clear the itct */
		hisi_sas_write32(hisi_hba, ITCT_CLR, 0);
		dev_dbg(dev, "clear ITCT ok\n");
	}
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
	ret = readl_poll_timeout(hisi_hba->regs + AXI_CFG, val, !val,
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
	} else
		dev_err(dev, "no reset method!\n");

	return 0;
}

static int hw_init_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int rc;

	rc = reset_hw_v3_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "hisi_sas_reset_hw failed, rc=%d", rc);
		return rc;
	}

	msleep(100);
	init_reg_v3_hw(hisi_hba);

	return 0;
}

static void enable_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg |= PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void disable_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg &= ~PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
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

	disable_phy_v3_hw(hisi_hba, phy_no);
	if (phy->identify.device_type == SAS_END_DEVICE) {
		txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
		hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
					txid_auto | TX_HARDRST_MSK);
	}
	msleep(100);
	start_phy_v3_hw(hisi_hba, phy_no);
}

enum sas_linkrate phy_get_max_linkrate_v3_hw(void)
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

		start_phy_v3_hw(hisi_hba, i);
	}
}

static void sl_notify_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
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

	for (i = 0; i < hisi_hba->n_phy; i++)
		if (((phy_port_num_ma >> (i * 4)) & 0xf) == port_id)
			bitmap |= 1 << i;

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
		dev_warn(dev, "full queue=%d r=%d w=%d\n\n",
				queue, r, w);
		return -EAGAIN;
	}

	return 0;
}

static void start_delivery_v3_hw(struct hisi_sas_dq *dq)
{
	struct hisi_hba *hisi_hba = dq->hisi_hba;
	int dlvry_queue = dq->slot_prep->dlvry_queue;
	int dlvry_queue_slot = dq->slot_prep->dlvry_queue_slot;

	dq->wr_point = ++dlvry_queue_slot % HISI_SAS_QUEUE_SLOTS;
	hisi_sas_write32(hisi_hba, DLVRY_Q_0_WR_PTR + (dlvry_queue * 0x14),
			 dq->wr_point);
}

static int prep_prd_sge_v3_hw(struct hisi_hba *hisi_hba,
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

static int prep_ssp_v3_hw(struct hisi_hba *hisi_hba,
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
		rc = prep_prd_sge_v3_hw(hisi_hba, slot, hdr, task->scatter,
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

	return 0;
}

static int prep_smp_v3_hw(struct hisi_hba *hisi_hba,
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

static int get_ncq_tag_v3_hw(struct sas_task *task, u32 *tag)
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

static int prep_ata_v3_hw(struct hisi_hba *hisi_hba,
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

	hdr->dw0 = cpu_to_le32(port->id << CMD_HDR_PORT_OFF);
	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type))
		hdr->dw0 |= cpu_to_le32(3 << CMD_HDR_CMD_OFF);
	else
		hdr->dw0 |= cpu_to_le32(4 << CMD_HDR_CMD_OFF);

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
		task->ata_task.fis.command, task->data_dir))
		<< CMD_HDR_FRAME_TYPE_OFF;
	dw1 |= sas_dev->device_id << CMD_HDR_DEV_ID_OFF;

	if (CMD_IS_UNCONSTRAINT(task->ata_task.fis.command))
		dw1 |= 1 << CMD_HDR_UNCON_CMD_OFF;

	hdr->dw1 = cpu_to_le32(dw1);

	/* dw2 */
	if (task->ata_task.use_ncq && get_ncq_tag_v3_hw(task, &hdr_tag)) {
		task->ata_task.fis.sector_count |= (u8) (hdr_tag << 3);
		dw2 |= hdr_tag << CMD_HDR_NCQ_TAG_OFF;
	}

	dw2 |= (HISI_SAS_MAX_STP_RESP_SZ / 4) << CMD_HDR_CFL_OFF |
			2 << CMD_HDR_SG_MOD_OFF;
	hdr->dw2 = cpu_to_le32(dw2);

	/* dw3 */
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	if (has_data) {
		rc = prep_prd_sge_v3_hw(hisi_hba, slot, hdr, task->scatter,
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

static int prep_abort_v3_hw(struct hisi_hba *hisi_hba,
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
				   ((dev_is_sata(dev) ? 1:0)
					<< CMD_HDR_ABORT_DEVICE_TYPE_OFF) |
					(abort_flag
					 << CMD_HDR_ABORT_FLAG_OFF));

	/* dw1 */
	hdr->dw1 = cpu_to_le32(device_id
			<< CMD_HDR_DEV_ID_OFF);

	/* dw7 */
	hdr->dw7 = cpu_to_le32(tag_to_abort << CMD_HDR_ABORT_IPTT_OFF);
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	return 0;
}

static int phy_up_v3_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	int i, res = 0;
	u32 context, port_id, link_rate, hard_phy_linkrate;
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = hisi_hba->dev;

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
	hard_phy_linkrate = hisi_sas_phy_read32(hisi_hba, phy_no,
						HARD_PHY_LINKRATE);
	phy->maximum_linkrate = hard_phy_linkrate & 0xf;
	phy->minimum_linkrate = (hard_phy_linkrate >> 4) & 0xf;
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);

	/* Check for SATA dev */
	context = hisi_sas_read32(hisi_hba, PHY_CONTEXT);
	if (context & (1 << phy_no)) {
		struct hisi_sas_initial_fis *initial_fis;
		struct dev_to_host_fis *fis;
		u8 attached_sas_addr[SAS_ADDR_SIZE] = {0};

		dev_info(dev, "phyup: phy%d link_rate=%d\n", phy_no, link_rate);
		initial_fis = &hisi_hba->initial_fis[phy_no];
		fis = &initial_fis->fis;
		sas_phy->oob_mode = SATA_OOB_MODE;
		attached_sas_addr[0] = 0x50;
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
	queue_work(hisi_hba->wq, &phy->phyup_ws);

end:
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_PHY_ENABLE_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 0);

	return res;
}

static int phy_down_v3_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	u32 phy_state, sl_ctrl, txid_auto;
	struct device *dev = hisi_hba->dev;

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

	return 0;
}

static void phy_bcast_v3_hw(int phy_no, struct hisi_hba *hisi_hba)
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
					phy_bcast_v3_hw(phy_no, hisi_hba);
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

static irqreturn_t int_chnl_int_v3_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	struct device *dev = hisi_hba->dev;
	u32 ent_msk, ent_tmp, irq_msk;
	int phy_no = 0;

	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK3);
	ent_tmp = ent_msk;
	ent_msk |= ENT_INT_SRC_MSK3_ENT95_MSK_MSK;
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_msk);

	irq_msk = hisi_sas_read32(hisi_hba, CHNL_INT_STATUS)
				& 0xeeeeeeee;

	while (irq_msk) {
		u32 irq_value0 = hisi_sas_phy_read32(hisi_hba, phy_no,
						     CHL_INT0);
		u32 irq_value1 = hisi_sas_phy_read32(hisi_hba, phy_no,
						     CHL_INT1);
		u32 irq_value2 = hisi_sas_phy_read32(hisi_hba, phy_no,
						     CHL_INT2);

		if ((irq_msk & (4 << (phy_no * 4))) &&
						irq_value1) {
			if (irq_value1 & (CHL_INT1_DMAC_RX_ECC_ERR_MSK |
					  CHL_INT1_DMAC_TX_ECC_ERR_MSK))
				panic("%s: DMAC RX/TX ecc bad error! (0x%x)",
					dev_name(dev), irq_value1);

			hisi_sas_phy_write32(hisi_hba, phy_no,
					     CHL_INT1, irq_value1);
		}

		if (irq_msk & (8 << (phy_no * 4)) && irq_value2)
			hisi_sas_phy_write32(hisi_hba, phy_no,
					     CHL_INT2, irq_value2);


		if (irq_msk & (2 << (phy_no * 4)) && irq_value0) {
			hisi_sas_phy_write32(hisi_hba, phy_no,
					CHL_INT0, irq_value0
					& (~CHL_INT0_SL_RX_BCST_ACK_MSK)
					& (~CHL_INT0_SL_PHY_ENABLE_MSK)
					& (~CHL_INT0_NOT_RDY_MSK));
		}
		irq_msk &= ~(0xe << (phy_no * 4));
		phy_no++;
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_tmp);

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
	u32 dma_rx_err_type = record->dma_rx_err_type;
	u32 trans_tx_fail_type = record->trans_tx_fail_type;

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		if (dma_rx_err_type & RX_DATA_LEN_UNDERFLOW_MSK) {
			ts->residual = trans_tx_fail_type;
			ts->stat = SAS_DATA_UNDERRUN;
		} else if (complete_hdr->dw3 & CMPLT_HDR_IO_IN_TARGET_MSK) {
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
		} else if (complete_hdr->dw3 & CMPLT_HDR_IO_IN_TARGET_MSK) {
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
	enum exec_status sts;
	struct hisi_sas_complete_v3_hdr *complete_queue =
			hisi_hba->complete_hdr[slot->cmplt_queue];
	struct hisi_sas_complete_v3_hdr *complete_hdr =
			&complete_queue[slot->cmplt_queue_slot];
	int aborted;
	unsigned long flags;

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
		ts->stat = SAS_ABORTED_TASK;
		hisi_sas_slot_task_free(hisi_hba, task, slot);
		return -1;
	}

	if (unlikely(!sas_dev)) {
		dev_dbg(dev, "slot complete: port has not device\n");
		ts->stat = SAS_PHY_DOWN;
		goto out;
	}

	/*
	 * Use SAS+TMF status codes
	 */
	switch ((complete_hdr->dw0 & CMPLT_HDR_ABORT_STAT_MSK)
			>> CMPLT_HDR_ABORT_STAT_OFF) {
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
	if ((complete_hdr->dw0 & CMPLT_HDR_CMPLT_MSK) == 0x3) {
		slot_err_v3_hw(hisi_hba, task, slot);
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
		dev_err(dev, "slot complete: port %d has removed\n",
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

static void cq_tasklet_v3_hw(unsigned long val)
{
	struct hisi_sas_cq *cq = (struct hisi_sas_cq *)val;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	struct hisi_sas_slot *slot;
	struct hisi_sas_itct *itct;
	struct hisi_sas_complete_v3_hdr *complete_queue;
	u32 rd_point = cq->rd_point, wr_point, dev_id;
	int queue = cq->id;
	struct hisi_sas_dq *dq = &hisi_hba->dq[queue];

	complete_queue = hisi_hba->complete_hdr[queue];

	spin_lock(&dq->lock);
	wr_point = hisi_sas_read32(hisi_hba, COMPL_Q_0_WR_PTR +
				   (0x14 * queue));

	while (rd_point != wr_point) {
		struct hisi_sas_complete_v3_hdr *complete_hdr;
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
				slot_complete_v3_hw(hisi_hba, slot);

				act_tmp &= ~(1 << ncq_tag_count);
				ncq_tag_count = ffs(act_tmp);
			}
		} else {
			iptt = (complete_hdr->dw1) & CMPLT_HDR_IPTT_MSK;
			slot = &hisi_hba->slot_info[iptt];
			slot->cmplt_queue_slot = rd_point;
			slot->cmplt_queue = queue;
			slot_complete_v3_hw(hisi_hba, slot);
		}

		if (++rd_point >= HISI_SAS_QUEUE_SLOTS)
			rd_point = 0;
	}

	/* update rd_point */
	cq->rd_point = rd_point;
	hisi_sas_write32(hisi_hba, COMPL_Q_0_RD_PTR + (0x14 * queue), rd_point);
	spin_unlock(&dq->lock);
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

static int interrupt_init_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	struct pci_dev *pdev = hisi_hba->pci_dev;
	int vectors, rc;
	int i, k;
	int max_msi = HISI_SAS_MSI_COUNT_V3_HW;

	vectors = pci_alloc_irq_vectors(hisi_hba->pci_dev, 1,
					max_msi, PCI_IRQ_MSI);
	if (vectors < max_msi) {
		dev_err(dev, "could not allocate all msi (%d)\n", vectors);
		return -ENOENT;
	}

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

	/* Init tasklets for cq only */
	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct tasklet_struct *t = &cq->tasklet;

		rc = devm_request_irq(dev, pci_irq_vector(pdev, i+16),
					  cq_interrupt_v3_hw, 0,
					  DRV_NAME " cq", cq);
		if (rc) {
			dev_err(dev,
				"could not request cq%d interrupt, rc=%d\n",
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

		free_irq(pci_irq_vector(pdev, k+16), cq);
	}
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

	min -= SAS_LINK_RATE_1_5_GBPS;
	max -= SAS_LINK_RATE_1_5_GBPS;

	for (i = 0; i <= max; i++)
		rate_mask |= 1 << (i * 2);

	prog_phy_link_rate &= ~0xff;
	prog_phy_link_rate |= rate_mask;

	hisi_sas_phy_write32(hisi_hba, phy_no, PROG_PHY_LINK_RATE,
			prog_phy_link_rate);

	phy_hard_reset_v3_hw(hisi_hba, phy_no);
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

static int soft_reset_v3_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int rc;
	u32 status;

	interrupt_disable_v3_hw(hisi_hba);
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0x0);

	hisi_sas_stop_phys(hisi_hba);

	mdelay(10);

	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE + AM_CTRL_GLOBAL, 0x1);

	/* wait until bus idle */
	rc = readl_poll_timeout(hisi_hba->regs + AXI_MASTER_CFG_BASE +
		AM_CURR_TRANS_RETURN, status, status == 0x3, 10, 100);
	if (rc) {
		dev_err(dev, "axi bus is not idle, rc = %d\n", rc);
		return rc;
	}

	hisi_sas_init_mem(hisi_hba);

	return hw_init_v3_hw(hisi_hba);
}

static const struct hisi_sas_hw hisi_sas_v3_hw = {
	.hw_init = hisi_sas_v3_init,
	.setup_itct = setup_itct_v3_hw,
	.max_command_entries = HISI_SAS_COMMAND_ENTRIES_V3_HW,
	.get_wideport_bitmap = get_wideport_bitmap_v3_hw,
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v3_hdr),
	.free_device = free_device_v3_hw,
	.sl_notify = sl_notify_v3_hw,
	.prep_ssp = prep_ssp_v3_hw,
	.prep_smp = prep_smp_v3_hw,
	.prep_stp = prep_ata_v3_hw,
	.prep_abort = prep_abort_v3_hw,
	.get_free_slot = get_free_slot_v3_hw,
	.start_delivery = start_delivery_v3_hw,
	.slot_complete = slot_complete_v3_hw,
	.phys_init = phys_init_v3_hw,
	.phy_enable = enable_phy_v3_hw,
	.phy_disable = disable_phy_v3_hw,
	.phy_hard_reset = phy_hard_reset_v3_hw,
	.phy_get_max_linkrate = phy_get_max_linkrate_v3_hw,
	.phy_set_linkrate = phy_set_linkrate_v3_hw,
	.dereg_device = dereg_device_v3_hw,
	.soft_reset = soft_reset_v3_hw,
	.get_phys_state = get_phys_state_v3_hw,
};

static struct Scsi_Host *
hisi_sas_shost_alloc_pci(struct pci_dev *pdev)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;

	shost = scsi_host_alloc(hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost) {
		dev_err(dev, "shost alloc failed\n");
		return NULL;
	}
	hisi_hba = shost_priv(shost);

	hisi_hba->hw = &hisi_sas_v3_hw;
	hisi_hba->pci_dev = pdev;
	hisi_hba->dev = dev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	timer_setup(&hisi_hba->timer, NULL, 0);

	if (hisi_sas_get_fw_info(hisi_hba) < 0)
		goto err_out;

	if (hisi_sas_alloc(hisi_hba, shost)) {
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

	if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) != 0) ||
	    (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)) != 0)) {
		if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) ||
		   (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)) != 0)) {
			dev_err(dev, "No usable DMA addressing method\n");
			rc = -EIO;
			goto err_out_regions;
		}
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
		dev_err(dev, "cannot map register.\n");
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
	shost->sg_tablesize = min_t(u16, SG_ALL, HISI_SAS_SGE_PAGE_CNT);
	shost->can_queue = hisi_hba->hw->max_command_entries;
	shost->cmd_per_lun = hisi_hba->hw->max_command_entries;

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

	hisi_sas_init_add(hisi_hba);

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
	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];

		free_irq(pci_irq_vector(pdev, i+16), cq);
		tasklet_kill(&cq->tasklet);
	}
	pci_free_irq_vectors(pdev);
}

static void hisi_sas_v3_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sas_ha_struct *sha = dev_get_drvdata(dev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct Scsi_Host *shost = sha->core.shost;

	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_v3_destroy_irqs(pdev, hisi_hba);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	hisi_sas_free(hisi_hba);
	scsi_host_put(shost);
}

enum {
	/* instances of the controller */
	hip08,
};

static const struct pci_device_id sas_v3_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, 0xa230), hip08 },
	{}
};

static struct pci_driver sas_v3_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= sas_v3_pci_table,
	.probe		= hisi_sas_v3_probe,
	.remove		= hisi_sas_v3_remove,
};

module_pci_driver(sas_v3_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v3 hw driver based on pci device");
MODULE_ALIAS("platform:" DRV_NAME);
