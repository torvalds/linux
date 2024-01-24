/*
 * TC956X ethernet driver.
 *
 * tc956xmac_inc.h
 *
 * Copyright (C) 2009  STMicroelectronics Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  22 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  16 Aug 2021 : 1. PHY interrupt mode supported through .config_intr and .ack_interrupt API
 *  VERSION     : 01-00-09
 *  24 Aug 2021 : 1. Disable TC956X_PCIE_GEN3_SETTING and TC956X_LOAD_FW_HEADER macros and provide support via Makefile
 *  VERSION     : 01-00-10
 *  08 Dec 2021 : 1. Added Module parameters for Flow control thresholds per Queue.
 *  VERSION     : 01-00-30
 *  11 Jan 2022 : 1. Forced speed mode parameter added for fixed phy mode.
 *  VERSION     : 01-00-35
 *  26 Dec 2023 : 1. Added the support for TC commands taprio and flower
 *  VERSION     : 01-03-59
 */

#ifndef __TC956XMAC_PLATFORM_DATA
#define __TC956XMAC_PLATFORM_DATA

#include <linux/platform_device.h>
#include <linux/phy.h>
#include "tc956xmac_ioctl.h"

//#define TC956X
//#define TC956X_IOCTL_REG_RD_WR_ENABLE
//#define TC956X_WITHOUT_MDIO
//#define TC956X_PCIE_GEN3_SETTING
//#define TC956X_PCIE_DISABLE_DSP1 /*Enable this macro to disable DSP1 port*/
//#define TC956X_PCIE_DISABLE_DSP2 /*Enable this macro to disable DSP2 port*/
/* Enable for PORT0 interrupt mode, if commented polling mode */
#define TC956X_PHY_INTERRUPT_MODE_EMAC0
/* Enable for PORT1 interrupt mode, if commented polling mode */
#define TC956X_PHY_INTERRUPT_MODE_EMAC1

#ifdef TC956X
//#define TC956X_SRIOV_PF		/* Enable TC956X SRIOV Configuration */
//#define TC956X_MBX_DESIGN_HW_SEMAPHORE
#endif
/* Enable this macro to use Systick timer instead of Kernel timers
 * for handling Tx completion periodically
 */
#ifdef TC956X_AUTOMOTIVE_CONFIG
//#define TX_COMPLETION_WITHOUT_TIMERS
//#define TC956X_SW_MSI /*Enable this macro to process SW MSI when CM3 Systick Handler sends SW MSI*/
#define ENABLE_TX_TIMER /*Enable this macro to use Kernel timer. TC956X_SW_MSI can be disabled in this case */
#else
//#define TX_COMPLETION_WITHOUT_TIMERS
//#define TC956X_SW_MSI /*Enable this macro to process SW MSI when CM3 Systick Handler sends SW MSI*/
#define ENABLE_TX_TIMER /*Enable this macro to use Kernel timer. TC956X_SW_MSI can be disabled in this case */
#endif

/* By default macro is defined and for code coverage this macro has to be disabled */
#define TC956X_UNSUPPORTED_UNTESTED_FEATURE

//#define EEPROM_MAC_ADDR
//#define TC956X_MBX_DESIGN_HW_SEMAPHORE
//#define TC956X_SRIOV_VF    /* This macro mentions applicable changes for VF driver from CPE driver. It should be enabled along with TC956X macro */

#define MTL_MAX_RX_QUEUES	8
#define MTL_MAX_TX_QUEUES	8
#define MTL_MAX_TX_TC		5
#define TC956XMAC_CH_MAX		8

#define TC956XMAC_RX_COE_NONE	0
#define TC956XMAC_RX_COE_TYPE1	1
#define TC956XMAC_RX_COE_TYPE2	2

/*
 * Define the macros for CSR clock range parameters to be passed by
 * platform code.
 * This could also be configured at run time using CPU freq framework.
 */

/* MDC Clock Selection define*/
#define	TC956XMAC_CSR_60_100M	0x0	/* MDC = clk_scr_i/42 */
#define	TC956XMAC_CSR_100_150M	0x1	/* MDC = clk_scr_i/62 */
#define	TC956XMAC_CSR_20_35M	0x2	/* MDC = clk_scr_i/16 */
#define	TC956XMAC_CSR_35_60M	0x3	/* MDC = clk_scr_i/26 */
#define	TC956XMAC_CSR_150_250M	0x4	/* MDC = clk_scr_i/102 */
#define	TC956XMAC_CSR_250_300M	0x5	/* MDC = clk_scr_i/122 */

