/************************************************************************
 * s2io.h: A Linux PCI-X Ethernet driver for Neterion 10GbE Server NIC
 * Copyright(c) 2002-2007 Neterion Inc.

 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 ************************************************************************/
#ifndef _S2IO_H
#define _S2IO_H

#define TBD 0
#define s2BIT(loc)		(0x8000000000000000ULL >> (loc))
#define vBIT(val, loc, sz)	(((u64)val) << (64-loc-sz))
#define INV(d)  ((d&0xff)<<24) | (((d>>8)&0xff)<<16) | (((d>>16)&0xff)<<8)| ((d>>24)&0xff)

#ifndef BOOL
#define BOOL    int
#endif

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

#undef SUCCESS
#define SUCCESS 0
#define FAILURE -1
#define S2IO_MINUS_ONE 0xFFFFFFFFFFFFFFFFULL
#define S2IO_DISABLE_MAC_ENTRY 0xFFFFFFFFFFFFULL
#define S2IO_MAX_PCI_CONFIG_SPACE_REINIT 100
#define S2IO_BIT_RESET 1
#define S2IO_BIT_SET 2
#define CHECKBIT(value, nbit) (value & (1 << nbit))

/* Maximum time to flicker LED when asked to identify NIC using ethtool */
#define MAX_FLICKER_TIME	60000 /* 60 Secs */

/* Maximum outstanding splits to be configured into xena. */
enum {
	XENA_ONE_SPLIT_TRANSACTION = 0,
	XENA_TWO_SPLIT_TRANSACTION = 1,
	XENA_THREE_SPLIT_TRANSACTION = 2,
	XENA_FOUR_SPLIT_TRANSACTION = 3,
	XENA_EIGHT_SPLIT_TRANSACTION = 4,
	XENA_TWELVE_SPLIT_TRANSACTION = 5,
	XENA_SIXTEEN_SPLIT_TRANSACTION = 6,
	XENA_THIRTYTWO_SPLIT_TRANSACTION = 7
};
#define XENA_MAX_OUTSTANDING_SPLITS(n) (n << 4)

/*  OS concerned variables and constants */
#define WATCH_DOG_TIMEOUT		15*HZ
#define EFILL				0x1234
#define ALIGN_SIZE			127
#define	PCIX_COMMAND_REGISTER		0x62

/*
 * Debug related variables.
 */
/* different debug levels. */
#define	ERR_DBG		0
#define	INIT_DBG	1
#define	INFO_DBG	2
#define	TX_DBG		3
#define	INTR_DBG	4

/* Global variable that defines the present debug level of the driver. */
static int debug_level = ERR_DBG;

/* DEBUG message print. */
#define DBG_PRINT(dbg_level, args...)  if(!(debug_level<dbg_level)) printk(args)

#ifndef DMA_ERROR_CODE
#define DMA_ERROR_CODE          (~(dma_addr_t)0x0)
#endif

/* Protocol assist features of the NIC */
#define L3_CKSUM_OK 0xFFFF
#define L4_CKSUM_OK 0xFFFF
#define S2IO_JUMBO_SIZE 9600

/* Driver statistics maintained by driver */
struct swStat {
	unsigned long long single_ecc_errs;
	unsigned long long double_ecc_errs;
	unsigned long long parity_err_cnt;
	unsigned long long serious_err_cnt;
	unsigned long long soft_reset_cnt;
	unsigned long long fifo_full_cnt;
	unsigned long long ring_full_cnt[8];
	/* LRO statistics */
	unsigned long long clubbed_frms_cnt;
	unsigned long long sending_both;
	unsigned long long outof_sequence_pkts;
	unsigned long long flush_max_pkts;
	unsigned long long sum_avg_pkts_aggregated;
	unsigned long long num_aggregations;
	/* Other statistics */
	unsigned long long mem_alloc_fail_cnt;
	unsigned long long pci_map_fail_cnt;
	unsigned long long watchdog_timer_cnt;
	unsigned long long mem_allocated;
	unsigned long long mem_freed;
	unsigned long long link_up_cnt;
	unsigned long long link_down_cnt;
	unsigned long long link_up_time;
	unsigned long long link_down_time;

	/* Transfer Code statistics */
	unsigned long long tx_buf_abort_cnt;
	unsigned long long tx_desc_abort_cnt;
	unsigned long long tx_parity_err_cnt;
	unsigned long long tx_link_loss_cnt;
	unsigned long long tx_list_proc_err_cnt;

	unsigned long long rx_parity_err_cnt;
	unsigned long long rx_abort_cnt;
	unsigned long long rx_parity_abort_cnt;
	unsigned long long rx_rda_fail_cnt;
	unsigned long long rx_unkn_prot_cnt;
	unsigned long long rx_fcs_err_cnt;
	unsigned long long rx_buf_size_err_cnt;
	unsigned long long rx_rxd_corrupt_cnt;
	unsigned long long rx_unkn_err_cnt;

	/* Error/alarm statistics*/
	unsigned long long tda_err_cnt;
	unsigned long long pfc_err_cnt;
	unsigned long long pcc_err_cnt;
	unsigned long long tti_err_cnt;
	unsigned long long lso_err_cnt;
	unsigned long long tpa_err_cnt;
	unsigned long long sm_err_cnt;
	unsigned long long mac_tmac_err_cnt;
	unsigned long long mac_rmac_err_cnt;
	unsigned long long xgxs_txgxs_err_cnt;
	unsigned long long xgxs_rxgxs_err_cnt;
	unsigned long long rc_err_cnt;
	unsigned long long prc_pcix_err_cnt;
	unsigned long long rpa_err_cnt;
	unsigned long long rda_err_cnt;
	unsigned long long rti_err_cnt;
	unsigned long long mc_err_cnt;

};

