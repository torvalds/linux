/*
 * TC956X ethernet driver.
 *
 * tc956xmac_ioctl.h
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
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
 *  14 Sep 2021 : 1. Synchronization between ethtool vlan features
 *  		  "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
 * 		  2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter",
 *  		  and "tx-vlan-offload".
 * 		  3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
 * 		  4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.
 *  VERSION     : 01-00-13
 */

#ifndef _IOCTL_H__
#define _IOCTL_H__

/* Note: Multiple macro definitions for TC956X_PCIE_LOGSTAT and TC956X_PCIE_LOGSTAT_SUMMARY_ENABLE.
 * Please also define/undefine same macro in common.h, if changing in this file
 */
/* #undef TC956X_PCIE_LOGSTAT */
#define TC956X_PCIE_LOGSTAT
#ifdef TC956X_PCIE_LOGSTAT
/* Enable/Disable Logstat Summary Print during probe and resume. */
#undef TC956X_PCIE_LOGSTAT_SUMMARY_ENABLE
#endif

enum ioctl_commands {
	TC956XMAC_GET_CBS = 0x1,
	TC956XMAC_SET_CBS = 0x2,
	TC956XMAC_GET_EST = 0x3,
	TC956XMAC_SET_EST = 0x4,
#ifdef TC956X_UNSUPPORTED_UNTESTED
	TC956XMAC_GET_FPE = 0x5,
	TC956XMAC_SET_FPE = 0x6,
#endif
	TC956XMAC_GET_RXP = 0x7,
	TC956XMAC_SET_RXP = 0x8,
	TC956XMAC_GET_SPEED = 0x9,
	TC956XMAC_GET_TX_FREE_DESC = 0xa,
	TC956XMAC_REG_RD = 0xb,
	TC956XMAC_REG_WR = 0xc,
	TC956XMAC_SET_MAC_LOOPBACK = 0xd,
	TC956XMAC_SET_PHY_LOOPBACK = 0xe,
	TC956XMAC_L2_DA_FILTERING_CMD = 0xf,
	TC956XMAC_SET_PPS_OUT = 0x10,
	TC956XMAC_PTPCLK_CONFIG = 0x11,
	TC956XMAC_SA0_VLAN_INS_REP_REG = 0x12,
	TC956XMAC_SA1_VLAN_INS_REP_REG = 0x13,
	TC956XMAC_GET_TX_QCNT = 0x14,
	TC956XMAC_GET_RX_QCNT = 0x15,
	TC956XMAC_PCIE_CONFIG_REG_RD = 0x16,
	TC956XMAC_PCIE_CONFIG_REG_WR = 0x17,
	TC956XMAC_VLAN_FILTERING = 0x18,
	TC956XMAC_PTPOFFLOADING = 0x19,
	TC956X_GET_FW_STATUS = 0x1a,
	TC956XMAC_ENABLE_AUX_TIMESTAMP = 0x1b,
	TC956XMAC_ENABLE_ONESTEP_TIMESTAMP = 0x1c,
#ifdef TC956X_PCIE_LOGSTAT
	TC956X_PCIE_STATE_LOG_SUMMARY = 0x1d, /* LOGSTAT : Prints State Log Summary */
	TC956X_PCIE_GET_PCIE_LINK_PARAMS = 0x1e, /* LOGSTAT : Return and Print PCIe LTSSM, DLL, Speed and Lane Width */
	TC956X_PCIE_STATE_LOG_ENABLE    = 0x1f, /* LOGSTAT : Set State Log Enable/Disable */
#endif /* #ifdef TC956X_PCIE_LOGSTAT */