/* MTL algorithms identifiers */
#define MTL_TX_ALGORITHM_WRR	0x0
#define MTL_TX_ALGORITHM_WFQ	0x1
#define MTL_TX_ALGORITHM_DWRR	0x2
#define MTL_TX_ALGORITHM_SP	0x3
#define MTL_RX_ALGORITHM_SP	0x4
#define MTL_RX_ALGORITHM_WSP	0x5

/* RX/TX Queue Mode */
#define MTL_QUEUE_AVB		0x0
#define MTL_QUEUE_DCB		0x1
#define MTL_QUEUE_DISABLE	0x2

#ifdef TC956X_SRIOV_VF
#define TC956X_ENABLE 1
#define TC956X_DISABLE 0
#endif

/* The MDC clock could be set higher than the IEEE 802.3
 * specified frequency limit 0f 2.5 MHz, by programming a clock divider
 * of value different than the above defined values. The resultant MDIO
 * clock frequency of 12.5 MHz is applicable for the interfacing chips
 * supporting higher MDC clocks.
 * The MDC clock selection macros need to be defined for MDC clock rate
 * of 12.5 MHz, corresponding to the following selection.
 */
#define TC956XMAC_CSR_I_4		0x8	/* clk_csr_i/4 */
#define TC956XMAC_CSR_I_6		0x9	/* clk_csr_i/6 */
#define TC956XMAC_CSR_I_8		0xA	/* clk_csr_i/8 */
#define TC956XMAC_CSR_I_10		0xB	/* clk_csr_i/10 */
#define TC956XMAC_CSR_I_12		0xC	/* clk_csr_i/12 */
#define TC956XMAC_CSR_I_14		0xD	/* clk_csr_i/14 */
#define TC956XMAC_CSR_I_16		0xE	/* clk_csr_i/16 */
#define TC956XMAC_CSR_I_18		0xF	/* clk_csr_i/18 */

/* For TC956X, clk_scr_i = 125MHz */
#define TC956XMAC_XGMAC_MDC_CSR_4		0x0 /*clk_csr_i/4 */
#define TC956XMAC_XGMAC_MDC_CSR_6		0x1 /* clk_csr_i/6 */
#define TC956XMAC_XGMAC_MDC_CSR_8		0x2 /* clk_csr_i/8 */
#define TC956XMAC_XGMAC_MDC_CSR_10		0x3 /* clk_csr_i/10 */
#define TC956XMAC_XGMAC_MDC_CSR_12		0x4 /* clk_csr_i/12 */
#define TC956XMAC_XGMAC_MDC_CSR_14		0x5 /* clk_csr_i/14 */
#define TC956XMAC_XGMAC_MDC_CSR_16		0x6 /* clk_csr_i/16 */
#define TC956XMAC_XGMAC_MDC_CSR_18		0x7 /* clk_csr_i/18 */
#define TC956XMAC_XGMAC_MDC_CSR_62		0x8 /* clk_csr_i/62 */
#define TC956XMAC_XGMAC_MDC_CSR_102	0x9 /* clk_csr_i/102 */
#define TC956XMAC_XGMAC_MDC_CSR_122	0xA /* clk_csr_i/122 */
#define TC956XMAC_XGMAC_MDC_CSR_142	0xB /* clk_csr_i/142 */
#define TC956XMAC_XGMAC_MDC_CSR_162	0xC /* clk_csr_i/162 */
#define TC956XMAC_XGMAC_MDC_CSR_202	0xD /* clk_csr_i/202 */