/* Xpak releated alarm and warnings */
struct xpakStat {
	u64 alarm_transceiver_temp_high;
	u64 alarm_transceiver_temp_low;
	u64 alarm_laser_bias_current_high;
	u64 alarm_laser_bias_current_low;
	u64 alarm_laser_output_power_high;
	u64 alarm_laser_output_power_low;
	u64 warn_transceiver_temp_high;
	u64 warn_transceiver_temp_low;
	u64 warn_laser_bias_current_high;
	u64 warn_laser_bias_current_low;
	u64 warn_laser_output_power_high;
	u64 warn_laser_output_power_low;
	u64 xpak_regs_stat;
	u32 xpak_timer_count;
};


/* The statistics block of Xena */
struct stat_block {
/* Tx MAC statistics counters. */
	__le32 tmac_data_octets;
	__le32 tmac_frms;
	__le64 tmac_drop_frms;
	__le32 tmac_bcst_frms;
	__le32 tmac_mcst_frms;
	__le64 tmac_pause_ctrl_frms;
	__le32 tmac_ucst_frms;
	__le32 tmac_ttl_octets;
	__le32 tmac_any_err_frms;
	__le32 tmac_nucst_frms;
	__le64 tmac_ttl_less_fb_octets;
	__le64 tmac_vld_ip_octets;
	__le32 tmac_drop_ip;
	__le32 tmac_vld_ip;
	__le32 tmac_rst_tcp;
	__le32 tmac_icmp;
	__le64 tmac_tcp;
	__le32 reserved_0;
	__le32 tmac_udp;

/* Rx MAC Statistics counters. */
	__le32 rmac_data_octets;
	__le32 rmac_vld_frms;
	__le64 rmac_fcs_err_frms;
	__le64 rmac_drop_frms;
	__le32 rmac_vld_bcst_frms;
	__le32 rmac_vld_mcst_frms;
	__le32 rmac_out_rng_len_err_frms;
	__le32 rmac_in_rng_len_err_frms;
	__le64 rmac_long_frms;
	__le64 rmac_pause_ctrl_frms;
	__le64 rmac_unsup_ctrl_frms;
	__le32 rmac_accepted_ucst_frms;
	__le32 rmac_ttl_octets;
	__le32 rmac_discarded_frms;
	__le32 rmac_accepted_nucst_frms;
	__le32 reserved_1;
	__le32 rmac_drop_events;
	__le64 rmac_ttl_less_fb_octets;
	__le64 rmac_ttl_frms;
	__le64 reserved_2;
	__le32 rmac_usized_frms;
	__le32 reserved_3;
	__le32 rmac_frag_frms;
	__le32 rmac_osized_frms;
	__le32 reserved_4;
	__le32 rmac_jabber_frms;
	__le64 rmac_ttl_64_frms;
	__le64 rmac_ttl_65_127_frms;
	__le64 reserved_5;
	__le64 rmac_ttl_128_255_frms;
	__le64 rmac_ttl_256_511_frms;
	__le64 reserved_6;
	__le64 rmac_ttl_512_1023_frms;
	__le64 rmac_ttl_1024_1518_frms;
	__le32 rmac_ip;
	__le32 reserved_7;
	__le64 rmac_ip_octets;
	__le32 rmac_drop_ip;
	__le32 rmac_hdr_err_ip;
	__le32 reserved_8;
	__le32 rmac_icmp;
	__le64 rmac_tcp;
	__le32 rmac_err_drp_udp;
	__le32 rmac_udp;
	__le64 rmac_xgmii_err_sym;
	__le64 rmac_frms_q0;
	__le64 rmac_frms_q1;
	__le64 rmac_frms_q2;
	__le64 rmac_frms_q3;
	__le64 rmac_frms_q4;
	__le64 rmac_frms_q5;
	__le64 rmac_frms_q6;
	__le64 rmac_frms_q7;
	__le16 rmac_full_q3;
	__le16 rmac_full_q2;
	__le16 rmac_full_q1;
	__le16 rmac_full_q0;
	__le16 rmac_full_q7;
	__le16 rmac_full_q6;
	__le16 rmac_full_q5;
	__le16 rmac_full_q4;
	__le32 reserved_9;
	__le32 rmac_pause_cnt;
	__le64 rmac_xgmii_data_err_cnt;
	__le64 rmac_xgmii_ctrl_err_cnt;
	__le32 rmac_err_tcp;
	__le32 rmac_accepted_ip;

/* PCI/PCI-X Read transaction statistics. */
	__le32 new_rd_req_cnt;
	__le32 rd_req_cnt;
	__le32 rd_rtry_cnt;
	__le32 new_rd_req_rtry_cnt;

/* PCI/PCI-X Write/Read transaction statistics. */
	__le32 wr_req_cnt;
	__le32 wr_rtry_rd_ack_cnt;
	__le32 new_wr_req_rtry_cnt;
	__le32 new_wr_req_cnt;
	__le32 wr_disc_cnt;
	__le32 wr_rtry_cnt;

/*	PCI/PCI-X Write / DMA Transaction statistics. */
	__le32 txp_wr_cnt;
	__le32 rd_rtry_wr_ack_cnt;
	__le32 txd_wr_cnt;
	__le32 txd_rd_cnt;
	__le32 rxd_wr_cnt;
	__le32 rxd_rd_cnt;
	__le32 rxf_wr_cnt;
	__le32 txf_rd_cnt;

/* Tx MAC statistics overflow counters. */
	__le32 tmac_data_octets_oflow;
	__le32 tmac_frms_oflow;
	__le32 tmac_bcst_frms_oflow;
	__le32 tmac_mcst_frms_oflow;
	__le32 tmac_ucst_frms_oflow;
	__le32 tmac_ttl_octets_oflow;
	__le32 tmac_any_err_frms_oflow;
	__le32 tmac_nucst_frms_oflow;
	__le64 tmac_vlan_frms;
	__le32 tmac_drop_ip_oflow;
	__le32 tmac_vld_ip_oflow;
	__le32 tmac_rst_tcp_oflow;
	__le32 tmac_icmp_oflow;
	__le32 tpa_unknown_protocol;
	__le32 tmac_udp_oflow;
	__le32 reserved_10;
	__le32 tpa_parse_failure;

/* Rx MAC Statistics overflow counters. */
	__le32 rmac_data_octets_oflow;
	__le32 rmac_vld_frms_oflow;
	__le32 rmac_vld_bcst_frms_oflow;
	__le32 rmac_vld_mcst_frms_oflow;
	__le32 rmac_accepted_ucst_frms_oflow;
	__le32 rmac_ttl_octets_oflow;
	__le32 rmac_discarded_frms_oflow;
	__le32 rmac_accepted_nucst_frms_oflow;
	__le32 rmac_usized_frms_oflow;
	__le32 rmac_drop_events_oflow;
	__le32 rmac_frag_frms_oflow;
	__le32 rmac_osized_frms_oflow;
	__le32 rmac_ip_oflow;
	__le32 rmac_jabber_frms_oflow;
	__le32 rmac_icmp_oflow;
	__le32 rmac_drop_ip_oflow;
	__le32 rmac_err_drp_udp_oflow;
	__le32 rmac_udp_oflow;
	__le32 reserved_11;
	__le32 rmac_pause_cnt_oflow;
	__le64 rmac_ttl_1519_4095_frms;
	__le64 rmac_ttl_4096_8191_frms;
	__le64 rmac_ttl_8192_max_frms;
	__le64 rmac_ttl_gt_max_frms;
	__le64 rmac_osized_alt_frms;
	__le64 rmac_jabber_alt_frms;
	__le64 rmac_gt_max_alt_frms;
	__le64 rmac_vlan_frms;
	__le32 rmac_len_discard;
	__le32 rmac_fcs_discard;
	__le32 rmac_pf_discard;
	__le32 rmac_da_discard;
	__le32 rmac_red_discard;
	__le32 rmac_rts_discard;
	__le32 reserved_12;
	__le32 rmac_ingm_full_discard;
	__le32 reserved_13;
	__le32 rmac_accepted_ip_oflow;
	__le32 reserved_14;
	__le32 link_fault_cnt;
	u8  buffer[20];
	struct swStat sw_stat;
	struct xpakStat xpak_stat;
};