	TC956XMAC_VLAN_STRIP_CONFIG   = 0x22,
	TC956XMAC_PCIE_LANE_CHANGE	= 0x23,
	TC956XMAC_PCIE_SET_TX_MARGIN	= 0x24,
	TC956XMAC_PCIE_SET_TX_DEEMPHASIS	= 0x25, /*Enable or disable Tx de-emphasis*/
	TC956XMAC_PCIE_SET_DFE	= 0x26,
	TC956XMAC_PCIE_SET_CTLE	= 0x27,
	TC956XMAC_PCIE_SPEED_CHANGE	= 0x28,
#ifdef TC956X_SRIOV_PF
	TC956XMAC_GET_QMODE = 0x29,
	TC956XMAC_SET_QMODE = 0x2a,
	TC956XMAC_GET_CBS100 = 0x2b,
	TC956XMAC_GET_CBS1000 = 0x2c,
	TC956XMAC_GET_CBS2500 = 0x2d,
	TC956XMAC_GET_CBS5000 = 0x2e,
	TC956XMAC_GET_CBS10000 = 0x2f,
	TC956XMAC_SET_CBS100 = 0x30,
	TC956XMAC_SET_CBS1000 = 0x31,
	TC956XMAC_SET_CBS2500 = 0x32,
	TC956XMAC_SET_CBS5000 = 0x33,
	TC956XMAC_SET_CBS10000 = 0x34,
#endif
};