/* AXI DMA Burst length supported */
#define DMA_AXI_BLEN_4		(1 << 1)
#define DMA_AXI_BLEN_8		(1 << 2)
#define DMA_AXI_BLEN_16		(1 << 3)
#define DMA_AXI_BLEN_32		(1 << 4)
#define DMA_AXI_BLEN_64		(1 << 5)
#define DMA_AXI_BLEN_128	(1 << 6)
#define DMA_AXI_BLEN_256	(1 << 7)
#define DMA_AXI_BLEN_ALL (DMA_AXI_BLEN_4 | DMA_AXI_BLEN_8 | DMA_AXI_BLEN_16 \
			| DMA_AXI_BLEN_32 | DMA_AXI_BLEN_64 \
			| DMA_AXI_BLEN_128 | DMA_AXI_BLEN_256)

/* Platfrom data for platform device structure's platform_data field */

struct tc956xmac_mdio_bus_data {
	unsigned int phy_mask;
	int *irqs;
	int probed_phy_irq;
	bool needs_reset;
};

struct tc956xmac_dma_cfg {
	int pbl;
	int txpbl;
	int rxpbl;
	bool pblx8;
	int fixed_burst;
	int mixed_burst;
	bool aal;
	bool eame;
};

#define AXI_BLEN	7
struct tc956xmac_axi {
	bool axi_lpi_en;
	bool axi_xit_frm;
	u32 axi_wr_osr_lmt;
	u32 axi_rd_osr_lmt;
	bool axi_kbbe;
	u32 axi_blen[AXI_BLEN];
	bool axi_fb;
	bool axi_mb;
	bool axi_rb;
};

#define EST_GCL		128
struct tc956xmac_est {
	int enable;
	u32 btr_offset[2];
	u32 btr[2];
	u32 ctr[2];
	u32 ter;
	u32 gcl[EST_GCL];
	u32 gcl_size;
#ifndef TC956X
	u32 gcl_unaligned[EST_GCL];
#endif
};

struct tc956xmac_rxq_cfg {
	u8 mode_to_use;
	u32 chan;
	u8 pkt_route;
	bool use_prio;
	u32 prio;
	u32 size;
	u32 rfd;
	u32 rfa;
};

struct tc956xmac_txq_cfg {
	u32 weight;
	u8 mode_to_use;
	/* Credit Base Shaper parameters */
	u32 send_slope;
	u32 idle_slope;
	u32 high_credit;
	u32 low_credit;
	bool use_prio;
	u32 prio;
	u32 tbs_en;
	u32 tso_en;
	u8 traffic_class;
	u32 size;
};

struct tc956xmac_fpe_cfg {
	bool enable;
	u32 pec_cfg;
	u32 afsz_cfg;
	u32 RA_time;
	u32 HA_time;
};

enum ch_owner {
	NOT_USED = 0,
	USE_IN_TC956X_SW = 1,
	USE_IN_OFFLOADER = 2,
};