/* Default value for 'vlan_strip_tag' configuration parameter */
#define NO_STRIP_IN_PROMISC 2

/*
 * Structures representing different init time configuration
 * parameters of the NIC.
 */

#define MAX_TX_FIFOS 8
#define MAX_RX_RINGS 8

#define FIFO_DEFAULT_NUM	5
#define FIFO_UDP_MAX_NUM			2 /* 0 - even, 1 -odd ports */
#define FIFO_OTHER_MAX_NUM			1


#define MAX_RX_DESC_1  (MAX_RX_RINGS * MAX_RX_BLOCKS_PER_RING * 127 )
#define MAX_RX_DESC_2  (MAX_RX_RINGS * MAX_RX_BLOCKS_PER_RING * 85 )
#define MAX_RX_DESC_3  (MAX_RX_RINGS * MAX_RX_BLOCKS_PER_RING * 85 )
#define MAX_TX_DESC    (MAX_AVAILABLE_TXDS)

/* FIFO mappings for all possible number of fifos configured */
static int fifo_map[][MAX_TX_FIFOS] = {
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 1, 1, 1, 1},
	{0, 0, 0, 1, 1, 1, 2, 2},
	{0, 0, 1, 1, 2, 2, 3, 3},
	{0, 0, 1, 1, 2, 2, 3, 4},
	{0, 0, 1, 1, 2, 3, 4, 5},
	{0, 0, 1, 2, 3, 4, 5, 6},
	{0, 1, 2, 3, 4, 5, 6, 7},
};

static u16 fifo_selector[MAX_TX_FIFOS] = {0, 1, 3, 3, 7, 7, 7, 7};

/* Maintains Per FIFO related information. */
struct tx_fifo_config {
#define	MAX_AVAILABLE_TXDS	8192
	u32 fifo_len;		/* specifies len of FIFO upto 8192, ie no of TxDLs */
/* Priority definition */
#define TX_FIFO_PRI_0               0	/*Highest */
#define TX_FIFO_PRI_1               1
#define TX_FIFO_PRI_2               2
#define TX_FIFO_PRI_3               3
#define TX_FIFO_PRI_4               4
#define TX_FIFO_PRI_5               5
#define TX_FIFO_PRI_6               6
#define TX_FIFO_PRI_7               7	/*lowest */
	u8 fifo_priority;	/* specifies pointer level for FIFO */
	/* user should not set twos fifos with same pri */
	u8 f_no_snoop;
#define NO_SNOOP_TXD                0x01
#define NO_SNOOP_TXD_BUFFER          0x02
};


