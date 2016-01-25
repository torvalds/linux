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

/* Completion header */
/* dw0 */
#define CMPLT_HDR_RSPNS_XFRD_OFF	10
#define CMPLT_HDR_RSPNS_XFRD_MSK	(0x1 << CMPLT_HDR_RSPNS_XFRD_OFF)
#define CMPLT_HDR_ERX_OFF		12
#define CMPLT_HDR_ERX_MSK		(0x1 << CMPLT_HDR_ERX_OFF)
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

enum {
	HISI_SAS_PHY_PHY_UPDOWN,
	HISI_SAS_PHY_CHNL_INT,
	HISI_SAS_PHY_INT_NR
};

#define HISI_SAS_COMMAND_ENTRIES_V2_HW 4096

static u32 hisi_sas_read32(struct hisi_hba *hisi_hba, u32 off)
{
	void __iomem *regs = hisi_hba->regs + off;

	return readl(regs);
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

	return 0;
}

static void init_reg_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	struct device_node *np = dev->of_node;
	int i;

	/* Global registers init */

	/* Deal with am-max-transmissions quirk */
	if (of_get_property(np, "hip06-sas-v2-quirk-amt", NULL)) {
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
	hisi_sas_write32(hisi_hba, MAX_CON_TIME_LIMIT_TIME, 0x4E20);
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

static void start_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	config_id_frame_v2_hw(hisi_hba, phy_no);
	config_phy_opt_mode_v2_hw(hisi_hba, phy_no);
	enable_phy_v2_hw(hisi_hba, phy_no);
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

	if ((complete_hdr->dw0 & CMPLT_HDR_ERX_MSK) &&
		(!(complete_hdr->dw0 & CMPLT_HDR_RSPNS_XFRD_MSK))) {
		dev_dbg(dev, "%s slot %d has error info 0x%x\n",
			__func__, slot->cmplt_queue_slot,
			complete_hdr->dw0 & CMPLT_HDR_ERX_MSK);

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
	unsigned long flags;

	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 1);

	spin_lock_irqsave(&hisi_hba->lock, flags);
	sas_ha->notify_port_event(sas_phy, PORTE_BROADCAST_RCVD);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

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
	int phy_no;

	phy_no = sas_phy->id;
	initial_fis = &hisi_hba->initial_fis[phy_no];
	fis = &initial_fis->fis;

	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK1);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, ent_msk | 1 << phy_no);

	ent_int = hisi_sas_read32(hisi_hba, ENT_INT_SRC1);
	ent_tmp = ent_int;
	ent_int >>= ENT_INT_SRC1_D2H_FIS_CH1_OFF * (phy_no % 4);
	if ((ent_int & ENT_INT_SRC1_D2H_FIS_CH0_MSK) == 0) {
		dev_warn(dev, "sata int: phy%d did not receive FIS\n", phy_no);
		hisi_sas_write32(hisi_hba, ENT_INT_SRC1, ent_tmp);
		hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, ent_msk);
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
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, ent_tmp);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, ent_msk);

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
	.sl_notify = sl_notify_v2_hw,
	.get_wideport_bitmap = get_wideport_bitmap_v2_hw,
	.slot_complete = slot_complete_v2_hw,
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

static struct platform_driver hisi_sas_v2_driver = {
	.probe = hisi_sas_v2_probe,
	.remove = hisi_sas_v2_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sas_v2_of_match,
	},
};

module_platform_driver(hisi_sas_v2_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v2 hw driver");
MODULE_ALIAS("platform:" DRV_NAME);