#define TC956XMAC_GET_CBS_1	0x29 /* Sub commands - CBS */
#define TC956XMAC_GET_CBS_2	0x2a
#define TC956XMAC_SET_CBS_1	0x2b
#define TC956XMAC_SET_CBS_2	0x2c
#define TC956XMAC_SET_EST_1	0x2d
#define TC956XMAC_SET_EST_2	0x2e
#define TC956XMAC_SET_EST_3	0x2f
#define TC956XMAC_SET_EST_4	0x30
#define TC956XMAC_SET_EST_5	0x31
#define TC956XMAC_SET_EST_6	0x32
#define TC956XMAC_SET_EST_7	0x33
#define TC956XMAC_SET_EST_8	0x34
#define TC956XMAC_SET_EST_9	0x35
#define TC956XMAC_SET_EST_10 0x36
#define TC956XMAC_SET_FPE_1	0x37
#define TC956XMAC_GET_FPE_1	0x38
#define TC956XMAC_GET_EST_1	0x39
#define TC956XMAC_GET_EST_2	0x3a
#define TC956XMAC_GET_EST_3	0x3b
#define TC956XMAC_GET_EST_4	0x3c
#define TC956XMAC_GET_EST_5	0x3d
#define TC956XMAC_GET_EST_6	0x3e
#define TC956XMAC_GET_EST_7	0x3f
#define TC956XMAC_GET_EST_8	0x40
#define TC956XMAC_GET_EST_9	0x41
#define TC956XMAC_GET_EST_10	0x42
#define TC956XMAC_SET_RXP_1	0x43
#define TC956XMAC_SET_RXP_2	0x44
#define TC956XMAC_SET_RXP_3	0x45
#define TC956XMAC_SET_RXP_4	0x46
#define TC956XMAC_SET_RXP_5	0x47
#define TC956XMAC_SET_RXP_6	0x48
#define TC956XMAC_SET_RXP_7	0x49
#define TC956XMAC_SET_RXP_8	0x4a
#define TC956XMAC_SET_RXP_9	0x4b
#define TC956XMAC_SET_RXP_10	0x4c
#define TC956XMAC_SET_RXP_11	0x4d
#define TC956XMAC_SET_RXP_12	0x4e
#define TC956XMAC_SET_RXP_13	0x4f
#define TC956XMAC_SET_RXP_14	0x50
#define TC956XMAC_SET_RXP_15	0x51
#define TC956XMAC_SET_RXP_16	0x52
#define TC956XMAC_SET_RXP_17	0x53
#define TC956XMAC_SET_RXP_18	0x54
#define TC956XMAC_SET_RXP_19	0x55
#define TC956XMAC_SET_RXP_20	0x56
#define TC956XMAC_SET_RXP_21	0x57
#define TC956XMAC_SET_RXP_22	0x58
#define TC956XMAC_SET_RXP_23	0x59
#define TC956XMAC_SET_RXP_24	0x5a
#define TC956XMAC_SET_RXP_25	0x5b
#define TC956XMAC_SET_RXP_26	0x5c
#define TC956XMAC_SET_RXP_27	0x5d
#define TC956XMAC_SET_RXP_28	0x5e
#define TC956XMAC_SET_RXP_29	0x5f
#define TC956XMAC_SET_RXP_30	0x60
#define TC956XMAC_SET_RXP_31	0x61
#define TC956XMAC_SET_RXP_32	0x62
#define TC956XMAC_SET_RXP_33	0x63
#define TC956XMAC_SET_RXP_34	0x64
#define TC956XMAC_SET_RXP_35	0x65
#define TC956XMAC_SET_RXP_36	0x66
#define TC956XMAC_SET_RXP_37	0x67
#define TC956XMAC_GET_RXP_1	    0x68
#define TC956XMAC_GET_RXP_2	    0x69
#define TC956XMAC_GET_RXP_3	    0x6a
#define TC956XMAC_GET_RXP_4	    0x6b
#define TC956XMAC_GET_RXP_5	    0x6c
#define TC956XMAC_GET_RXP_6	    0x6d
#define TC956XMAC_GET_RXP_7	    0x6e
#define TC956XMAC_GET_RXP_8	    0x6f
#define TC956XMAC_GET_RXP_9	    0x70
#define TC956XMAC_GET_RXP_10    0x71
#define TC956XMAC_GET_RXP_11    0x72
#define TC956XMAC_GET_RXP_12    0x73
#define TC956XMAC_GET_RXP_13    0x74
#define TC956XMAC_GET_RXP_14    0x75
#define TC956XMAC_GET_RXP_15    0x76
#define TC956XMAC_GET_RXP_16    0x77
#define TC956XMAC_GET_RXP_17    0x78
#define TC956XMAC_GET_RXP_18    0x79
#define TC956XMAC_GET_RXP_19    0x7a
#define TC956XMAC_GET_RXP_20    0x7b
#define TC956XMAC_GET_RXP_21    0x7c
#define TC956XMAC_GET_RXP_22    0x7d
#define TC956XMAC_GET_RXP_23    0x7e
#define TC956XMAC_GET_RXP_24    0x7f
#define TC956XMAC_GET_RXP_25    0x80
#define TC956XMAC_GET_RXP_26    0x81
#define TC956XMAC_GET_RXP_27    0x82
#define TC956XMAC_GET_RXP_28    0x83
#define TC956XMAC_GET_RXP_29    0x84
#define TC956XMAC_GET_RXP_30    0x85
#define TC956XMAC_GET_RXP_31    0x86
#define TC956XMAC_GET_RXP_32    0x87
#define TC956XMAC_GET_RXP_33    0x88
#define TC956XMAC_GET_RXP_34    0x89
#define TC956XMAC_GET_RXP_35    0x8a
#define TC956XMAC_GET_RXP_36    0x8b
#define TC956XMAC_GET_RXP_37    0x8c
#define OPCODE_MBX_VF_GET_HW_STMP		0x8d
#define OPCODE_MBX_VF_GET_MII_REG_1		0x8e
#define OPCODE_MBX_VF_GET_MII_REG_2		0x8f

enum ethtool_command {
	TC956XMAC_GET_PAUSE_PARAM = 0x19,
	TC956XMAC_GET_EEE = 0x20,
	TC956XMAC_GET_COALESCE = 0x21,
	TC956XMAC_GET_TS_INFO = 0x22,
};

#define SIOCSTIOCTL	SIOCDEVPRIVATE

#define TC956XMAC_IOCTL_QMODE_DCB		0x0
#define TC956XMAC_IOCTL_QMODE_AVB		0x1