/* Maintains per Ring related information */
struct rx_ring_config {
	u32 num_rxd;		/*No of RxDs per Rx Ring */
#define RX_RING_PRI_0               0	/* highest */
#define RX_RING_PRI_1               1
#define RX_RING_PRI_2               2
#define RX_RING_PRI_3               3
#define RX_RING_PRI_4               4
#define RX_RING_PRI_5               5
#define RX_RING_PRI_6               6
#define RX_RING_PRI_7               7	/* lowest */

	u8 ring_priority;	/*Specifies service priority of ring */
	/* OSM should not set any two rings with same priority */
	u8 ring_org;		/*Organization of ring */
#define RING_ORG_BUFF1		0x01
#define RX_RING_ORG_BUFF3	0x03
#define RX_RING_ORG_BUFF5	0x05

	u8 f_no_snoop;
#define NO_SNOOP_RXD                0x01
#define NO_SNOOP_RXD_BUFFER         0x02
};

/* This structure provides contains values of the tunable parameters
 * of the H/W
 */
struct config_param {
/* Tx Side */
	u32 tx_fifo_num;	/*Number of Tx FIFOs */

	/* 0-No steering, 1-Priority steering, 2-Default fifo map */
#define	NO_STEERING				0
#define	TX_PRIORITY_STEERING			0x1
#define TX_DEFAULT_STEERING 			0x2
	u8 tx_steering_type;

	u8 fifo_mapping[MAX_TX_FIFOS];
	struct tx_fifo_config tx_cfg[MAX_TX_FIFOS];	/*Per-Tx FIFO config */
	u32 max_txds;		/*Max no. of Tx buffer descriptor per TxDL */
	u64 tx_intr_type;
#define INTA	0
#define MSI_X	2
	u8 intr_type;
	u8 napi;

	/* Specifies if Tx Intr is UTILZ or PER_LIST type. */

/* Rx Side */
	u32 rx_ring_num;	/*Number of receive rings */
#define MAX_RX_BLOCKS_PER_RING  150

	struct rx_ring_config rx_cfg[MAX_RX_RINGS];	/*Per-Rx Ring config */

#define HEADER_ETHERNET_II_802_3_SIZE 14
#define HEADER_802_2_SIZE              3
#define HEADER_SNAP_SIZE               5
#define HEADER_VLAN_SIZE               4

#define MIN_MTU                       46
#define MAX_PYLD                    1500
#define MAX_MTU                     (MAX_PYLD+18)
#define MAX_MTU_VLAN                (MAX_PYLD+22)
#define MAX_PYLD_JUMBO              9600
#define MAX_MTU_JUMBO               (MAX_PYLD_JUMBO+18)
#define MAX_MTU_JUMBO_VLAN          (MAX_PYLD_JUMBO+22)
	u16 bus_speed;
	int max_mc_addr;	/* xena=64 herc=256 */
	int max_mac_addr;	/* xena=16 herc=64 */
	int mc_start_offset;	/* xena=16 herc=64 */
	u8 multiq;
};

/* Structure representing MAC Addrs */
struct mac_addr {
	u8 mac_addr[ETH_ALEN];
};

/* Structure that represent every FIFO element in the BAR1
 * Address location.
 */
struct TxFIFO_element {
	u64 TxDL_Pointer;

	u64 List_Control;
#define TX_FIFO_LAST_TXD_NUM( val)     vBIT(val,0,8)
#define TX_FIFO_FIRST_LIST             s2BIT(14)
#define TX_FIFO_LAST_LIST              s2BIT(15)
#define TX_FIFO_FIRSTNLAST_LIST        vBIT(3,14,2)
#define TX_FIFO_SPECIAL_FUNC           s2BIT(23)
#define TX_FIFO_DS_NO_SNOOP            s2BIT(31)
#define TX_FIFO_BUFF_NO_SNOOP          s2BIT(30)
};

/* Tx descriptor structure */
struct TxD {
	u64 Control_1;
/* bit mask */
#define TXD_LIST_OWN_XENA       s2BIT(7)
#define TXD_T_CODE              (s2BIT(12)|s2BIT(13)|s2BIT(14)|s2BIT(15))
#define TXD_T_CODE_OK(val)      (|(val & TXD_T_CODE))
#define GET_TXD_T_CODE(val)     ((val & TXD_T_CODE)<<12)
#define TXD_GATHER_CODE         (s2BIT(22) | s2BIT(23))
#define TXD_GATHER_CODE_FIRST   s2BIT(22)
#define TXD_GATHER_CODE_LAST    s2BIT(23)
#define TXD_TCP_LSO_EN          s2BIT(30)
#define TXD_UDP_COF_EN          s2BIT(31)
#define TXD_UFO_EN		s2BIT(31) | s2BIT(30)
#define TXD_TCP_LSO_MSS(val)    vBIT(val,34,14)
#define TXD_UFO_MSS(val)	vBIT(val,34,14)
#define TXD_BUFFER0_SIZE(val)   vBIT(val,48,16)

	u64 Control_2;
#define TXD_TX_CKO_CONTROL      (s2BIT(5)|s2BIT(6)|s2BIT(7))
#define TXD_TX_CKO_IPV4_EN      s2BIT(5)
#define TXD_TX_CKO_TCP_EN       s2BIT(6)
#define TXD_TX_CKO_UDP_EN       s2BIT(7)
#define TXD_VLAN_ENABLE         s2BIT(15)
#define TXD_VLAN_TAG(val)       vBIT(val,16,16)
#define TXD_INT_NUMBER(val)     vBIT(val,34,6)
#define TXD_INT_TYPE_PER_LIST   s2BIT(47)
#define TXD_INT_TYPE_UTILZ      s2BIT(46)
#define TXD_SET_MARKER         vBIT(0x6,0,4)