struct plat_tc956xmacenet_data {
	int bus_id;
	int phy_addr;
	int interface;
	struct tc956xmac_rx_parser_cfg rxp_cfg;
	struct pci_dev *pdev;
	phy_interface_t phy_interface;
	struct tc956xmac_mdio_bus_data *mdio_bus_data;
	struct device_node *phy_node;
#ifndef TC956X_SRIOV_VF
	struct device_node *phylink_node;
#endif
	struct device_node *mdio_node;
	struct tc956xmac_dma_cfg *dma_cfg;
	struct tc956xmac_est *est;
	int clk_csr;
	int clk_crs;
	u8 mdc_clk;
	bool c45_needed;
	int has_gmac;
	int enh_desc;
	int tx_coe;
	int rx_coe;
	int bugged_jumbo;
	int pmt;
	int force_sf_dma_mode;
	int force_thresh_dma_mode;
	int riwt_off;
	int max_speed;
	int maxmtu;
	int multicast_filter_bins;
	int unicast_filter_entries;
	int tx_fifo_size;
	int rx_fifo_size;
	u32 rx_queues_to_use;
	u32 tx_queues_to_use;
	/* Variable for queues and channels used in current driver instance */
#ifdef TC956X_SRIOV_PF
	u32 tx_ch_in_use[TC956XMAC_CH_MAX];
	u32 rx_ch_in_use[TC956XMAC_CH_MAX];
	u32 tx_q_in_use[MTL_MAX_TX_QUEUES];
	u32 rx_q_in_use[MTL_MAX_RX_QUEUES];
#elif defined TC956X_SRIOV_VF
	u32 vf_id; /*0 or 1 or 2*/
	u32 rx_queues_to_use_actual;
	u32 tx_queues_to_use_actual;
	u32 ch_in_use[TC956XMAC_CH_MAX];
	u32 tx_q_in_use[MTL_MAX_TX_QUEUES];
	u32 rx_q_in_use[MTL_MAX_TX_QUEUES];
	u32 tx_q_size[MTL_MAX_TX_QUEUES];
	u32 best_effort_ch_no;
	u32 gptp_ch_no;
	u32 avb_class_a_ch_no;
	u32 avb_class_b_ch_no;
	u32 tsn_ch_no;
	u32 tsn_application;
	u32 avb_application;
#endif
	u8 rx_sched_algorithm;
	u8 tx_sched_algorithm;
	struct tc956xmac_rxq_cfg rx_queues_cfg[MTL_MAX_RX_QUEUES];
	struct tc956xmac_txq_cfg tx_queues_cfg[MTL_MAX_TX_QUEUES];
	void (*fix_mac_speed)(void *priv, unsigned int speed);
	int (*init)(struct platform_device *pdev, void *priv);
	void (*exit)(struct platform_device *pdev, void *priv);
	struct mac_device_info *(*setup)(void *priv);
	void *bsp_priv;
	struct clk *tc956xmac_clk;
	struct clk *pclk;
	struct clk *clk_ptp_ref;
	unsigned int clk_ptp_rate;
	unsigned int clk_ref_rate;
	s32 ptp_max_adj;
	struct reset_control *tc956xmac_rst;
	struct tc956xmac_axi *axi;
	int has_gmac4;
	bool has_sun8i;
	bool tso_en;
	int rss_en;
	int sph_en;
	int mac_port_sel_speed;
	bool en_tx_lpi_clockgating;
	struct tc956xmac_fpe_cfg fpe_cfg;
	int has_xgmac;
	int (*cphy_read)(void *priv, int phyaddr, int phyreg);
	int (*cphy_write)(void *priv, int phyaddr, int phyreg, u16 phydata);
#ifndef TC956X_SRIOV_VF
	enum ch_owner tx_dma_ch_owner[MTL_MAX_TX_QUEUES];
	enum ch_owner rx_dma_ch_owner[MTL_MAX_RX_QUEUES];
#endif
	u32 port_num;
	u32 port_interface; /* Kernel module parameter variable for interface */
	bool phy_interrupt_mode; /* For Handling of PHY Operating mode */
	int forced_speed; /* applicable only in case of fixed phy mode */
	/*Preamble*/
	bool pse;
	int start_phy_addr;
	bool gate_mask;
};
#ifdef TC956X_SRIOV_VF
static const struct tc956xmac_vf_entry {
	u32 rx_queues_to_use_actual;
	u32 tx_queues_to_use_actual;
	/* No of and name of Rx and Tx channel will be always same */
	u32 ch_in_use[TC956XMAC_CH_MAX];
	u32 tx_q_in_use[MTL_MAX_TX_QUEUES];
	u32 rx_q_in_use[MTL_MAX_TX_QUEUES];
	u32 tx_q_size[MTL_MAX_TX_QUEUES];
	u32 best_effort_ch_no;
	u32 gptp_ch_no;
	u32 avb_class_a_ch_no;
	u32 avb_class_b_ch_no;
	u32 tsn_ch_no;
	u32 tsn_application; /* Set if VF supports TSN application */
	u32 avb_application; /* Set if VF supports AVB app */
	struct tc956xmac_txq_cfg tx_queues_cfg[MTL_MAX_TX_QUEUES];
} tc956xmac_vf[] = {
	/* VF[0] configurations */
	{
		.rx_queues_to_use_actual = 8,
		.tx_queues_to_use_actual = 8,
		.ch_in_use = {1, 0, 0, 0, 0, 1, 0, 0},
		.tx_q_in_use = {1, 0, 0, 0, 0, 1, 0, 0},
		.rx_q_in_use = {0, 0, 0, 0, 0, 0, 0, 0},
		.tx_q_size = {4096, 0, 0, 0, 0, 4096, 0, 0},
		.best_effort_ch_no = 0,
		.gptp_ch_no = 0,
		.avb_class_a_ch_no = 0, /* Not used - redirect to BE Ch */
		.avb_class_b_ch_no = 5,
		.tsn_ch_no = 0,	/* Not used - redirect to BE Ch */
		.tsn_application = 0,
		.avb_application = 1,
		.tx_queues_cfg = {
			{ /*Tx queue 0 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DCB,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = TC956X_ENABLE,
				.traffic_class = 0
			},
			{ /*Tx queue 1 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 2 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 3 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 4 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 5 configuration */
				/* CBS: queue 5 -> Class B traffic (25% BW) */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_AVB,
				.send_slope = 0x1800,
				.idle_slope = 0x800,
				.high_credit = 0x320000,
				.low_credit = 0xff6a0000,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = TC956X_ENABLE,
				.tso_en = 0x0,
				.traffic_class = 2
			},
			{ /*Tx queue 6 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 7 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
		},
	},
	/* VF[1] configurations */
	{
		.rx_queues_to_use_actual = 8,
		.tx_queues_to_use_actual = 8,
		.ch_in_use = {0, 1, 0, 0, 0, 0, 0, 0},
		.tx_q_in_use = {0, 1, 0, 0, 0, 0, 0, 0},
		.rx_q_in_use = {0, 0, 0, 0, 0, 0, 0, 0},
		.tx_q_size = {0, 4096, 0, 0, 0, 0, 0, 0},
		.best_effort_ch_no = 1,
		.gptp_ch_no = 1,
		.avb_class_a_ch_no = 1, /* Not used - redirect to BE Ch */
		.avb_class_b_ch_no = 1, /* Not used - redirect to BE Ch */
		.tsn_ch_no = 1,	/* Not used - redirect to BE Ch */
		.tsn_application = 0,
		.avb_application = 0,
		.tx_queues_cfg = {
			{ /*Tx queue 0 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio = false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 1 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DCB,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = TC956X_ENABLE,
				.traffic_class = 0
			},
			{ /*Tx queue 2 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 3 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 4 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 5 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 6 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 7 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
		},
	},
	/* VF[2] configurations */
	{
		.rx_queues_to_use_actual = 8,
		.tx_queues_to_use_actual = 8,
		.ch_in_use = {0, 0, 1, 0, 0, 0, 1, 1},
		.tx_q_in_use = {0, 0, 1, 0, 0, 0, 1, 1},
		.rx_q_in_use = {0, 0, 0, 0, 0, 0, 0, 0},
		.tx_q_size = {0, 0, 18432, 0, 0, 0, 4096, 4096},
		.best_effort_ch_no = 2,
		.gptp_ch_no = 2,
		.avb_class_a_ch_no = 6,
		.avb_class_b_ch_no = 2, /* Not used - redirect to BE Ch */
		.tsn_ch_no = 7,
		.tsn_application = 1,
		.avb_application = 1,
		.tx_queues_cfg = {
			{ /*Tx queue 0 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 1 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 2 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DCB,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = TC956X_ENABLE,
				.traffic_class = 0
			},
			{ /*Tx queue 3 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 4 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 5 configuration */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_DISABLE,
				.send_slope = 0x0,
				.idle_slope = 0x0,
				.high_credit = 0x0,
				.low_credit = 0x0,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = 0x0,
				.tso_en = 0x0,
				.traffic_class = 0
			},
			{ /*Tx queue 6 configuration */
				/* CBS: queue 6 -> Class A traffic (25% BW) */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_AVB,
				.send_slope = 0x1800,
				.idle_slope = 0x800,
				.high_credit = 0x320000,
				.low_credit = 0xff6a0000,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = TC956X_ENABLE,
				.tso_en = 0x0,
				.traffic_class = 3
			},
			{ /*Tx queue 7 configuration */
				/* CBS: queue 7 -> Class CDT traffic (40%) BW */
				.weight = 0x1,
				.mode_to_use = MTL_QUEUE_AVB,
				.send_slope = 0x1333,
				.idle_slope = 0xccc,
				.high_credit = 0x500000,
				.low_credit = 0xff880000,
				.use_prio =  false,
				.prio = 0x0,
				.tbs_en = TC956X_ENABLE,
				.tso_en = 0x0,
				.traffic_class = 4
			},
		},
	},

};
#endif
#endif