#define TC956XMAC0_REG 0
#define TC956XMAC1_REG 1


/* Do not include the SA */
#define TC956XMAC_SA0_NONE			((TC956XMAC0_REG << 2) | 0)
/* Include/Insert the SA with value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_DESC_INSERT	((TC956XMAC0_REG << 2) | 1)
/* Replace the SA with the value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_DESC_REPLACE	((TC956XMAC0_REG << 2) | 2)
/* Include/Insert the SA with value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_REG_INSERT	((TC956XMAC0_REG << 2) | 2)
/* Replace the SA with the value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_REG_REPLACE	((TC956XMAC0_REG << 2) | 3)

/* Do not include the SA */
#define TC956XMAC_SA1_NONE			((TC956XMAC1_REG << 2) | 0)
/* Include/Insert the SA with value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_DESC_INSERT	((TC956XMAC1_REG << 2) | 1)
/* Replace the SA with the value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_DESC_REPLACE	((TC956XMAC1_REG << 2) | 2)
/* Include/Insert the SA with value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_REG_INSERT	((TC956XMAC1_REG << 2) | 2)
/* Replace the SA with the value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_REG_REPLACE	((TC956XMAC1_REG << 2) | 3)

/* for PTP offloading configuration */
#define TC956X_PTP_OFFLOADING_DISABLE			0
#define TC956X_PTP_OFFLOADING_ENABLE			1

#define TC956X_PTP_ORDINARY_SLAVE			1
#define TC956X_PTP_ORDINARY_MASTER			2
#define TC956X_PTP_TRASPARENT_SLAVE			3
#define TC956X_PTP_TRASPARENT_MASTER			4
#define TC956X_PTP_PEER_TO_PEER_TRANSPARENT		5

#define TC956X_AUX_SNAPSHOT_0				1

struct tc956x_ioctl_aux_snapshot {
	__u32 cmd;
	__u32 aux_snapshot_ctrl;
};
#ifdef TC956X_SRIOV_PF
struct tc956x_config_ptpoffloading {
	__u32 cmd;
	bool en_dis; /* Enable/Disable */
	int mode;  /* PTP Ordinary/Transparent Slave, Ordinary/Transparent Master, Peer to Peer Transparent */
	int domain_num; /* Domain Number between 0 and 0xFF */
	bool mc_uc; /* Enable/Disable */
	unsigned char mc_uc_addr[ETH_ALEN]; /* Mac Address */
};

struct tc956x_config_ost {
	__u32 cmd;
	bool en_dis; /* Enable/Disable OST */
};

struct tc956xmac_ioctl_qmode_cfg {
	__u32 cmd;
	__u32 queue_idx;
	__u32 queue_mode;
};
#elif defined TC956X_SRIOV_VF
struct tc956x_config_ptpoffloading {
	__u32 cmd;
	int en_dis;
	int mode;
	int domain_num;
	int mc_uc;
	unsigned char mc_uc_addr[ETH_ALEN];
};

struct tc956x_config_ost {
	__u32 cmd;
	int en_dis;
};
#endif
struct tc956xmac_ioctl_cbs_params {
	__u32 send_slope; /* Send Slope value supported 15 bits */
	__u32 idle_slope; /* Idle Slope value supported 20 bits */
	__u32 high_credit; /* High Credit value supported 28 bits */
	__u32 low_credit; /* Low Credit value supported 28 bits */
	__u32 percentage; /* Dummy */
};

struct tc956xmac_ioctl_cbs_cfg {
	__u32 cmd;
	__u32 queue_idx;
	struct tc956xmac_ioctl_cbs_params speed100cfg;
	struct tc956xmac_ioctl_cbs_params speed1000cfg;
	struct tc956xmac_ioctl_cbs_params speed10000cfg;
	struct tc956xmac_ioctl_cbs_params speed5000cfg;
	struct tc956xmac_ioctl_cbs_params speed2500cfg;

};