	u64 Buffer_Pointer;
	u64 Host_Control;	/* reserved for host */
};

/* Structure to hold the phy and virt addr of every TxDL. */
struct list_info_hold {
	dma_addr_t list_phy_addr;
	void *list_virt_addr;
};

/* Rx descriptor structure for 1 buffer mode */
struct RxD_t {
	u64 Host_Control;	/* reserved for host */
	u64 Control_1;
#define RXD_OWN_XENA            s2BIT(7)
#define RXD_T_CODE              (s2BIT(12)|s2BIT(13)|s2BIT(14)|s2BIT(15))
#define RXD_FRAME_PROTO         vBIT(0xFFFF,24,8)
#define RXD_FRAME_VLAN_TAG      s2BIT(24)
#define RXD_FRAME_PROTO_IPV4    s2BIT(27)
#define RXD_FRAME_PROTO_IPV6    s2BIT(28)
#define RXD_FRAME_IP_FRAG	s2BIT(29)
#define RXD_FRAME_PROTO_TCP     s2BIT(30)
#define RXD_FRAME_PROTO_UDP     s2BIT(31)
#define TCP_OR_UDP_FRAME        (RXD_FRAME_PROTO_TCP | RXD_FRAME_PROTO_UDP)
#define RXD_GET_L3_CKSUM(val)   ((u16)(val>> 16) & 0xFFFF)
#define RXD_GET_L4_CKSUM(val)   ((u16)(val) & 0xFFFF)

	u64 Control_2;
#define	THE_RXD_MARK		0x3
#define	SET_RXD_MARKER		vBIT(THE_RXD_MARK, 0, 2)
#define	GET_RXD_MARKER(ctrl)	((ctrl & SET_RXD_MARKER) >> 62)

#define MASK_VLAN_TAG           vBIT(0xFFFF,48,16)
#define SET_VLAN_TAG(val)       vBIT(val,48,16)
#define SET_NUM_TAG(val)       vBIT(val,16,32)


};
/* Rx descriptor structure for 1 buffer mode */
struct RxD1 {
	struct RxD_t h;

#define MASK_BUFFER0_SIZE_1       vBIT(0x3FFF,2,14)
#define SET_BUFFER0_SIZE_1(val)   vBIT(val,2,14)
#define RXD_GET_BUFFER0_SIZE_1(_Control_2) \
	(u16)((_Control_2 & MASK_BUFFER0_SIZE_1) >> 48)
	u64 Buffer0_ptr;
};
/* Rx descriptor structure for 3 or 2 buffer mode */

struct RxD3 {
	struct RxD_t h;

#define MASK_BUFFER0_SIZE_3       vBIT(0xFF,2,14)
#define MASK_BUFFER1_SIZE_3       vBIT(0xFFFF,16,16)
#define MASK_BUFFER2_SIZE_3       vBIT(0xFFFF,32,16)
#define SET_BUFFER0_SIZE_3(val)   vBIT(val,8,8)
#define SET_BUFFER1_SIZE_3(val)   vBIT(val,16,16)
#define SET_BUFFER2_SIZE_3(val)   vBIT(val,32,16)
#define RXD_GET_BUFFER0_SIZE_3(Control_2) \
	(u8)((Control_2 & MASK_BUFFER0_SIZE_3) >> 48)
#define RXD_GET_BUFFER1_SIZE_3(Control_2) \
	(u16)((Control_2 & MASK_BUFFER1_SIZE_3) >> 32)
#define RXD_GET_BUFFER2_SIZE_3(Control_2) \
	(u16)((Control_2 & MASK_BUFFER2_SIZE_3) >> 16)
#define BUF0_LEN	40
#define BUF1_LEN	1

	u64 Buffer0_ptr;
	u64 Buffer1_ptr;
	u64 Buffer2_ptr;
};


/* Structure that represents the Rx descriptor block which contains
 * 128 Rx descriptors.
 */
struct RxD_block {
#define MAX_RXDS_PER_BLOCK_1            127
	struct RxD1 rxd[MAX_RXDS_PER_BLOCK_1];

	u64 reserved_0;
#define END_OF_BLOCK    0xFEFFFFFFFFFFFFFFULL
	u64 reserved_1;		/* 0xFEFFFFFFFFFFFFFF to mark last
				 * Rxd in this blk */
	u64 reserved_2_pNext_RxD_block;	/* Logical ptr to next */
	u64 pNext_RxD_Blk_physical;	/* Buff0_ptr.In a 32 bit arch
					 * the upper 32 bits should
					 * be 0 */
};

#define SIZE_OF_BLOCK	4096

#define RXD_MODE_1	0 /* One Buffer mode */
#define RXD_MODE_3B	1 /* Two Buffer mode */

/* Structure to hold virtual addresses of Buf0 and Buf1 in
 * 2buf mode. */
struct buffAdd {
	void *ba_0_org;
	void *ba_1_org;
	void *ba_0;
	void *ba_1;
};

/* Structure which stores all the MAC control parameters */

/* This structure stores the offset of the RxD in the ring
 * from which the Rx Interrupt processor can start picking
 * up the RxDs for processing.
 */
struct rx_curr_get_info {
	u32 block_index;
	u32 offset;
	u32 ring_len;
};

struct rx_curr_put_info {
	u32 block_index;
	u32 offset;
	u32 ring_len;
};

