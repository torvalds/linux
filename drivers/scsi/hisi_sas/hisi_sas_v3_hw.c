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
#define AXI_AHB_CLK_CFG			0x3c
#define AXI_USER1			0x48
#define AXI_USER2			0x4c
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
#define RX_IDAF_DWORD0			(PORT_BASE + 0xc4)
#define RXOP_CHECK_CFG_H		(PORT_BASE + 0xfc)
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

static void init_reg_v3_hw(struct hisi_hba *hisi_hba)
{
	int i;

	/* Global registers init */
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE,
			 (u32)((1ULL << hisi_hba->queue_count) - 1));
	hisi_sas_write32(hisi_hba, AXI_USER1, 0x0);
	hisi_sas_write32(hisi_hba, AXI_USER2, 0x40000060);
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
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xfff00c30);
	hisi_sas_write32(hisi_hba, AWQOS_AWCACHE_CFG, 0xf0f0);
	hisi_sas_write32(hisi_hba, ARQOS_ARCACHE_CFG, 0xf0f0);
	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK+0x4*i, 0);

	hisi_sas_write32(hisi_hba, AXI_AHB_CLK_CFG, 1);
	hisi_sas_write32(hisi_hba, HYPER_STREAM_ID_EN_CFG, 1);
	hisi_sas_write32(hisi_hba, CFG_MAX_TAG, 0xfff07fff);

	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, PROG_PHY_LINK_RATE, 0x801);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, RXOP_CHECK_CFG_H, 0x1000);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0x8ffffbff);
		hisi_sas_phy_write32(hisi_hba, i, SL_CFG, 0x83f801fc);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_DWS_RESET_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_OOB_RESTART_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL, 0x199b4fa);
		hisi_sas_phy_write32(hisi_hba, i, SAS_SSP_CON_TIMER_CFG,
				     0xa0064);
		hisi_sas_phy_write32(hisi_hba, i, SAS_STP_CON_TIMER_CFG,
				     0xa0064);
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

static int hw_init_v3_hw(struct hisi_hba *hisi_hba)
{
	init_reg_v3_hw(hisi_hba);

	return 0;
}

static void enable_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg |= PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void start_phy_v3_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	config_id_frame_v3_hw(hisi_hba, phy_no);
	config_phy_opt_mode_v3_hw(hisi_hba, phy_no);
	enable_phy_v3_hw(hisi_hba, phy_no);
}

static void start_phys_v3_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		start_phy_v3_hw(hisi_hba, i);
}

static void phys_init_v3_hw(struct hisi_hba *hisi_hba)
{
	start_phys_v3_hw(hisi_hba);
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
	int res = 0;
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

	return res;
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
					& (~CHL_INT0_HOTPLUG_TOUT_MSK)
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
	struct hisi_sas_err_record_v3 *record =	slot->status_buffer;
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
		struct ssp_response_iu *iu = slot->status_buffer +
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
		       slot->status_buffer +
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

static const struct hisi_sas_hw hisi_sas_v3_hw = {
	.hw_init = hisi_sas_v3_init,
	.max_command_entries = HISI_SAS_COMMAND_ENTRIES_V3_HW,
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v3_hdr),
	.sl_notify = sl_notify_v3_hw,
	.phys_init = phys_init_v3_hw,
};

static struct Scsi_Host *
hisi_sas_shost_alloc_pci(struct pci_dev *pdev)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;

	shost = scsi_host_alloc(hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost)
		goto err_out;
	hisi_hba = shost_priv(shost);

	hisi_hba->hw = &hisi_sas_v3_hw;
	hisi_hba->pci_dev = pdev;
	hisi_hba->dev = dev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	init_timer(&hisi_hba->timer);

	if (hisi_sas_get_fw_info(hisi_hba) < 0)
		goto err_out;

	if (hisi_sas_alloc(hisi_hba, shost)) {
		hisi_sas_free(hisi_hba);
		goto err_out;
	}

	return shost;
err_out:
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
	kfree(shost);
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
	}
	pci_free_irq_vectors(pdev);
}

static void hisi_sas_v3_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sas_ha_struct *sha = dev_get_drvdata(dev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_free(hisi_hba);
	hisi_sas_v3_destroy_irqs(pdev, hisi_hba);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
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

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v3 hw driver based on pci device");
MODULE_ALIAS("platform:" DRV_NAME);