struct tc956xmac_ioctl_speed {
	__u32 cmd;
	__u32 queue_idx;
	__u32 connected_speed;
};
struct tc956xmac_ioctl_l2_da_filter {
	unsigned int cmd;
	unsigned int chInx;
	int command_error;
	/* 0 - perfect and 1 - hash filtering */
	int perfect_hash;
	/* 0 - perfect and 1 - inverse matching */
	int perfect_inverse_match;
};

struct tc956xmac_ioctl_vlan_filter {
	__u32 cmd;
	/* 0 - disable and 1 - enable */
	/* Please note 0 - disable is not supported */
	int filter_enb_dis;
	/* 0 - perfect and 1 - hash filtering */
	int perfect_hash;
	/* 0 - perfect and 1 - inverse matching */
	int perfect_inverse_match;
};

struct tc956xmac_ioctl_free_desc {
	__u32 cmd;
	__u32 queue_idx;
	void *ptr;
};

struct tc956xmac_PPS_Config {
	__u32 cmd;
	unsigned int ptpclk_freq; /* Maximum 250MHz */
	unsigned int ppsout_freq; /* PPS Output Frequency */
	unsigned int ppsout_duty; /* Duty Cycle between 0 and 100 */
	unsigned int ppsout_align_ns; /* first output align to ppsout_align_ns in ns. */
	unsigned short ppsout_ch; /* Ch0 and Ch1 */
	bool ppsout_align; /* first output align */
};

struct tc956xmac_ioctl_reg_rd_wr {
	__u32 cmd;
	__u32 queue_idx;
	__u32 bar_num;
	__u32 addr;
	void *ptr;
};

struct tc956xmac_ioctl_loopback {
	__u32 cmd;
	__u32 flags;
};
struct tc956xmac_ioctl_phy_loopback {
	__u32 cmd;
	__u32 flags;
	__u32 phy_reg;
	__u32 bit;
};

struct tc956xmac_rx_parser_entry {
	__le32 match_data;
	__le32 match_en;
	__u8 af:1;
	__u8 rf:1;
	__u8 im:1;
	__u8 nc:1;
	__u8 res1:4;
	__u8 frame_offset:6;
	__u8 res2:2;
	__u8 ok_index;
	__u8 res3;
	__u16 dma_ch_no;
	__u16 res4;
} __packed;

struct tc956xmac_ioctl_rxp_entry {
	__u32 match_data;
	__u32 match_en;
	__u8 af:1;
	__u8 rf:1;
	__u8 im:1;
	__u8 nc:1;
	__u8 res1:4;
	__u8 frame_offset:6;
	__u8 res2:2;
	__u8 ok_index;
	__u8 res3;
	__u16 dma_ch_no;
	__u16 res4;
} __attribute__((packed));

#define TC956XMAC_RX_PARSER_MAX_ENTRIES		128

struct tc956xmac_rx_parser_cfg {
	bool enable;
	__u32 nve;
	__u32 npe;
	struct tc956xmac_rx_parser_entry entries[TC956XMAC_RX_PARSER_MAX_ENTRIES];
};

struct tc956xmac_ioctl_rxp_cfg {
	__u32 cmd;
	__u32 frpes;
	__u32 enabled;
	__u32 nve;
	__u32 npe;
	struct tc956xmac_ioctl_rxp_entry entries[TC956XMAC_RX_PARSER_MAX_ENTRIES];
};

#define TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES		128

struct tc956xmac_ioctl_est_cfg {
	__u32 cmd;
	__u32 enabled;
	__u32 estwid;		/* parameter used to get the value and not used in set */
	__u32 estdep;		/* parameter used to get the value and not used in set */
	__u32 btr_offset[2];
	__u32 ctr[2];
	__u32 ter;
	__u32 gcl[TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES];
	__u32 gcl_size;
};