/* This structure stores the offset of the TxDl in the FIFO
 * from which the Tx Interrupt processor can start picking
 * up the TxDLs for send complete interrupt processing.
 */
struct tx_curr_get_info {
	u32 offset;
	u32 fifo_len;
};

struct tx_curr_put_info {
	u32 offset;
	u32 fifo_len;
};

struct rxd_info {
	void *virt_addr;
	dma_addr_t dma_addr;
};

/* Structure that holds the Phy and virt addresses of the Blocks */
struct rx_block_info {
	void *block_virt_addr;
	dma_addr_t block_dma_addr;
	struct rxd_info *rxds;
};

/* Ring specific structure */
struct ring_info {
	/* The ring number */
	int ring_no;

	/*
	 *  Place holders for the virtual and physical addresses of
	 *  all the Rx Blocks
	 */
	struct rx_block_info rx_blocks[MAX_RX_BLOCKS_PER_RING];
	int block_count;
	int pkt_cnt;

	/*
	 * Put pointer info which indictes which RxD has to be replenished
	 * with a new buffer.
	 */
	struct rx_curr_put_info rx_curr_put_info;

	/*
	 * Get pointer info which indictes which is the last RxD that was
	 * processed by the driver.
	 */
	struct rx_curr_get_info rx_curr_get_info;

	/* Buffer Address store. */
	struct buffAdd **ba;
	struct s2io_nic *nic;
};

/* Fifo specific structure */
struct fifo_info {
	/* FIFO number */
	int fifo_no;

	/* Maximum TxDs per TxDL */
	int max_txds;

	/* Place holder of all the TX List's Phy and Virt addresses. */
	struct list_info_hold *list_info;

	/*
	 * Current offset within the tx FIFO where driver would write
	 * new Tx frame
	 */
	struct tx_curr_put_info tx_curr_put_info;

	/*
	 * Current offset within tx FIFO from where the driver would start freeing
	 * the buffers
	 */
	struct tx_curr_get_info tx_curr_get_info;
#define FIFO_QUEUE_START 0
#define FIFO_QUEUE_STOP 1
	int queue_state;

	/* copy of sp->dev pointer */
	struct net_device *dev;

	/* copy of multiq status */
	u8 multiq;

	/* Per fifo lock */
	spinlock_t tx_lock;

	/* Per fifo UFO in band structure */
	u64 *ufo_in_band_v;

	struct s2io_nic *nic;
} ____cacheline_aligned;

/* Information related to the Tx and Rx FIFOs and Rings of Xena
 * is maintained in this structure.
 */
struct mac_info {
/* tx side stuff */
	/* logical pointer of start of each Tx FIFO */
	struct TxFIFO_element __iomem *tx_FIFO_start[MAX_TX_FIFOS];

	/* Fifo specific structure */
	struct fifo_info fifos[MAX_TX_FIFOS];

	/* Save virtual address of TxD page with zero DMA addr(if any) */
	void *zerodma_virt_addr;

/* rx side stuff */
	/* Ring specific structure */
	struct ring_info rings[MAX_RX_RINGS];

	u16 rmac_pause_time;
	u16 mc_pause_threshold_q0q3;
	u16 mc_pause_threshold_q4q7;

	void *stats_mem;	/* orignal pointer to allocated mem */
	dma_addr_t stats_mem_phy;	/* Physical address of the stat block */
	u32 stats_mem_sz;
	struct stat_block *stats_info;	/* Logical address of the stat block */
};

/* structure representing the user defined MAC addresses */
struct usr_addr {
	char addr[ETH_ALEN];
	int usage_cnt;
};

/* Default Tunable parameters of the NIC. */
#define DEFAULT_FIFO_0_LEN 4096
#define DEFAULT_FIFO_1_7_LEN 512
#define SMALL_BLK_CNT	30
#define LARGE_BLK_CNT	100

/*
 * Structure to keep track of the MSI-X vectors and the corresponding
 * argument registered against each vector
 */
#define MAX_REQUESTED_MSI_X	17
struct s2io_msix_entry
{
	u16 vector;
	u16 entry;
	void *arg;

	u8 type;
#define	MSIX_FIFO_TYPE	1
#define	MSIX_RING_TYPE	2

	u8 in_use;
#define MSIX_REGISTERED_SUCCESS	0xAA
};

struct msix_info_st {
	u64 addr;
	u64 data;
};

/* Data structure to represent a LRO session */
struct lro {
	struct sk_buff	*parent;
	struct sk_buff  *last_frag;
	u8		*l2h;
	struct iphdr	*iph;
	struct tcphdr	*tcph;
	u32		tcp_next_seq;
	__be32		tcp_ack;
	int		total_len;
	int		frags_len;
	int		sg_num;
	int		in_use;
	__be16		window;
	u16             vlan_tag;
	u32		cur_tsval;
	__be32		cur_tsecr;
	u8		saw_ts;
} ____cacheline_aligned;

/* These flags represent the devices temporary state */
enum s2io_device_state_t
{
	__S2IO_STATE_LINK_TASK=0,
	__S2IO_STATE_CARD_UP
};

/* Structure representing one instance of the NIC */
struct s2io_nic {
	int rxd_mode;
	/*
	 * Count of packets to be processed in a given iteration, it will be indicated
	 * by the quota field of the device structure when NAPI is enabled.
	 */
	int pkts_to_process;
	struct net_device *dev;
	struct napi_struct napi;
	struct mac_info mac_control;
	struct config_param config;
	struct pci_dev *pdev;
	void __iomem *bar0;
	void __iomem *bar1;
#define MAX_MAC_SUPPORTED   16
#define MAX_SUPPORTED_MULTICASTS MAX_MAC_SUPPORTED

	struct mac_addr def_mac_addr[256];

	struct net_device_stats stats;
	int high_dma_flag;
	int device_enabled_once;

	char name[60];

	/* Timer that handles I/O errors/exceptions */
	struct timer_list alarm_timer;

	/* Space to back up the PCI config space */
	u32 config_space[256 / sizeof(u32)];

	atomic_t rx_bufs_left[MAX_RX_RINGS];

#define PROMISC     1
#define ALL_MULTI   2

#define MAX_ADDRS_SUPPORTED 64
	u16 usr_addr_count;
	u16 mc_addr_count;
	struct usr_addr usr_addrs[256];

	u16 m_cast_flg;
	u16 all_multi_pos;
	u16 promisc_flg;

	/*  Id timer, used to blink NIC to physically identify NIC. */
	struct timer_list id_timer;

	/*  Restart timer, used to restart NIC if the device is stuck and
	 *  a schedule task that will set the correct Link state once the
	 *  NIC's PHY has stabilized after a state change.
	 */
	struct work_struct rst_timer_task;
	struct work_struct set_link_task;

	/* Flag that can be used to turn on or turn off the Rx checksum
	 * offload feature.
	 */
	int rx_csum;

	/* Below variables are used for fifo selection to transmit a packet */
	u16 fifo_selector[MAX_TX_FIFOS];

	/* Total fifos for tcp packets */
	u8 total_tcp_fifos;

	/*
	* Beginning index of udp for udp packets
	* Value will be equal to
	* (tx_fifo_num - FIFO_UDP_MAX_NUM - FIFO_OTHER_MAX_NUM)
	*/
	u8 udp_fifo_idx;

	u8 total_udp_fifos;

	/*
	 * Beginning index of fifo for all other packets
	 * Value will be equal to (tx_fifo_num - FIFO_OTHER_MAX_NUM)
	*/
	u8 other_fifo_idx;

	/*  after blink, the adapter must be restored with original
	 *  values.
	 */
	u64 adapt_ctrl_org;

	/* Last known link state. */
	u16 last_link_state;
#define	LINK_DOWN	1
#define	LINK_UP		2

	int task_flag;
	unsigned long long start_time;
	struct vlan_group *vlgrp;
#define MSIX_FLG                0xA5
	struct msix_entry *entries;
	int msi_detected;
	wait_queue_head_t msi_wait;
	struct s2io_msix_entry *s2io_entries;
	char desc[MAX_REQUESTED_MSI_X][25];

	int avail_msix_vectors; /* No. of MSI-X vectors granted by system */

	struct msix_info_st msix_info[0x3f];

#define XFRAME_I_DEVICE		1
#define XFRAME_II_DEVICE	2
	u8 device_type;

#define MAX_LRO_SESSIONS	32
	struct lro lro0_n[MAX_LRO_SESSIONS];
	unsigned long	clubbed_frms_cnt;
	unsigned long	sending_both;
	u8		lro;
	u16		lro_max_aggr_per_sess;
	volatile unsigned long state;
	u64		general_int_mask;
#define VPD_STRING_LEN 80
	u8  product_name[VPD_STRING_LEN];
	u8  serial_num[VPD_STRING_LEN];
};

#define RESET_ERROR 1;
#define CMD_ERROR   2;

/*  OS related system calls */
#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	u64 ret = 0;
	ret = readl(addr + 4);
	ret <<= 32;
	ret |= readl(addr);

	return ret;
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	writel((u32) (val), addr);
	writel((u32) (val >> 32), (addr + 4));
}
#endif

/*
 * Some registers have to be written in a particular order to
 * expect correct hardware operation. The macro SPECIAL_REG_WRITE
 * is used to perform such ordered writes. Defines UF (Upper First)
 * and LF (Lower First) will be used to specify the required write order.
 */
#define UF	1
#define LF	2
static inline void SPECIAL_REG_WRITE(u64 val, void __iomem *addr, int order)
{
	u32 ret;

	if (order == LF) {
		writel((u32) (val), addr);
		ret = readl(addr);
		writel((u32) (val >> 32), (addr + 4));
		ret = readl(addr + 4);
	} else {
		writel((u32) (val >> 32), (addr + 4));
		ret = readl(addr + 4);
		writel((u32) (val), addr);
		ret = readl(addr);
	}
}

/*  Interrupt related values of Xena */

#define ENABLE_INTRS    1
#define DISABLE_INTRS   2

/*  Highest level interrupt blocks */
#define TX_PIC_INTR     (0x0001<<0)
#define TX_DMA_INTR     (0x0001<<1)
#define TX_MAC_INTR     (0x0001<<2)
#define TX_XGXS_INTR    (0x0001<<3)
#define TX_TRAFFIC_INTR (0x0001<<4)
#define RX_PIC_INTR     (0x0001<<5)
#define RX_DMA_INTR     (0x0001<<6)
#define RX_MAC_INTR     (0x0001<<7)
#define RX_XGXS_INTR    (0x0001<<8)
#define RX_TRAFFIC_INTR (0x0001<<9)
#define MC_INTR         (0x0001<<10)
#define ENA_ALL_INTRS    (   TX_PIC_INTR     | \
                            TX_DMA_INTR     | \
                            TX_MAC_INTR     | \
                            TX_XGXS_INTR    | \
                            TX_TRAFFIC_INTR | \
                            RX_PIC_INTR     | \
                            RX_DMA_INTR     | \
                            RX_MAC_INTR     | \
                            RX_XGXS_INTR    | \
                            RX_TRAFFIC_INTR | \
                            MC_INTR )