struct tc956xmac_ioctl_fpe_cfg {
	__u32 cmd;
	__u32 enabled;
	__u32 pec;
	__u32 afsz;
	__u32 RA_time;
	__u32 HA_time;
};

struct tc956xmac_ioctl_sa_ins_cfg {
	__u32 cmd;
	unsigned int control_flag;
	unsigned char mac_addr[ETH_ALEN];

};

struct tc956xmac_ioctl_tx_qcnt {
	__u32 cmd;
	__u32 queue_idx;
	void *ptr;
};

struct tc956xmac_ioctl_rx_qcnt {
	__u32 cmd;
	__u32 queue_idx;
	void *ptr;
};

struct tc956xmac_ioctl_pcie_reg_rd_wr {
	__u32 cmd;
	__u32 addr;
	void *ptr;
};

struct tc956x_ioctl_fwstatus {
	__u32 cmd;
	__u32 wdt_count;
	__u32 systick_count;
	__u32 fw_status;
};

struct tc956xmac_ioctl_vlan_strip_cfg {
	__u32 cmd;
	__u32 enabled; /* 1 to enable stripping, 0 to disable stripping */
};

enum lane_width {
	LANE_1	= 1,
	LANE_2	= 2,
	LANE_4	= 4,
};

enum pcie_speed {
	GEN_1	= 1, /*2.5 GT/s*/
	GEN_2	= 2, /*5 GT/s*/
	GEN_3	= 3, /*8 GT/s*/
};

/**
 * enum port - Enumeration for ports available
 */
enum ports {
	UPSTREAM_PORT     = 0U, /* Used for Calculating port Offset */
	DOWNSTREAM_PORT1  = 1U,
	DOWNSTREAM_PORT2  = 2U,
	INTERNAL_ENDPOINT = 3U,
};

#ifdef TC956X_PCIE_LOGSTAT

/**
 * enum state_log_enable - Enumeration for State Log Enable
 */
enum state_log_enable {
	STATE_LOG_DISABLE = 0U,
	STATE_LOG_ENABLE  = 1U,
};

/**
 * struct tc956x_pcie_link_params - PCIe Link Parameters
 *
 * PCIe Link Parameters
 * ltssm : Link Training and Status State Machine(LTSSM) Value(0 to 0x1F).
 * dll : Data Link Layer Active/Inactive State Value(0, 1).
 * speed : Link Speed (Gen1 : 2.5GT/s, Gen2 : 5 GT/s, Gen3 : 8GT/s).
 * width : Number of Active Lanes(0, 1, 2, 3, 4).
 */
struct tc956x_pcie_link_params {
	__u8 ltssm; /* Current Link Training and Status State Machine(LTSSM) Value */
	__u8 dll; /* Current Data Link Layer State */
	__u8 speed; /* Current Link Speed */
	__u8 width; /* Current Link Width */
};
#endif /* #ifdef TC956X_PCIE_LOGSTAT */
/**
 * struct tc956x_ioctl_pcie_lane_change - IOCTL arguments for
 * PCIe USP and DSPs lane change for power reduction
 * dst_lane - Lane number which has to be switched from current configuration
 * port - this lane switch to be operated on which port
 */
struct tc956x_ioctl_pcie_lane_change {
	__u32 cmd;
	enum lane_width target_lane_width; /* 1, 2, 4 */
	enum ports port; /* USP, DSP1, others return error*/
};


/**
 * struct tc956x_ioctl_pcie_set_tx_margin - IOCTL arguments for
 * PCIe USP and DSPs tx margin change for power reduction
 * txmargin - target tx margin value.
 * port - USP/DSP1/DSP2 on which Tx margin setting to be done
 */
struct tc956x_ioctl_pcie_set_tx_margin {
	__u32 cmd;
	__u16 txmargin; /* Set Only LSB 0:8 bits valid*/
	enum ports port; /* USP, DSP1, DSP2*/
};