/*  Interrupt masks for the general interrupt mask register */
#define DISABLE_ALL_INTRS   0xFFFFFFFFFFFFFFFFULL

#define TXPIC_INT_M         s2BIT(0)
#define TXDMA_INT_M         s2BIT(1)
#define TXMAC_INT_M         s2BIT(2)
#define TXXGXS_INT_M        s2BIT(3)
#define TXTRAFFIC_INT_M     s2BIT(8)
#define PIC_RX_INT_M        s2BIT(32)
#define RXDMA_INT_M         s2BIT(33)
#define RXMAC_INT_M         s2BIT(34)
#define MC_INT_M            s2BIT(35)
#define RXXGXS_INT_M        s2BIT(36)
#define RXTRAFFIC_INT_M     s2BIT(40)

/*  PIC level Interrupts TODO*/

/*  DMA level Inressupts */
#define TXDMA_PFC_INT_M     s2BIT(0)
#define TXDMA_PCC_INT_M     s2BIT(2)

/*  PFC block interrupts */
#define PFC_MISC_ERR_1      s2BIT(0)	/* Interrupt to indicate FIFO full */

/* PCC block interrupts. */
#define	PCC_FB_ECC_ERR	   vBIT(0xff, 16, 8)	/* Interrupt to indicate
						   PCC_FB_ECC Error. */

#define RXD_GET_VLAN_TAG(Control_2) (u16)(Control_2 & MASK_VLAN_TAG)
/*
 * Prototype declaration.
 */
static int __devinit s2io_init_nic(struct pci_dev *pdev,
				   const struct pci_device_id *pre);
static void __devexit s2io_rem_nic(struct pci_dev *pdev);
static int init_shared_mem(struct s2io_nic *sp);
static void free_shared_mem(struct s2io_nic *sp);
static int init_nic(struct s2io_nic *nic);
static void rx_intr_handler(struct ring_info *ring_data);
static void tx_intr_handler(struct fifo_info *fifo_data);
static void s2io_handle_errors(void * dev_id);

static int s2io_starter(void);
static void s2io_closer(void);
static void s2io_tx_watchdog(struct net_device *dev);
static void s2io_set_multicast(struct net_device *dev);
static int rx_osm_handler(struct ring_info *ring_data, struct RxD_t * rxdp);
static void s2io_link(struct s2io_nic * sp, int link);
static void s2io_reset(struct s2io_nic * sp);
static int s2io_poll(struct napi_struct *napi, int budget);
static void s2io_init_pci(struct s2io_nic * sp);
static int do_s2io_prog_unicast(struct net_device *dev, u8 *addr);
static void s2io_alarm_handle(unsigned long data);
static irqreturn_t
s2io_msix_ring_handle(int irq, void *dev_id);
static irqreturn_t
s2io_msix_fifo_handle(int irq, void *dev_id);
static irqreturn_t s2io_isr(int irq, void *dev_id);
static int verify_xena_quiescence(struct s2io_nic *sp);
static const struct ethtool_ops netdev_ethtool_ops;
static void s2io_set_link(struct work_struct *work);
static int s2io_set_swapper(struct s2io_nic * sp);
static void s2io_card_down(struct s2io_nic *nic);
static int s2io_card_up(struct s2io_nic *nic);
static int wait_for_cmd_complete(void __iomem *addr, u64 busy_bit,
					int bit_state);
static int s2io_add_isr(struct s2io_nic * sp);
static void s2io_rem_isr(struct s2io_nic * sp);

static void restore_xmsi_data(struct s2io_nic *nic);
static void do_s2io_store_unicast_mc(struct s2io_nic *sp);
static void do_s2io_restore_unicast_mc(struct s2io_nic *sp);
static u64 do_s2io_read_unicast_mc(struct s2io_nic *sp, int offset);
static int do_s2io_add_mc(struct s2io_nic *sp, u8 *addr);
static int do_s2io_add_mac(struct s2io_nic *sp, u64 addr, int offset);
static int do_s2io_delete_unicast_mc(struct s2io_nic *sp, u64 addr);

static int
s2io_club_tcp_session(u8 *buffer, u8 **tcp, u32 *tcp_len, struct lro **lro,
		      struct RxD_t *rxdp, struct s2io_nic *sp);
static void clear_lro_session(struct lro *lro);
static void queue_rx_frame(struct sk_buff *skb, u16 vlan_tag);
static void update_L3L4_header(struct s2io_nic *sp, struct lro *lro);
static void lro_append_pkt(struct s2io_nic *sp, struct lro *lro,
			   struct sk_buff *skb, u32 tcp_len);
static int rts_ds_steer(struct s2io_nic *nic, u8 ds_codepoint, u8 ring);

static pci_ers_result_t s2io_io_error_detected(struct pci_dev *pdev,
			                      pci_channel_state_t state);
static pci_ers_result_t s2io_io_slot_reset(struct pci_dev *pdev);
static void s2io_io_resume(struct pci_dev *pdev);

#define s2io_tcp_mss(skb) skb_shinfo(skb)->gso_size
#define s2io_udp_mss(skb) skb_shinfo(skb)->gso_size
#define s2io_offload_type(skb) skb_shinfo(skb)->gso_type

#define S2IO_PARM_INT(X, def_val) \
	static unsigned int X = def_val;\
		module_param(X , uint, 0);

#endif				/* _S2IO_H */