/**
 * struct tc956x_ioctl_pcie_set_tx_deemphasis - IOCTL arguments for
 * PCIe USP and DSPs tx de-emphasis change for power reduction
 * enable - enable or disable tx demphasis setting value.
 * txpreset - Gen3 Tx preset value as defined by PCIe Base Specifications
 * port - USP/DSP1/DSP2 on which Tx Deemphasis setting to be done
 * Note: Gen1, Gen2 txpreset configuration not supported
 */
struct tc956x_ioctl_pcie_set_tx_deemphasis {
	__u32 cmd;
	__u8 enable; /* 1: enable, 0: disable*/
	__u8 txpreset; /* Gen3 tx preset, valid values are from 0 to 10; dont care in case of 'disable' selection */
	enum ports port; /* USP, DSP1, DSP2*/
};


/**
 * struct tc956x_ioctl_pcie_set_dfe - IOCTL arguments for
 * PCIe USP and DSPs DFE disable/enable for power reduction
 * enable - enable or disable DFE
 * port - USP/DSP1/DSP2 on which DFE should be enabled/disabled
 */
struct tc956x_ioctl_pcie_set_dfe {
	__u32 cmd;
	__u8 enable; /* 1: enable, 0: disable*/
	enum ports port; /* USP, DSP1, DSP2*/
};

/**
 * struct tc956x_ioctl_pcie_set_ctle_fixed_mode - IOCTL arguments for
 * PCIe USP and DSPs CTLE configuration
 * eqc_force - CTLE C value
 * eq_res - CTLE R value
 * vga_ctrl - CTLE VGA value
 * port - USP/DSP1/DSP2 on which CTLE fixed mode setting to be done
 */

struct tc956x_ioctl_pcie_set_ctle_fixed_mode {
	__u32 cmd;
	__u8 eqc_force;
	__u8 eq_res;
	__u8 vga_ctrl;
	enum ports port; /* USP, DSP1, DSP2r*/
};

/**
 * struct tc956x_ioctl_pcie_set_speed - IOCTL arguments for
 * PCIe USP and DSPs speed change
 * speed - target pcie gen speed
 */
struct tc956x_ioctl_pcie_set_speed {
	__u32 cmd;
	enum pcie_speed speed; /*1 or 2 or 3*/
};
#ifdef TC956X_PCIE_LOGSTAT

/**
 * struct tc956x_ioctl_state_log_summary - IOCTL arguments for
 * State Logging Summary.
 *
 * cmd - TC956X_PCIE_STATE_LOG_SUMMARY IOCTL.
 * port - USP/DSP1/DSP2/EP for which state logging enable/disbale to be done.
 */
struct tc956x_ioctl_state_log_summary {
	__u32 cmd;
	enum ports port; /* USP, DSP1, DSP2, EP*/
};

/**
 * struct tc956x_ioctl_state_log_enable - IOCTL arguments for
 * Enabling/Disabling State Logging.
 *
 * cmd - TC956X_PCIE_STATE_LOG_ENABLE IOCTL.
 * enable - enable/disable state log.
 * port - USP/DSP1/DSP2/EP for which state logging enable/disbale to be done.
 */
struct tc956x_ioctl_state_log_enable {
	__u32 cmd;
	enum state_log_enable enable; /* Enable/Disable */
	enum ports port; /* USP, DSP1, DSP2, EP*/
};

/**
 * struct tc956x_ioctl_pcie_link_params - IOCTL arguments for State log data
 *
 * cmd - TC956X_PCIE_GET_PCIE_LINK_PARAMS IOCTL.
 * link_param - structure for pcie link parameters to read.
 * port - USP/DSP1/DSP2/EP for which state link parameters to be read.
 */
struct tc956x_ioctl_pcie_link_params {
	__u32 cmd;
	struct tc956x_pcie_link_params *link_param;
	enum ports port;
};

#endif /* #ifdef TC956X_PCIE_LOGSTAT */

#endif /*_IOCT_H */
