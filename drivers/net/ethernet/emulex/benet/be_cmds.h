/*
 * Copyright (C) 2005 - 2014 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/*
 * The driver sends configuration and managements command requests to the
 * firmware in the BE. These requests are communicated to the processor
 * using Work Request Blocks (WRBs) submitted to the MCC-WRB ring or via one
 * WRB inside a MAILBOX.
 * The commands are serviced by the ARM processor in the BladeEngine's MPU.
 */

struct be_sge {
	u32 pa_lo;
	u32 pa_hi;
	u32 len;
};

#define MCC_WRB_EMBEDDED_MASK	1 	/* bit 0 of dword 0*/
#define MCC_WRB_SGE_CNT_SHIFT	3	/* bits 3 - 7 of dword 0 */
#define MCC_WRB_SGE_CNT_MASK	0x1F	/* bits 3 - 7 of dword 0 */
struct be_mcc_wrb {
	u32 embedded;		/* dword 0 */
	u32 payload_length;	/* dword 1 */
	u32 tag0;		/* dword 2 */
	u32 tag1;		/* dword 3 */
	u32 rsvd;		/* dword 4 */
	union {
		u8 embedded_payload[236]; /* used by embedded cmds */
		struct be_sge sgl[19];    /* used by non-embedded cmds */
	} payload;
};

#define CQE_FLAGS_VALID_MASK 		(1 << 31)
#define CQE_FLAGS_ASYNC_MASK 		(1 << 30)
#define CQE_FLAGS_COMPLETED_MASK 	(1 << 28)
#define CQE_FLAGS_CONSUMED_MASK 	(1 << 27)

/* Completion Status */
enum {
	MCC_STATUS_SUCCESS = 0,
	MCC_STATUS_FAILED = 1,
	MCC_STATUS_ILLEGAL_REQUEST = 2,
	MCC_STATUS_ILLEGAL_FIELD = 3,
	MCC_STATUS_INSUFFICIENT_BUFFER = 4,
	MCC_STATUS_UNAUTHORIZED_REQUEST = 5,
	MCC_STATUS_NOT_SUPPORTED = 66
};

#define MCC_ADDL_STS_INSUFFICIENT_RESOURCES	0x16

#define CQE_STATUS_COMPL_MASK		0xFFFF
#define CQE_STATUS_COMPL_SHIFT		0	/* bits 0 - 15 */
#define CQE_STATUS_EXTD_MASK		0xFFFF
#define CQE_STATUS_EXTD_SHIFT		16	/* bits 16 - 31 */

struct be_mcc_compl {
	u32 status;		/* dword 0 */
	u32 tag0;		/* dword 1 */
	u32 tag1;		/* dword 2 */
	u32 flags;		/* dword 3 */
};

/* When the async bit of mcc_compl is set, the last 4 bytes of
 * mcc_compl is interpreted as follows:
 */
#define ASYNC_TRAILER_EVENT_CODE_SHIFT	8	/* bits 8 - 15 */
#define ASYNC_TRAILER_EVENT_CODE_MASK	0xFF
#define ASYNC_TRAILER_EVENT_TYPE_SHIFT	16
#define ASYNC_TRAILER_EVENT_TYPE_MASK	0xFF
#define ASYNC_EVENT_CODE_LINK_STATE	0x1
#define ASYNC_EVENT_CODE_GRP_5		0x5
#define ASYNC_EVENT_QOS_SPEED		0x1
#define ASYNC_EVENT_COS_PRIORITY	0x2
#define ASYNC_EVENT_PVID_STATE		0x3
#define ASYNC_EVENT_CODE_QNQ		0x6
#define ASYNC_DEBUG_EVENT_TYPE_QNQ	1

struct be_async_event_trailer {
	u32 code;
};

enum {
	LINK_DOWN	= 0x0,
	LINK_UP		= 0x1
};
#define LINK_STATUS_MASK			0x1
#define LOGICAL_LINK_STATUS_MASK		0x2

/* When the event code of an async trailer is link-state, the mcc_compl
 * must be interpreted as follows
 */
struct be_async_event_link_state {
	u8 physical_port;
	u8 port_link_status;
	u8 port_duplex;
	u8 port_speed;
	u8 port_fault;
	u8 rsvd0[7];
	struct be_async_event_trailer trailer;
} __packed;

/* When the event code of an async trailer is GRP-5 and event_type is QOS_SPEED
 * the mcc_compl must be interpreted as follows
 */
struct be_async_event_grp5_qos_link_speed {
	u8 physical_port;
	u8 rsvd[5];
	u16 qos_link_speed;
	u32 event_tag;
	struct be_async_event_trailer trailer;
} __packed;

/* When the event code of an async trailer is GRP5 and event type is
 * CoS-Priority, the mcc_compl must be interpreted as follows
 */
struct be_async_event_grp5_cos_priority {
	u8 physical_port;
	u8 available_priority_bmap;
	u8 reco_default_priority;
	u8 valid;
	u8 rsvd0;
	u8 event_tag;
	struct be_async_event_trailer trailer;
} __packed;

/* When the event code of an async trailer is GRP5 and event type is
 * PVID state, the mcc_compl must be interpreted as follows
 */
struct be_async_event_grp5_pvid_state {
	u8 enabled;
	u8 rsvd0;
	u16 tag;
	u32 event_tag;
	u32 rsvd1;
	struct be_async_event_trailer trailer;
} __packed;

/* async event indicating outer VLAN tag in QnQ */
struct be_async_event_qnq {
	u8 valid;	/* Indicates if outer VLAN is valid */
	u8 rsvd0;
	u16 vlan_tag;
	u32 event_tag;
	u8 rsvd1[4];
	struct be_async_event_trailer trailer;
} __packed;

struct be_mcc_mailbox {
	struct be_mcc_wrb wrb;
	struct be_mcc_compl compl;
};

#define CMD_SUBSYSTEM_COMMON	0x1
#define CMD_SUBSYSTEM_ETH 	0x3
#define CMD_SUBSYSTEM_LOWLEVEL  0xb

#define OPCODE_COMMON_NTWK_MAC_QUERY			1
#define OPCODE_COMMON_NTWK_MAC_SET			2
#define OPCODE_COMMON_NTWK_MULTICAST_SET		3
#define OPCODE_COMMON_NTWK_VLAN_CONFIG  		4
#define OPCODE_COMMON_NTWK_LINK_STATUS_QUERY		5
#define OPCODE_COMMON_READ_FLASHROM			6
#define OPCODE_COMMON_WRITE_FLASHROM			7
#define OPCODE_COMMON_CQ_CREATE				12
#define OPCODE_COMMON_EQ_CREATE				13
#define OPCODE_COMMON_MCC_CREATE			21
#define OPCODE_COMMON_SET_QOS				28
#define OPCODE_COMMON_MCC_CREATE_EXT			90
#define OPCODE_COMMON_SEEPROM_READ			30
#define OPCODE_COMMON_GET_CNTL_ATTRIBUTES               32
#define OPCODE_COMMON_NTWK_RX_FILTER    		34
#define OPCODE_COMMON_GET_FW_VERSION			35
#define OPCODE_COMMON_SET_FLOW_CONTROL			36
#define OPCODE_COMMON_GET_FLOW_CONTROL			37
#define OPCODE_COMMON_SET_FRAME_SIZE			39
#define OPCODE_COMMON_MODIFY_EQ_DELAY			41
#define OPCODE_COMMON_FIRMWARE_CONFIG			42
#define OPCODE_COMMON_NTWK_INTERFACE_CREATE 		50
#define OPCODE_COMMON_NTWK_INTERFACE_DESTROY 		51
#define OPCODE_COMMON_MCC_DESTROY        		53
#define OPCODE_COMMON_CQ_DESTROY        		54
#define OPCODE_COMMON_EQ_DESTROY        		55
#define OPCODE_COMMON_QUERY_FIRMWARE_CONFIG		58
#define OPCODE_COMMON_NTWK_PMAC_ADD			59
#define OPCODE_COMMON_NTWK_PMAC_DEL			60
#define OPCODE_COMMON_FUNCTION_RESET			61
#define OPCODE_COMMON_MANAGE_FAT			68
#define OPCODE_COMMON_ENABLE_DISABLE_BEACON		69
#define OPCODE_COMMON_GET_BEACON_STATE			70
#define OPCODE_COMMON_READ_TRANSRECV_DATA		73
#define OPCODE_COMMON_GET_PORT_NAME			77
#define OPCODE_COMMON_SET_LOGICAL_LINK_CONFIG		80
#define OPCODE_COMMON_SET_INTERRUPT_ENABLE		89
#define OPCODE_COMMON_SET_FN_PRIVILEGES			100
#define OPCODE_COMMON_GET_PHY_DETAILS			102
#define OPCODE_COMMON_SET_DRIVER_FUNCTION_CAP		103
#define OPCODE_COMMON_GET_CNTL_ADDITIONAL_ATTRIBUTES	121
#define OPCODE_COMMON_GET_EXT_FAT_CAPABILITES		125
#define OPCODE_COMMON_SET_EXT_FAT_CAPABILITES		126
#define OPCODE_COMMON_GET_MAC_LIST			147
#define OPCODE_COMMON_SET_MAC_LIST			148
#define OPCODE_COMMON_GET_HSW_CONFIG			152
#define OPCODE_COMMON_GET_FUNC_CONFIG			160
#define OPCODE_COMMON_GET_PROFILE_CONFIG		164
#define OPCODE_COMMON_SET_PROFILE_CONFIG		165
#define OPCODE_COMMON_GET_ACTIVE_PROFILE		167
#define OPCODE_COMMON_SET_HSW_CONFIG			153
#define OPCODE_COMMON_GET_FN_PRIVILEGES			170
#define OPCODE_COMMON_READ_OBJECT			171
#define OPCODE_COMMON_WRITE_OBJECT			172
#define OPCODE_COMMON_MANAGE_IFACE_FILTERS		193
#define OPCODE_COMMON_GET_IFACE_LIST			194
#define OPCODE_COMMON_ENABLE_DISABLE_VF			196

#define OPCODE_ETH_RSS_CONFIG				1
#define OPCODE_ETH_ACPI_CONFIG				2
#define OPCODE_ETH_PROMISCUOUS				3
#define OPCODE_ETH_GET_STATISTICS			4
#define OPCODE_ETH_TX_CREATE				7
#define OPCODE_ETH_RX_CREATE            		8
#define OPCODE_ETH_TX_DESTROY           		9
#define OPCODE_ETH_RX_DESTROY           		10
#define OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG		12
#define OPCODE_ETH_GET_PPORT_STATS			18

#define OPCODE_LOWLEVEL_HOST_DDR_DMA                    17
#define OPCODE_LOWLEVEL_LOOPBACK_TEST                   18
#define OPCODE_LOWLEVEL_SET_LOOPBACK_MODE		19

struct be_cmd_req_hdr {
	u8 opcode;		/* dword 0 */
	u8 subsystem;		/* dword 0 */
	u8 port_number;		/* dword 0 */
	u8 domain;		/* dword 0 */
	u32 timeout;		/* dword 1 */
	u32 request_length;	/* dword 2 */
	u8 version;		/* dword 3 */
	u8 rsvd[3];		/* dword 3 */
};

#define RESP_HDR_INFO_OPCODE_SHIFT	0	/* bits 0 - 7 */
#define RESP_HDR_INFO_SUBSYS_SHIFT	8 	/* bits 8 - 15 */
struct be_cmd_resp_hdr {
	u8 opcode;		/* dword 0 */
	u8 subsystem;		/* dword 0 */
	u8 rsvd[2];		/* dword 0 */
	u8 status;		/* dword 1 */
	u8 add_status;		/* dword 1 */
	u8 rsvd1[2];		/* dword 1 */
	u32 response_length;	/* dword 2 */
	u32 actual_resp_len;	/* dword 3 */
};

struct phys_addr {
	u32 lo;
	u32 hi;
};

/**************************
 * BE Command definitions *
 **************************/

/* Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field */
struct amap_eq_context {
	u8 cidx[13];		/* dword 0*/
	u8 rsvd0[3];		/* dword 0*/
	u8 epidx[13];		/* dword 0*/
	u8 valid;		/* dword 0*/
	u8 rsvd1;		/* dword 0*/
	u8 size;		/* dword 0*/
	u8 pidx[13];		/* dword 1*/
	u8 rsvd2[3];		/* dword 1*/
	u8 pd[10];		/* dword 1*/
	u8 count[3];		/* dword 1*/
	u8 solevent;		/* dword 1*/
	u8 stalled;		/* dword 1*/
	u8 armed;		/* dword 1*/
	u8 rsvd3[4];		/* dword 2*/
	u8 func[8];		/* dword 2*/
	u8 rsvd4;		/* dword 2*/
	u8 delaymult[10];	/* dword 2*/
	u8 rsvd5[2];		/* dword 2*/
	u8 phase[2];		/* dword 2*/
	u8 nodelay;		/* dword 2*/
	u8 rsvd6[4];		/* dword 2*/
	u8 rsvd7[32];		/* dword 3*/
} __packed;

struct be_cmd_req_eq_create {
	struct be_cmd_req_hdr hdr;
	u16 num_pages;		/* sword */
	u16 rsvd0;		/* sword */
	u8 context[sizeof(struct amap_eq_context) / 8];
	struct phys_addr pages[8];
} __packed;

struct be_cmd_resp_eq_create {
	struct be_cmd_resp_hdr resp_hdr;
	u16 eq_id;		/* sword */
	u16 msix_idx;		/* available only in v2 */
} __packed;

/******************** Mac query ***************************/
enum {
	MAC_ADDRESS_TYPE_STORAGE = 0x0,
	MAC_ADDRESS_TYPE_NETWORK = 0x1,
	MAC_ADDRESS_TYPE_PD = 0x2,
	MAC_ADDRESS_TYPE_MANAGEMENT = 0x3
};

struct mac_addr {
	u16 size_of_struct;
	u8 addr[ETH_ALEN];
} __packed;

struct be_cmd_req_mac_query {
	struct be_cmd_req_hdr hdr;
	u8 type;
	u8 permanent;
	u16 if_id;
	u32 pmac_id;
} __packed;

struct be_cmd_resp_mac_query {
	struct be_cmd_resp_hdr hdr;
	struct mac_addr mac;
};

/******************** PMac Add ***************************/
struct be_cmd_req_pmac_add {
	struct be_cmd_req_hdr hdr;
	u32 if_id;
	u8 mac_address[ETH_ALEN];
	u8 rsvd0[2];
} __packed;

struct be_cmd_resp_pmac_add {
	struct be_cmd_resp_hdr hdr;
	u32 pmac_id;
};

/******************** PMac Del ***************************/
struct be_cmd_req_pmac_del {
	struct be_cmd_req_hdr hdr;
	u32 if_id;
	u32 pmac_id;
};

/******************** Create CQ ***************************/
/* Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field */
struct amap_cq_context_be {
	u8 cidx[11];		/* dword 0*/
	u8 rsvd0;		/* dword 0*/
	u8 coalescwm[2];	/* dword 0*/
	u8 nodelay;		/* dword 0*/
	u8 epidx[11];		/* dword 0*/
	u8 rsvd1;		/* dword 0*/
	u8 count[2];		/* dword 0*/
	u8 valid;		/* dword 0*/
	u8 solevent;		/* dword 0*/
	u8 eventable;		/* dword 0*/
	u8 pidx[11];		/* dword 1*/
	u8 rsvd2;		/* dword 1*/
	u8 pd[10];		/* dword 1*/
	u8 eqid[8];		/* dword 1*/
	u8 stalled;		/* dword 1*/
	u8 armed;		/* dword 1*/
	u8 rsvd3[4];		/* dword 2*/
	u8 func[8];		/* dword 2*/
	u8 rsvd4[20];		/* dword 2*/
	u8 rsvd5[32];		/* dword 3*/
} __packed;

struct amap_cq_context_v2 {
	u8 rsvd0[12];		/* dword 0*/
	u8 coalescwm[2];	/* dword 0*/
	u8 nodelay;		/* dword 0*/
	u8 rsvd1[12];		/* dword 0*/
	u8 count[2];		/* dword 0*/
	u8 valid;		/* dword 0*/
	u8 rsvd2;		/* dword 0*/
	u8 eventable;		/* dword 0*/
	u8 eqid[16];		/* dword 1*/
	u8 rsvd3[15];		/* dword 1*/
	u8 armed;		/* dword 1*/
	u8 rsvd4[32];		/* dword 2*/
	u8 rsvd5[32];		/* dword 3*/
} __packed;

struct be_cmd_req_cq_create {
	struct be_cmd_req_hdr hdr;
	u16 num_pages;
	u8 page_size;
	u8 rsvd0;
	u8 context[sizeof(struct amap_cq_context_be) / 8];
	struct phys_addr pages[8];
} __packed;


struct be_cmd_resp_cq_create {
	struct be_cmd_resp_hdr hdr;
	u16 cq_id;
	u16 rsvd0;
} __packed;

struct be_cmd_req_get_fat {
	struct be_cmd_req_hdr hdr;
	u32 fat_operation;
	u32 read_log_offset;
	u32 read_log_length;
	u32 data_buffer_size;
	u32 data_buffer[1];
} __packed;

struct be_cmd_resp_get_fat {
	struct be_cmd_resp_hdr hdr;
	u32 log_size;
	u32 read_log_length;
	u32 rsvd[2];
	u32 data_buffer[1];
} __packed;


/******************** Create MCCQ ***************************/
/* Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field */
struct amap_mcc_context_be {
	u8 con_index[14];
	u8 rsvd0[2];
	u8 ring_size[4];
	u8 fetch_wrb;
	u8 fetch_r2t;
	u8 cq_id[10];
	u8 prod_index[14];
	u8 fid[8];
	u8 pdid[9];
	u8 valid;
	u8 rsvd1[32];
	u8 rsvd2[32];
} __packed;

struct amap_mcc_context_v1 {
	u8 async_cq_id[16];
	u8 ring_size[4];
	u8 rsvd0[12];
	u8 rsvd1[31];
	u8 valid;
	u8 async_cq_valid[1];
	u8 rsvd2[31];
	u8 rsvd3[32];
} __packed;

struct be_cmd_req_mcc_create {
	struct be_cmd_req_hdr hdr;
	u16 num_pages;
	u16 cq_id;
	u8 context[sizeof(struct amap_mcc_context_be) / 8];
	struct phys_addr pages[8];
} __packed;

struct be_cmd_req_mcc_ext_create {
	struct be_cmd_req_hdr hdr;
	u16 num_pages;
	u16 cq_id;
	u32 async_event_bitmap[1];
	u8 context[sizeof(struct amap_mcc_context_v1) / 8];
	struct phys_addr pages[8];
} __packed;

struct be_cmd_resp_mcc_create {
	struct be_cmd_resp_hdr hdr;
	u16 id;
	u16 rsvd0;
} __packed;

/******************** Create TxQ ***************************/
#define BE_ETH_TX_RING_TYPE_STANDARD    	2
#define BE_ULP1_NUM				1

struct be_cmd_req_eth_tx_create {
	struct be_cmd_req_hdr hdr;
	u8 num_pages;
	u8 ulp_num;
	u16 type;
	u16 if_id;
	u8 queue_size;
	u8 rsvd0;
	u32 rsvd1;
	u16 cq_id;
	u16 rsvd2;
	u32 rsvd3[13];
	struct phys_addr pages[8];
} __packed;

struct be_cmd_resp_eth_tx_create {
	struct be_cmd_resp_hdr hdr;
	u16 cid;
	u16 rid;
	u32 db_offset;
	u32 rsvd0[4];
} __packed;

/******************** Create RxQ ***************************/
struct be_cmd_req_eth_rx_create {
	struct be_cmd_req_hdr hdr;
	u16 cq_id;
	u8 frag_size;
	u8 num_pages;
	struct phys_addr pages[2];
	u32 interface_id;
	u16 max_frame_size;
	u16 rsvd0;
	u32 rss_queue;
} __packed;

struct be_cmd_resp_eth_rx_create {
	struct be_cmd_resp_hdr hdr;
	u16 id;
	u8 rss_id;
	u8 rsvd0;
} __packed;

/******************** Q Destroy  ***************************/
/* Type of Queue to be destroyed */
enum {
	QTYPE_EQ = 1,
	QTYPE_CQ,
	QTYPE_TXQ,
	QTYPE_RXQ,
	QTYPE_MCCQ
};

struct be_cmd_req_q_destroy {
	struct be_cmd_req_hdr hdr;
	u16 id;
	u16 bypass_flush;	/* valid only for rx q destroy */
} __packed;

/************ I/f Create (it's actually I/f Config Create)**********/

/* Capability flags for the i/f */
enum be_if_flags {
	BE_IF_FLAGS_RSS = 0x4,
	BE_IF_FLAGS_PROMISCUOUS = 0x8,
	BE_IF_FLAGS_BROADCAST = 0x10,
	BE_IF_FLAGS_UNTAGGED = 0x20,
	BE_IF_FLAGS_ULP = 0x40,
	BE_IF_FLAGS_VLAN_PROMISCUOUS = 0x80,
	BE_IF_FLAGS_VLAN = 0x100,
	BE_IF_FLAGS_MCAST_PROMISCUOUS = 0x200,
	BE_IF_FLAGS_PASS_L2_ERRORS = 0x400,
	BE_IF_FLAGS_PASS_L3L4_ERRORS = 0x800,
	BE_IF_FLAGS_MULTICAST = 0x1000
};

#define BE_IF_CAP_FLAGS_WANT (BE_IF_FLAGS_RSS | BE_IF_FLAGS_PROMISCUOUS |\
			 BE_IF_FLAGS_BROADCAST | BE_IF_FLAGS_VLAN_PROMISCUOUS |\
			 BE_IF_FLAGS_VLAN | BE_IF_FLAGS_MCAST_PROMISCUOUS |\
			 BE_IF_FLAGS_PASS_L3L4_ERRORS | BE_IF_FLAGS_MULTICAST |\
			 BE_IF_FLAGS_UNTAGGED)

/* An RX interface is an object with one or more MAC addresses and
 * filtering capabilities. */
struct be_cmd_req_if_create {
	struct be_cmd_req_hdr hdr;
	u32 version;		/* ignore currently */
	u32 capability_flags;
	u32 enable_flags;
	u8 mac_addr[ETH_ALEN];
	u8 rsvd0;
	u8 pmac_invalid; /* if set, don't attach the mac addr to the i/f */
	u32 vlan_tag;	 /* not used currently */
} __packed;

struct be_cmd_resp_if_create {
	struct be_cmd_resp_hdr hdr;
	u32 interface_id;
	u32 pmac_id;
};

/****** I/f Destroy(it's actually I/f Config Destroy )**********/
struct be_cmd_req_if_destroy {
	struct be_cmd_req_hdr hdr;
	u32 interface_id;
};

/*************** HW Stats Get **********************************/
struct be_port_rxf_stats_v0 {
	u32 rx_bytes_lsd;	/* dword 0*/
	u32 rx_bytes_msd;	/* dword 1*/
	u32 rx_total_frames;	/* dword 2*/
	u32 rx_unicast_frames;	/* dword 3*/
	u32 rx_multicast_frames;	/* dword 4*/
	u32 rx_broadcast_frames;	/* dword 5*/
	u32 rx_crc_errors;	/* dword 6*/
	u32 rx_alignment_symbol_errors;	/* dword 7*/
	u32 rx_pause_frames;	/* dword 8*/
	u32 rx_control_frames;	/* dword 9*/
	u32 rx_in_range_errors;	/* dword 10*/
	u32 rx_out_range_errors;	/* dword 11*/
	u32 rx_frame_too_long;	/* dword 12*/
	u32 rx_address_filtered;	/* dword 13*/
	u32 rx_vlan_filtered;	/* dword 14*/
	u32 rx_dropped_too_small;	/* dword 15*/
	u32 rx_dropped_too_short;	/* dword 16*/
	u32 rx_dropped_header_too_small;	/* dword 17*/
	u32 rx_dropped_tcp_length;	/* dword 18*/
	u32 rx_dropped_runt;	/* dword 19*/
	u32 rx_64_byte_packets;	/* dword 20*/
	u32 rx_65_127_byte_packets;	/* dword 21*/
	u32 rx_128_256_byte_packets;	/* dword 22*/
	u32 rx_256_511_byte_packets;	/* dword 23*/
	u32 rx_512_1023_byte_packets;	/* dword 24*/
	u32 rx_1024_1518_byte_packets;	/* dword 25*/
	u32 rx_1519_2047_byte_packets;	/* dword 26*/
	u32 rx_2048_4095_byte_packets;	/* dword 27*/
	u32 rx_4096_8191_byte_packets;	/* dword 28*/
	u32 rx_8192_9216_byte_packets;	/* dword 29*/
	u32 rx_ip_checksum_errs;	/* dword 30*/
	u32 rx_tcp_checksum_errs;	/* dword 31*/
	u32 rx_udp_checksum_errs;	/* dword 32*/
	u32 rx_non_rss_packets;	/* dword 33*/
	u32 rx_ipv4_packets;	/* dword 34*/
	u32 rx_ipv6_packets;	/* dword 35*/
	u32 rx_ipv4_bytes_lsd;	/* dword 36*/
	u32 rx_ipv4_bytes_msd;	/* dword 37*/
	u32 rx_ipv6_bytes_lsd;	/* dword 38*/
	u32 rx_ipv6_bytes_msd;	/* dword 39*/
	u32 rx_chute1_packets;	/* dword 40*/
	u32 rx_chute2_packets;	/* dword 41*/
	u32 rx_chute3_packets;	/* dword 42*/
	u32 rx_management_packets;	/* dword 43*/
	u32 rx_switched_unicast_packets;	/* dword 44*/
	u32 rx_switched_multicast_packets;	/* dword 45*/
	u32 rx_switched_broadcast_packets;	/* dword 46*/
	u32 tx_bytes_lsd;	/* dword 47*/
	u32 tx_bytes_msd;	/* dword 48*/
	u32 tx_unicastframes;	/* dword 49*/
	u32 tx_multicastframes;	/* dword 50*/
	u32 tx_broadcastframes;	/* dword 51*/
	u32 tx_pauseframes;	/* dword 52*/
	u32 tx_controlframes;	/* dword 53*/
	u32 tx_64_byte_packets;	/* dword 54*/
	u32 tx_65_127_byte_packets;	/* dword 55*/
	u32 tx_128_256_byte_packets;	/* dword 56*/
	u32 tx_256_511_byte_packets;	/* dword 57*/
	u32 tx_512_1023_byte_packets;	/* dword 58*/
	u32 tx_1024_1518_byte_packets;	/* dword 59*/
	u32 tx_1519_2047_byte_packets;	/* dword 60*/
	u32 tx_2048_4095_byte_packets;	/* dword 61*/
	u32 tx_4096_8191_byte_packets;	/* dword 62*/
	u32 tx_8192_9216_byte_packets;	/* dword 63*/
	u32 rx_fifo_overflow;	/* dword 64*/
	u32 rx_input_fifo_overflow;	/* dword 65*/
};

struct be_rxf_stats_v0 {
	struct be_port_rxf_stats_v0 port[2];
	u32 rx_drops_no_pbuf;	/* dword 132*/
	u32 rx_drops_no_txpb;	/* dword 133*/
	u32 rx_drops_no_erx_descr;	/* dword 134*/
	u32 rx_drops_no_tpre_descr;	/* dword 135*/
	u32 management_rx_port_packets;	/* dword 136*/
	u32 management_rx_port_bytes;	/* dword 137*/
	u32 management_rx_port_pause_frames;	/* dword 138*/
	u32 management_rx_port_errors;	/* dword 139*/
	u32 management_tx_port_packets;	/* dword 140*/
	u32 management_tx_port_bytes;	/* dword 141*/
	u32 management_tx_port_pause;	/* dword 142*/
	u32 management_rx_port_rxfifo_overflow;	/* dword 143*/
	u32 rx_drops_too_many_frags;	/* dword 144*/
	u32 rx_drops_invalid_ring;	/* dword 145*/
	u32 forwarded_packets;	/* dword 146*/
	u32 rx_drops_mtu;	/* dword 147*/
	u32 rsvd0[7];
	u32 port0_jabber_events;
	u32 port1_jabber_events;
	u32 rsvd1[6];
};

struct be_erx_stats_v0 {
	u32 rx_drops_no_fragments[44];     /* dwordS 0 to 43*/
	u32 rsvd[4];
};

struct be_pmem_stats {
	u32 eth_red_drops;
	u32 rsvd[5];
};

struct be_hw_stats_v0 {
	struct be_rxf_stats_v0 rxf;
	u32 rsvd[48];
	struct be_erx_stats_v0 erx;
	struct be_pmem_stats pmem;
};

struct be_cmd_req_get_stats_v0 {
	struct be_cmd_req_hdr hdr;
	u8 rsvd[sizeof(struct be_hw_stats_v0)];
};

struct be_cmd_resp_get_stats_v0 {
	struct be_cmd_resp_hdr hdr;
	struct be_hw_stats_v0 hw_stats;
};

struct lancer_pport_stats {
	u32 tx_packets_lo;
	u32 tx_packets_hi;
	u32 tx_unicast_packets_lo;
	u32 tx_unicast_packets_hi;
	u32 tx_multicast_packets_lo;
	u32 tx_multicast_packets_hi;
	u32 tx_broadcast_packets_lo;
	u32 tx_broadcast_packets_hi;
	u32 tx_bytes_lo;
	u32 tx_bytes_hi;
	u32 tx_unicast_bytes_lo;
	u32 tx_unicast_bytes_hi;
	u32 tx_multicast_bytes_lo;
	u32 tx_multicast_bytes_hi;
	u32 tx_broadcast_bytes_lo;
	u32 tx_broadcast_bytes_hi;
	u32 tx_discards_lo;
	u32 tx_discards_hi;
	u32 tx_errors_lo;
	u32 tx_errors_hi;
	u32 tx_pause_frames_lo;
	u32 tx_pause_frames_hi;
	u32 tx_pause_on_frames_lo;
	u32 tx_pause_on_frames_hi;
	u32 tx_pause_off_frames_lo;
	u32 tx_pause_off_frames_hi;
	u32 tx_internal_mac_errors_lo;
	u32 tx_internal_mac_errors_hi;
	u32 tx_control_frames_lo;
	u32 tx_control_frames_hi;
	u32 tx_packets_64_bytes_lo;
	u32 tx_packets_64_bytes_hi;
	u32 tx_packets_65_to_127_bytes_lo;
	u32 tx_packets_65_to_127_bytes_hi;
	u32 tx_packets_128_to_255_bytes_lo;
	u32 tx_packets_128_to_255_bytes_hi;
	u32 tx_packets_256_to_511_bytes_lo;
	u32 tx_packets_256_to_511_bytes_hi;
	u32 tx_packets_512_to_1023_bytes_lo;
	u32 tx_packets_512_to_1023_bytes_hi;
	u32 tx_packets_1024_to_1518_bytes_lo;
	u32 tx_packets_1024_to_1518_bytes_hi;
	u32 tx_packets_1519_to_2047_bytes_lo;
	u32 tx_packets_1519_to_2047_bytes_hi;
	u32 tx_packets_2048_to_4095_bytes_lo;
	u32 tx_packets_2048_to_4095_bytes_hi;
	u32 tx_packets_4096_to_8191_bytes_lo;
	u32 tx_packets_4096_to_8191_bytes_hi;
	u32 tx_packets_8192_to_9216_bytes_lo;
	u32 tx_packets_8192_to_9216_bytes_hi;
	u32 tx_lso_packets_lo;
	u32 tx_lso_packets_hi;
	u32 rx_packets_lo;
	u32 rx_packets_hi;
	u32 rx_unicast_packets_lo;
	u32 rx_unicast_packets_hi;
	u32 rx_multicast_packets_lo;
	u32 rx_multicast_packets_hi;
	u32 rx_broadcast_packets_lo;
	u32 rx_broadcast_packets_hi;
	u32 rx_bytes_lo;
	u32 rx_bytes_hi;
	u32 rx_unicast_bytes_lo;
	u32 rx_unicast_bytes_hi;
	u32 rx_multicast_bytes_lo;
	u32 rx_multicast_bytes_hi;
	u32 rx_broadcast_bytes_lo;
	u32 rx_broadcast_bytes_hi;
	u32 rx_unknown_protos;
	u32 rsvd_69; /* Word 69 is reserved */
	u32 rx_discards_lo;
	u32 rx_discards_hi;
	u32 rx_errors_lo;
	u32 rx_errors_hi;
	u32 rx_crc_errors_lo;
	u32 rx_crc_errors_hi;
	u32 rx_alignment_errors_lo;
	u32 rx_alignment_errors_hi;
	u32 rx_symbol_errors_lo;
	u32 rx_symbol_errors_hi;
	u32 rx_pause_frames_lo;
	u32 rx_pause_frames_hi;
	u32 rx_pause_on_frames_lo;
	u32 rx_pause_on_frames_hi;
	u32 rx_pause_off_frames_lo;
	u32 rx_pause_off_frames_hi;
	u32 rx_frames_too_long_lo;
	u32 rx_frames_too_long_hi;
	u32 rx_internal_mac_errors_lo;
	u32 rx_internal_mac_errors_hi;
	u32 rx_undersize_packets;
	u32 rx_oversize_packets;
	u32 rx_fragment_packets;
	u32 rx_jabbers;
	u32 rx_control_frames_lo;
	u32 rx_control_frames_hi;
	u32 rx_control_frames_unknown_opcode_lo;
	u32 rx_control_frames_unknown_opcode_hi;
	u32 rx_in_range_errors;
	u32 rx_out_of_range_errors;
	u32 rx_address_filtered;
	u32 rx_vlan_filtered;
	u32 rx_dropped_too_small;
	u32 rx_dropped_too_short;
	u32 rx_dropped_header_too_small;
	u32 rx_dropped_invalid_tcp_length;
	u32 rx_dropped_runt;
	u32 rx_ip_checksum_errors;
	u32 rx_tcp_checksum_errors;
	u32 rx_udp_checksum_errors;
	u32 rx_non_rss_packets;
	u32 rsvd_111;
	u32 rx_ipv4_packets_lo;
	u32 rx_ipv4_packets_hi;
	u32 rx_ipv6_packets_lo;
	u32 rx_ipv6_packets_hi;
	u32 rx_ipv4_bytes_lo;
	u32 rx_ipv4_bytes_hi;
	u32 rx_ipv6_bytes_lo;
	u32 rx_ipv6_bytes_hi;
	u32 rx_nic_packets_lo;
	u32 rx_nic_packets_hi;
	u32 rx_tcp_packets_lo;
	u32 rx_tcp_packets_hi;
	u32 rx_iscsi_packets_lo;
	u32 rx_iscsi_packets_hi;
	u32 rx_management_packets_lo;
	u32 rx_management_packets_hi;
	u32 rx_switched_unicast_packets_lo;
	u32 rx_switched_unicast_packets_hi;
	u32 rx_switched_multicast_packets_lo;
	u32 rx_switched_multicast_packets_hi;
	u32 rx_switched_broadcast_packets_lo;
	u32 rx_switched_broadcast_packets_hi;
	u32 num_forwards_lo;
	u32 num_forwards_hi;
	u32 rx_fifo_overflow;
	u32 rx_input_fifo_overflow;
	u32 rx_drops_too_many_frags_lo;
	u32 rx_drops_too_many_frags_hi;
	u32 rx_drops_invalid_queue;
	u32 rsvd_141;
	u32 rx_drops_mtu_lo;
	u32 rx_drops_mtu_hi;
	u32 rx_packets_64_bytes_lo;
	u32 rx_packets_64_bytes_hi;
	u32 rx_packets_65_to_127_bytes_lo;
	u32 rx_packets_65_to_127_bytes_hi;
	u32 rx_packets_128_to_255_bytes_lo;
	u32 rx_packets_128_to_255_bytes_hi;
	u32 rx_packets_256_to_511_bytes_lo;
	u32 rx_packets_256_to_511_bytes_hi;
	u32 rx_packets_512_to_1023_bytes_lo;
	u32 rx_packets_512_to_1023_bytes_hi;
	u32 rx_packets_1024_to_1518_bytes_lo;
	u32 rx_packets_1024_to_1518_bytes_hi;
	u32 rx_packets_1519_to_2047_bytes_lo;
	u32 rx_packets_1519_to_2047_bytes_hi;
	u32 rx_packets_2048_to_4095_bytes_lo;
	u32 rx_packets_2048_to_4095_bytes_hi;
	u32 rx_packets_4096_to_8191_bytes_lo;
	u32 rx_packets_4096_to_8191_bytes_hi;
	u32 rx_packets_8192_to_9216_bytes_lo;
	u32 rx_packets_8192_to_9216_bytes_hi;
};

struct pport_stats_params {
	u16 pport_num;
	u8 rsvd;
	u8 reset_stats;
};

struct lancer_cmd_req_pport_stats {
	struct be_cmd_req_hdr hdr;
	union {
		struct pport_stats_params params;
		u8 rsvd[sizeof(struct lancer_pport_stats)];
	} cmd_params;
};

struct lancer_cmd_resp_pport_stats {
	struct be_cmd_resp_hdr hdr;
	struct lancer_pport_stats pport_stats;
};

static inline struct lancer_pport_stats*
	pport_stats_from_cmd(struct be_adapter *adapter)
{
	struct lancer_cmd_resp_pport_stats *cmd = adapter->stats_cmd.va;
	return &cmd->pport_stats;
}

struct be_cmd_req_get_cntl_addnl_attribs {
	struct be_cmd_req_hdr hdr;
	u8 rsvd[8];
};

struct be_cmd_resp_get_cntl_addnl_attribs {
	struct be_cmd_resp_hdr hdr;
	u16 ipl_file_number;
	u8 ipl_file_version;
	u8 rsvd0;
	u8 on_die_temperature; /* in degrees centigrade*/
	u8 rsvd1[3];
};

struct be_cmd_req_vlan_config {
	struct be_cmd_req_hdr hdr;
	u8 interface_id;
	u8 promiscuous;
	u8 untagged;
	u8 num_vlan;
	u16 normal_vlan[64];
} __packed;

/******************* RX FILTER ******************************/
#define BE_MAX_MC		64 /* set mcast promisc if > 64 */
struct macaddr {
	u8 byte[ETH_ALEN];
};

struct be_cmd_req_rx_filter {
	struct be_cmd_req_hdr hdr;
	u32 global_flags_mask;
	u32 global_flags;
	u32 if_flags_mask;
	u32 if_flags;
	u32 if_id;
	u32 mcast_num;
	struct macaddr mcast_mac[BE_MAX_MC];
};

/******************** Link Status Query *******************/
struct be_cmd_req_link_status {
	struct be_cmd_req_hdr hdr;
	u32 rsvd;
};

enum {
	PHY_LINK_DUPLEX_NONE = 0x0,
	PHY_LINK_DUPLEX_HALF = 0x1,
	PHY_LINK_DUPLEX_FULL = 0x2
};

enum {
	PHY_LINK_SPEED_ZERO = 0x0, 	/* => No link */
	PHY_LINK_SPEED_10MBPS = 0x1,
	PHY_LINK_SPEED_100MBPS = 0x2,
	PHY_LINK_SPEED_1GBPS = 0x3,
	PHY_LINK_SPEED_10GBPS = 0x4,
	PHY_LINK_SPEED_20GBPS = 0x5,
	PHY_LINK_SPEED_25GBPS = 0x6,
	PHY_LINK_SPEED_40GBPS = 0x7
};

struct be_cmd_resp_link_status {
	struct be_cmd_resp_hdr hdr;
	u8 physical_port;
	u8 mac_duplex;
	u8 mac_speed;
	u8 mac_fault;
	u8 mgmt_mac_duplex;
	u8 mgmt_mac_speed;
	u16 link_speed;
	u8 logical_link_status;
	u8 rsvd1[3];
} __packed;

/******************** Port Identification ***************************/
/*    Identifies the type of port attached to NIC     */
struct be_cmd_req_port_type {
	struct be_cmd_req_hdr hdr;
	u32 page_num;
	u32 port;
};

enum {
	TR_PAGE_A0 = 0xa0,
	TR_PAGE_A2 = 0xa2
};

struct be_cmd_resp_port_type {
	struct be_cmd_resp_hdr hdr;
	u32 page_num;
	u32 port;
	struct data {
		u8 identifier;
		u8 identifier_ext;
		u8 connector;
		u8 transceiver[8];
		u8 rsvd0[3];
		u8 length_km;
		u8 length_hm;
		u8 length_om1;
		u8 length_om2;
		u8 length_cu;
		u8 length_cu_m;
		u8 vendor_name[16];
		u8 rsvd;
		u8 vendor_oui[3];
		u8 vendor_pn[16];
		u8 vendor_rev[4];
	} data;
};

/******************** Get FW Version *******************/
struct be_cmd_req_get_fw_version {
	struct be_cmd_req_hdr hdr;
	u8 rsvd0[FW_VER_LEN];
	u8 rsvd1[FW_VER_LEN];
} __packed;

struct be_cmd_resp_get_fw_version {
	struct be_cmd_resp_hdr hdr;
	u8 firmware_version_string[FW_VER_LEN];
	u8 fw_on_flash_version_string[FW_VER_LEN];
} __packed;

/******************** Set Flow Contrl *******************/
struct be_cmd_req_set_flow_control {
	struct be_cmd_req_hdr hdr;
	u16 tx_flow_control;
	u16 rx_flow_control;
} __packed;

/******************** Get Flow Contrl *******************/
struct be_cmd_req_get_flow_control {
	struct be_cmd_req_hdr hdr;
	u32 rsvd;
};

struct be_cmd_resp_get_flow_control {
	struct be_cmd_resp_hdr hdr;
	u16 tx_flow_control;
	u16 rx_flow_control;
} __packed;

/******************** Modify EQ Delay *******************/
struct be_set_eqd {
	u32 eq_id;
	u32 phase;
	u32 delay_multiplier;
};

struct be_cmd_req_modify_eq_delay {
	struct be_cmd_req_hdr hdr;
	u32 num_eq;
	struct be_set_eqd set_eqd[MAX_EVT_QS];
} __packed;

struct be_cmd_resp_modify_eq_delay {
	struct be_cmd_resp_hdr hdr;
	u32 rsvd0;
} __packed;

/******************** Get FW Config *******************/
/* The HW can come up in either of the following multi-channel modes
 * based on the skew/IPL.
 */
#define RDMA_ENABLED				0x4
#define FLEX10_MODE				0x400
#define VNIC_MODE				0x20000
#define UMC_ENABLED				0x1000000
struct be_cmd_req_query_fw_cfg {
	struct be_cmd_req_hdr hdr;
	u32 rsvd[31];
};

struct be_cmd_resp_query_fw_cfg {
	struct be_cmd_resp_hdr hdr;
	u32 be_config_number;
	u32 asic_revision;
	u32 phys_port;
	u32 function_mode;
	u32 rsvd[26];
	u32 function_caps;
};

/******************** RSS Config ****************************************/
/* RSS type		Input parameters used to compute RX hash
 * RSS_ENABLE_IPV4	SRC IPv4, DST IPv4
 * RSS_ENABLE_TCP_IPV4	SRC IPv4, DST IPv4, TCP SRC PORT, TCP DST PORT
 * RSS_ENABLE_IPV6	SRC IPv6, DST IPv6
 * RSS_ENABLE_TCP_IPV6	SRC IPv6, DST IPv6, TCP SRC PORT, TCP DST PORT
 * RSS_ENABLE_UDP_IPV4	SRC IPv4, DST IPv4, UDP SRC PORT, UDP DST PORT
 * RSS_ENABLE_UDP_IPV6	SRC IPv6, DST IPv6, UDP SRC PORT, UDP DST PORT
 *
 * When multiple RSS types are enabled, HW picks the best hash policy
 * based on the type of the received packet.
 */
#define RSS_ENABLE_NONE				0x0
#define RSS_ENABLE_IPV4				0x1
#define RSS_ENABLE_TCP_IPV4			0x2
#define RSS_ENABLE_IPV6				0x4
#define RSS_ENABLE_TCP_IPV6			0x8
#define RSS_ENABLE_UDP_IPV4			0x10
#define RSS_ENABLE_UDP_IPV6			0x20

#define L3_RSS_FLAGS				(RXH_IP_DST | RXH_IP_SRC)
#define L4_RSS_FLAGS				(RXH_L4_B_0_1 | RXH_L4_B_2_3)

struct be_cmd_req_rss_config {
	struct be_cmd_req_hdr hdr;
	u32 if_id;
	u16 enable_rss;
	u16 cpu_table_size_log2;
	u32 hash[10];
	u8 cpu_table[128];
	u8 flush;
	u8 rsvd0[3];
};

/******************** Port Beacon ***************************/

#define BEACON_STATE_ENABLED		0x1
#define BEACON_STATE_DISABLED		0x0

struct be_cmd_req_enable_disable_beacon {
	struct be_cmd_req_hdr hdr;
	u8  port_num;
	u8  beacon_state;
	u8  beacon_duration;
	u8  status_duration;
} __packed;

struct be_cmd_resp_enable_disable_beacon {
	struct be_cmd_resp_hdr resp_hdr;
	u32 rsvd0;
} __packed;

struct be_cmd_req_get_beacon_state {
	struct be_cmd_req_hdr hdr;
	u8  port_num;
	u8  rsvd0;
	u16 rsvd1;
} __packed;

struct be_cmd_resp_get_beacon_state {
	struct be_cmd_resp_hdr resp_hdr;
	u8 beacon_state;
	u8 rsvd0[3];
} __packed;

/****************** Firmware Flash ******************/
struct flashrom_params {
	u32 op_code;
	u32 op_type;
	u32 data_buf_size;
	u32 offset;
};

struct be_cmd_write_flashrom {
	struct be_cmd_req_hdr hdr;
	struct flashrom_params params;
	u8 data_buf[32768];
	u8 rsvd[4];
} __packed;

/* cmd to read flash crc */
struct be_cmd_read_flash_crc {
	struct be_cmd_req_hdr hdr;
	struct flashrom_params params;
	u8 crc[4];
	u8 rsvd[4];
};
/**************** Lancer Firmware Flash ************/
struct amap_lancer_write_obj_context {
	u8 write_length[24];
	u8 reserved1[7];
	u8 eof;
} __packed;

struct lancer_cmd_req_write_object {
	struct be_cmd_req_hdr hdr;
	u8 context[sizeof(struct amap_lancer_write_obj_context) / 8];
	u32 write_offset;
	u8 object_name[104];
	u32 descriptor_count;
	u32 buf_len;
	u32 addr_low;
	u32 addr_high;
};

#define LANCER_NO_RESET_NEEDED		0x00
#define LANCER_FW_RESET_NEEDED		0x02
struct lancer_cmd_resp_write_object {
	u8 opcode;
	u8 subsystem;
	u8 rsvd1[2];
	u8 status;
	u8 additional_status;
	u8 rsvd2[2];
	u32 resp_len;
	u32 actual_resp_len;
	u32 actual_write_len;
	u8 change_status;
	u8 rsvd3[3];
};

/************************ Lancer Read FW info **************/
#define LANCER_READ_FILE_CHUNK			(32*1024)
#define LANCER_READ_FILE_EOF_MASK		0x80000000

#define LANCER_FW_DUMP_FILE			"/dbg/dump.bin"
#define LANCER_VPD_PF_FILE			"/vpd/ntr_pf.vpd"
#define LANCER_VPD_VF_FILE			"/vpd/ntr_vf.vpd"

struct lancer_cmd_req_read_object {
	struct be_cmd_req_hdr hdr;
	u32 desired_read_len;
	u32 read_offset;
	u8 object_name[104];
	u32 descriptor_count;
	u32 buf_len;
	u32 addr_low;
	u32 addr_high;
};

struct lancer_cmd_resp_read_object {
	u8 opcode;
	u8 subsystem;
	u8 rsvd1[2];
	u8 status;
	u8 additional_status;
	u8 rsvd2[2];
	u32 resp_len;
	u32 actual_resp_len;
	u32 actual_read_len;
	u32 eof;
};

/************************ WOL *******************************/
struct be_cmd_req_acpi_wol_magic_config{
	struct be_cmd_req_hdr hdr;
	u32 rsvd0[145];
	u8 magic_mac[6];
	u8 rsvd2[2];
} __packed;

struct be_cmd_req_acpi_wol_magic_config_v1 {
	struct be_cmd_req_hdr hdr;
	u8 rsvd0[2];
	u8 query_options;
	u8 rsvd1[5];
	u32 rsvd2[288];
	u8 magic_mac[6];
	u8 rsvd3[22];
} __packed;

struct be_cmd_resp_acpi_wol_magic_config_v1 {
	struct be_cmd_resp_hdr hdr;
	u8 rsvd0[2];
	u8 wol_settings;
	u8 rsvd1[5];
	u32 rsvd2[295];
} __packed;

#define BE_GET_WOL_CAP			2

#define BE_WOL_CAP			0x1
#define BE_PME_D0_CAP			0x8
#define BE_PME_D1_CAP			0x10
#define BE_PME_D2_CAP			0x20
#define BE_PME_D3HOT_CAP		0x40
#define BE_PME_D3COLD_CAP		0x80

/********************** LoopBack test *********************/
struct be_cmd_req_loopback_test {
	struct be_cmd_req_hdr hdr;
	u32 loopback_type;
	u32 num_pkts;
	u64 pattern;
	u32 src_port;
	u32 dest_port;
	u32 pkt_size;
};

struct be_cmd_resp_loopback_test {
	struct be_cmd_resp_hdr resp_hdr;
	u32    status;
	u32    num_txfer;
	u32    num_rx;
	u32    miscomp_off;
	u32    ticks_compl;
};

struct be_cmd_req_set_lmode {
	struct be_cmd_req_hdr hdr;
	u8 src_port;
	u8 dest_port;
	u8 loopback_type;
	u8 loopback_state;
};

struct be_cmd_resp_set_lmode {
	struct be_cmd_resp_hdr resp_hdr;
	u8 rsvd0[4];
};

/********************** DDR DMA test *********************/
struct be_cmd_req_ddrdma_test {
	struct be_cmd_req_hdr hdr;
	u64 pattern;
	u32 byte_count;
	u32 rsvd0;
	u8  snd_buff[4096];
	u8  rsvd1[4096];
};

struct be_cmd_resp_ddrdma_test {
	struct be_cmd_resp_hdr hdr;
	u64 pattern;
	u32 byte_cnt;
	u32 snd_err;
	u8  rsvd0[4096];
	u8  rcv_buff[4096];
};

/*********************** SEEPROM Read ***********************/

#define BE_READ_SEEPROM_LEN 1024
struct be_cmd_req_seeprom_read {
	struct be_cmd_req_hdr hdr;
	u8 rsvd0[BE_READ_SEEPROM_LEN];
};

struct be_cmd_resp_seeprom_read {
	struct be_cmd_req_hdr hdr;
	u8 seeprom_data[BE_READ_SEEPROM_LEN];
};

enum {
	PHY_TYPE_CX4_10GB = 0,
	PHY_TYPE_XFP_10GB,
	PHY_TYPE_SFP_1GB,
	PHY_TYPE_SFP_PLUS_10GB,
	PHY_TYPE_KR_10GB,
	PHY_TYPE_KX4_10GB,
	PHY_TYPE_BASET_10GB,
	PHY_TYPE_BASET_1GB,
	PHY_TYPE_BASEX_1GB,
	PHY_TYPE_SGMII,
	PHY_TYPE_DISABLED = 255
};

#define BE_SUPPORTED_SPEED_NONE		0
#define BE_SUPPORTED_SPEED_10MBPS	1
#define BE_SUPPORTED_SPEED_100MBPS	2
#define BE_SUPPORTED_SPEED_1GBPS	4
#define BE_SUPPORTED_SPEED_10GBPS	8

#define BE_AN_EN			0x2
#define BE_PAUSE_SYM_EN			0x80

/* MAC speed valid values */
#define SPEED_DEFAULT  0x0
#define SPEED_FORCED_10GB  0x1
#define SPEED_FORCED_1GB  0x2
#define SPEED_AUTONEG_10GB  0x3
#define SPEED_AUTONEG_1GB  0x4
#define SPEED_AUTONEG_100MB  0x5
#define SPEED_AUTONEG_10GB_1GB 0x6
#define SPEED_AUTONEG_10GB_1GB_100MB 0x7
#define SPEED_AUTONEG_1GB_100MB  0x8
#define SPEED_AUTONEG_10MB  0x9
#define SPEED_AUTONEG_1GB_100MB_10MB 0xa
#define SPEED_AUTONEG_100MB_10MB 0xb
#define SPEED_FORCED_100MB  0xc
#define SPEED_FORCED_10MB  0xd

struct be_cmd_req_get_phy_info {
	struct be_cmd_req_hdr hdr;
	u8 rsvd0[24];
};

struct be_phy_info {
	u16 phy_type;
	u16 interface_type;
	u32 misc_params;
	u16 ext_phy_details;
	u16 rsvd;
	u16 auto_speeds_supported;
	u16 fixed_speeds_supported;
	u32 future_use[2];
};

struct be_cmd_resp_get_phy_info {
	struct be_cmd_req_hdr hdr;
	struct be_phy_info phy_info;
};

/*********************** Set QOS ***********************/

#define BE_QOS_BITS_NIC				1

struct be_cmd_req_set_qos {
	struct be_cmd_req_hdr hdr;
	u32 valid_bits;
	u32 max_bps_nic;
	u32 rsvd[7];
};

struct be_cmd_resp_set_qos {
	struct be_cmd_resp_hdr hdr;
	u32 rsvd;
};

/*********************** Controller Attributes ***********************/
struct be_cmd_req_cntl_attribs {
	struct be_cmd_req_hdr hdr;
};

struct be_cmd_resp_cntl_attribs {
	struct be_cmd_resp_hdr hdr;
	struct mgmt_controller_attrib attribs;
};

/*********************** Set driver function ***********************/
#define CAPABILITY_SW_TIMESTAMPS	2
#define CAPABILITY_BE3_NATIVE_ERX_API	4

struct be_cmd_req_set_func_cap {
	struct be_cmd_req_hdr hdr;
	u32 valid_cap_flags;
	u32 cap_flags;
	u8 rsvd[212];
};

struct be_cmd_resp_set_func_cap {
	struct be_cmd_resp_hdr hdr;
	u32 valid_cap_flags;
	u32 cap_flags;
	u8 rsvd[212];
};

/*********************** Function Privileges ***********************/
enum {
	BE_PRIV_DEFAULT = 0x1,
	BE_PRIV_LNKQUERY = 0x2,
	BE_PRIV_LNKSTATS = 0x4,
	BE_PRIV_LNKMGMT = 0x8,
	BE_PRIV_LNKDIAG = 0x10,
	BE_PRIV_UTILQUERY = 0x20,
	BE_PRIV_FILTMGMT = 0x40,
	BE_PRIV_IFACEMGMT = 0x80,
	BE_PRIV_VHADM = 0x100,
	BE_PRIV_DEVCFG = 0x200,
	BE_PRIV_DEVSEC = 0x400
};
#define MAX_PRIVILEGES		(BE_PRIV_VHADM | BE_PRIV_DEVCFG | \
				 BE_PRIV_DEVSEC)
#define MIN_PRIVILEGES		BE_PRIV_DEFAULT

struct be_cmd_priv_map {
	u8 opcode;
	u8 subsystem;
	u32 priv_mask;
};

struct be_cmd_req_get_fn_privileges {
	struct be_cmd_req_hdr hdr;
	u32 rsvd;
};

struct be_cmd_resp_get_fn_privileges {
	struct be_cmd_resp_hdr hdr;
	u32 privilege_mask;
};

struct be_cmd_req_set_fn_privileges {
	struct be_cmd_req_hdr hdr;
	u32 privileges;		/* Used by BE3, SH-R */
	u32 privileges_lancer;	/* Used by Lancer */
};

/******************** GET/SET_MACLIST  **************************/
#define BE_MAX_MAC			64
struct be_cmd_req_get_mac_list {
	struct be_cmd_req_hdr hdr;
	u8 mac_type;
	u8 perm_override;
	u16 iface_id;
	u32 mac_id;
	u32 rsvd[3];
} __packed;

struct get_list_macaddr {
	u16 mac_addr_size;
	union {
		u8 macaddr[6];
		struct {
			u8 rsvd[2];
			u32 mac_id;
		} __packed s_mac_id;
	} __packed mac_addr_id;
} __packed;

struct be_cmd_resp_get_mac_list {
	struct be_cmd_resp_hdr hdr;
	struct get_list_macaddr fd_macaddr; /* Factory default mac */
	struct get_list_macaddr macid_macaddr; /* soft mac */
	u8 true_mac_count;
	u8 pseudo_mac_count;
	u8 mac_list_size;
	u8 rsvd;
	/* perm override mac */
	struct get_list_macaddr macaddr_list[BE_MAX_MAC];
} __packed;

struct be_cmd_req_set_mac_list {
	struct be_cmd_req_hdr hdr;
	u8 mac_count;
	u8 rsvd1;
	u16 rsvd2;
	struct macaddr mac[BE_MAX_MAC];
} __packed;

/*********************** HSW Config ***********************/
#define PORT_FWD_TYPE_VEPA		0x3
#define PORT_FWD_TYPE_VEB		0x2

struct amap_set_hsw_context {
	u8 interface_id[16];
	u8 rsvd0[14];
	u8 pvid_valid;
	u8 pport;
	u8 rsvd1[6];
	u8 port_fwd_type[3];
	u8 rsvd2[7];
	u8 pvid[16];
	u8 rsvd3[32];
	u8 rsvd4[32];
	u8 rsvd5[32];
} __packed;

struct be_cmd_req_set_hsw_config {
	struct be_cmd_req_hdr hdr;
	u8 context[sizeof(struct amap_set_hsw_context) / 8];
} __packed;

struct be_cmd_resp_set_hsw_config {
	struct be_cmd_resp_hdr hdr;
	u32 rsvd;
};

struct amap_get_hsw_req_context {
	u8 interface_id[16];
	u8 rsvd0[14];
	u8 pvid_valid;
	u8 pport;
} __packed;

struct amap_get_hsw_resp_context {
	u8 rsvd0[6];
	u8 port_fwd_type[3];
	u8 rsvd1[7];
	u8 pvid[16];
	u8 rsvd2[32];
	u8 rsvd3[32];
	u8 rsvd4[32];
} __packed;

struct be_cmd_req_get_hsw_config {
	struct be_cmd_req_hdr hdr;
	u8 context[sizeof(struct amap_get_hsw_req_context) / 8];
} __packed;

struct be_cmd_resp_get_hsw_config {
	struct be_cmd_resp_hdr hdr;
	u8 context[sizeof(struct amap_get_hsw_resp_context) / 8];
	u32 rsvd;
};

/******************* get port names ***************/
struct be_cmd_req_get_port_name {
	struct be_cmd_req_hdr hdr;
	u32 rsvd0;
};

struct be_cmd_resp_get_port_name {
	struct be_cmd_req_hdr hdr;
	u8 port_name[4];
};

/*************** HW Stats Get v1 **********************************/
#define BE_TXP_SW_SZ			48
struct be_port_rxf_stats_v1 {
	u32 rsvd0[12];
	u32 rx_crc_errors;
	u32 rx_alignment_symbol_errors;
	u32 rx_pause_frames;
	u32 rx_priority_pause_frames;
	u32 rx_control_frames;
	u32 rx_in_range_errors;
	u32 rx_out_range_errors;
	u32 rx_frame_too_long;
	u32 rx_address_filtered;
	u32 rx_dropped_too_small;
	u32 rx_dropped_too_short;
	u32 rx_dropped_header_too_small;
	u32 rx_dropped_tcp_length;
	u32 rx_dropped_runt;
	u32 rsvd1[10];
	u32 rx_ip_checksum_errs;
	u32 rx_tcp_checksum_errs;
	u32 rx_udp_checksum_errs;
	u32 rsvd2[7];
	u32 rx_switched_unicast_packets;
	u32 rx_switched_multicast_packets;
	u32 rx_switched_broadcast_packets;
	u32 rsvd3[3];
	u32 tx_pauseframes;
	u32 tx_priority_pauseframes;
	u32 tx_controlframes;
	u32 rsvd4[10];
	u32 rxpp_fifo_overflow_drop;
	u32 rx_input_fifo_overflow_drop;
	u32 pmem_fifo_overflow_drop;
	u32 jabber_events;
	u32 rsvd5[3];
};


struct be_rxf_stats_v1 {
	struct be_port_rxf_stats_v1 port[4];
	u32 rsvd0[2];
	u32 rx_drops_no_pbuf;
	u32 rx_drops_no_txpb;
	u32 rx_drops_no_erx_descr;
	u32 rx_drops_no_tpre_descr;
	u32 rsvd1[6];
	u32 rx_drops_too_many_frags;
	u32 rx_drops_invalid_ring;
	u32 forwarded_packets;
	u32 rx_drops_mtu;
	u32 rsvd2[14];
};

struct be_erx_stats_v1 {
	u32 rx_drops_no_fragments[68];     /* dwordS 0 to 67*/
	u32 rsvd[4];
};

struct be_port_rxf_stats_v2 {
	u32 rsvd0[10];
	u32 roce_bytes_received_lsd;
	u32 roce_bytes_received_msd;
	u32 rsvd1[5];
	u32 roce_frames_received;
	u32 rx_crc_errors;
	u32 rx_alignment_symbol_errors;
	u32 rx_pause_frames;
	u32 rx_priority_pause_frames;
	u32 rx_control_frames;
	u32 rx_in_range_errors;
	u32 rx_out_range_errors;
	u32 rx_frame_too_long;
	u32 rx_address_filtered;
	u32 rx_dropped_too_small;
	u32 rx_dropped_too_short;
	u32 rx_dropped_header_too_small;
	u32 rx_dropped_tcp_length;
	u32 rx_dropped_runt;
	u32 rsvd2[10];
	u32 rx_ip_checksum_errs;
	u32 rx_tcp_checksum_errs;
	u32 rx_udp_checksum_errs;
	u32 rsvd3[7];
	u32 rx_switched_unicast_packets;
	u32 rx_switched_multicast_packets;
	u32 rx_switched_broadcast_packets;
	u32 rsvd4[3];
	u32 tx_pauseframes;
	u32 tx_priority_pauseframes;
	u32 tx_controlframes;
	u32 rsvd5[10];
	u32 rxpp_fifo_overflow_drop;
	u32 rx_input_fifo_overflow_drop;
	u32 pmem_fifo_overflow_drop;
	u32 jabber_events;
	u32 rsvd6[3];
	u32 rx_drops_payload_size;
	u32 rx_drops_clipped_header;
	u32 rx_drops_crc;
	u32 roce_drops_payload_len;
	u32 roce_drops_crc;
	u32 rsvd7[19];
};

struct be_rxf_stats_v2 {
	struct be_port_rxf_stats_v2 port[4];
	u32 rsvd0[2];
	u32 rx_drops_no_pbuf;
	u32 rx_drops_no_txpb;
	u32 rx_drops_no_erx_descr;
	u32 rx_drops_no_tpre_descr;
	u32 rsvd1[6];
	u32 rx_drops_too_many_frags;
	u32 rx_drops_invalid_ring;
	u32 forwarded_packets;
	u32 rx_drops_mtu;
	u32 rsvd2[35];
};

struct be_hw_stats_v1 {
	struct be_rxf_stats_v1 rxf;
	u32 rsvd0[BE_TXP_SW_SZ];
	struct be_erx_stats_v1 erx;
	struct be_pmem_stats pmem;
	u32 rsvd1[18];
};

struct be_cmd_req_get_stats_v1 {
	struct be_cmd_req_hdr hdr;
	u8 rsvd[sizeof(struct be_hw_stats_v1)];
};

struct be_cmd_resp_get_stats_v1 {
	struct be_cmd_resp_hdr hdr;
	struct be_hw_stats_v1 hw_stats;
};

struct be_erx_stats_v2 {
	u32 rx_drops_no_fragments[136];     /* dwordS 0 to 135*/
	u32 rsvd[3];
};

struct be_hw_stats_v2 {
	struct be_rxf_stats_v2 rxf;
	u32 rsvd0[BE_TXP_SW_SZ];
	struct be_erx_stats_v2 erx;
	struct be_pmem_stats pmem;
	u32 rsvd1[18];
};

struct be_cmd_req_get_stats_v2 {
	struct be_cmd_req_hdr hdr;
	u8 rsvd[sizeof(struct be_hw_stats_v2)];
};

struct be_cmd_resp_get_stats_v2 {
	struct be_cmd_resp_hdr hdr;
	struct be_hw_stats_v2 hw_stats;
};

/************** get fat capabilites *******************/
#define MAX_MODULES 27
#define MAX_MODES 4
#define MODE_UART 0
#define FW_LOG_LEVEL_DEFAULT 48
#define FW_LOG_LEVEL_FATAL 64

struct ext_fat_mode {
	u8 mode;
	u8 rsvd0;
	u16 port_mask;
	u32 dbg_lvl;
	u64 fun_mask;
} __packed;

struct ext_fat_modules {
	u8 modules_str[32];
	u32 modules_id;
	u32 num_modes;
	struct ext_fat_mode trace_lvl[MAX_MODES];
} __packed;

struct be_fat_conf_params {
	u32 max_log_entries;
	u32 log_entry_size;
	u8 log_type;
	u8 max_log_funs;
	u8 max_log_ports;
	u8 rsvd0;
	u32 supp_modes;
	u32 num_modules;
	struct ext_fat_modules module[MAX_MODULES];
} __packed;

struct be_cmd_req_get_ext_fat_caps {
	struct be_cmd_req_hdr hdr;
	u32 parameter_type;
};

struct be_cmd_resp_get_ext_fat_caps {
	struct be_cmd_resp_hdr hdr;
	struct be_fat_conf_params get_params;
};

struct be_cmd_req_set_ext_fat_caps {
	struct be_cmd_req_hdr hdr;
	struct be_fat_conf_params set_params;
};

#define RESOURCE_DESC_SIZE_V0			72
#define RESOURCE_DESC_SIZE_V1			88
#define PCIE_RESOURCE_DESC_TYPE_V0		0x40
#define NIC_RESOURCE_DESC_TYPE_V0		0x41
#define PCIE_RESOURCE_DESC_TYPE_V1		0x50
#define NIC_RESOURCE_DESC_TYPE_V1		0x51
#define PORT_RESOURCE_DESC_TYPE_V1		0x55
#define MAX_RESOURCE_DESC			264

#define IMM_SHIFT				6	/* Immediate */
#define NOSV_SHIFT				7	/* No save */

struct be_res_desc_hdr {
	u8 desc_type;
	u8 desc_len;
} __packed;

struct be_port_res_desc {
	struct be_res_desc_hdr hdr;
	u8 rsvd0;
	u8 flags;
	u8 link_num;
	u8 mc_type;
	u16 rsvd1;

#define NV_TYPE_MASK				0x3	/* bits 0-1 */
#define NV_TYPE_DISABLED			1
#define NV_TYPE_VXLAN				3
#define SOCVID_SHIFT				2	/* Strip outer vlan */
#define RCVID_SHIFT				4	/* Report vlan */
	u8 nv_flags;
	u8 rsvd2;
	__le16 nv_port;					/* vxlan/gre port */
	u32 rsvd3[19];
} __packed;

struct be_pcie_res_desc {
	struct be_res_desc_hdr hdr;
	u8 rsvd0;
	u8 flags;
	u16 rsvd1;
	u8 pf_num;
	u8 rsvd2;
	u32 rsvd3;
	u8 sriov_state;
	u8 pf_state;
	u8 pf_type;
	u8 rsvd4;
	u16 num_vfs;
	u16 rsvd5;
	u32 rsvd6[17];
} __packed;

struct be_nic_res_desc {
	struct be_res_desc_hdr hdr;
	u8 rsvd1;

#define QUN_SHIFT				4 /* QoS is in absolute units */
	u8 flags;
	u8 vf_num;
	u8 rsvd2;
	u8 pf_num;
	u8 rsvd3;
	u16 unicast_mac_count;
	u8 rsvd4[6];
	u16 mcc_count;
	u16 vlan_count;
	u16 mcast_mac_count;
	u16 txq_count;
	u16 rq_count;
	u16 rssq_count;
	u16 lro_count;
	u16 cq_count;
	u16 toe_conn_count;
	u16 eq_count;
	u32 rsvd5;
	u32 cap_flags;
	u8 link_param;
	u8 rsvd6[3];
	u32 bw_min;
	u32 bw_max;
	u8 acpi_params;
	u8 wol_param;
	u16 rsvd7;
	u32 rsvd8[7];
} __packed;

/************ Multi-Channel type ***********/
enum mc_type {
	MC_NONE = 0x01,
	UMC = 0x02,
	FLEX10 = 0x03,
	vNIC1 = 0x04,
	nPAR = 0x05,
	UFP = 0x06,
	vNIC2 = 0x07
};

/* Is BE in a multi-channel mode */
static inline bool be_is_mc(struct be_adapter *adapter)
{
	return adapter->mc_type > MC_NONE;
}

struct be_cmd_req_get_func_config {
	struct be_cmd_req_hdr hdr;
};

struct be_cmd_resp_get_func_config {
	struct be_cmd_resp_hdr hdr;
	u32 desc_count;
	u8 func_param[MAX_RESOURCE_DESC * RESOURCE_DESC_SIZE_V1];
};

#define ACTIVE_PROFILE_TYPE			0x2
struct be_cmd_req_get_profile_config {
	struct be_cmd_req_hdr hdr;
	u8 rsvd;
	u8 type;
	u16 rsvd1;
};

struct be_cmd_resp_get_profile_config {
	struct be_cmd_resp_hdr hdr;
	u32 desc_count;
	u8 func_param[MAX_RESOURCE_DESC * RESOURCE_DESC_SIZE_V1];
};

struct be_cmd_req_set_profile_config {
	struct be_cmd_req_hdr hdr;
	u32 rsvd;
	u32 desc_count;
	u8 desc[RESOURCE_DESC_SIZE_V1];
};

struct be_cmd_resp_set_profile_config {
	struct be_cmd_resp_hdr hdr;
};

struct be_cmd_req_get_active_profile {
	struct be_cmd_req_hdr hdr;
	u32 rsvd;
} __packed;

struct be_cmd_resp_get_active_profile {
	struct be_cmd_resp_hdr hdr;
	u16 active_profile_id;
	u16 next_profile_id;
} __packed;

struct be_cmd_enable_disable_vf {
	struct be_cmd_req_hdr hdr;
	u8 enable;
	u8 rsvd[3];
};

struct be_cmd_req_intr_set {
	struct be_cmd_req_hdr hdr;
	u8 intr_enabled;
	u8 rsvd[3];
};

static inline bool check_privilege(struct be_adapter *adapter, u32 flags)
{
	return flags & adapter->cmd_privileges ? true : false;
}

/************** Get IFACE LIST *******************/
struct be_if_desc {
	u32 if_id;
	u32 cap_flags;
	u32 en_flags;
};

struct be_cmd_req_get_iface_list {
	struct be_cmd_req_hdr hdr;
};

struct be_cmd_resp_get_iface_list {
	struct be_cmd_req_hdr hdr;
	u32 if_cnt;
	struct be_if_desc if_desc;
};

/*************** Set logical link ********************/
#define PLINK_TRACK_SHIFT	8
struct be_cmd_req_set_ll_link {
	struct be_cmd_req_hdr hdr;
	u32 link_config; /* Bit 0: UP_DOWN, Bit 9: PLINK */
};

/************** Manage IFACE Filters *******************/
#define OP_CONVERT_NORMAL_TO_TUNNEL		0
#define OP_CONVERT_TUNNEL_TO_NORMAL		1

struct be_cmd_req_manage_iface_filters {
	struct be_cmd_req_hdr hdr;
	u8  op;
	u8  rsvd0;
	u8  flags;
	u8  rsvd1;
	u32 tunnel_iface_id;
	u32 target_iface_id;
	u8  mac[6];
	u16 vlan_tag;
	u32 tenant_id;
	u32 filter_id;
	u32 cap_flags;
	u32 cap_control_flags;
} __packed;

int be_pci_fnum_get(struct be_adapter *adapter);
int be_fw_wait_ready(struct be_adapter *adapter);
int be_cmd_mac_addr_query(struct be_adapter *adapter, u8 *mac_addr,
			  bool permanent, u32 if_handle, u32 pmac_id);
int be_cmd_pmac_add(struct be_adapter *adapter, u8 *mac_addr, u32 if_id,
		    u32 *pmac_id, u32 domain);
int be_cmd_pmac_del(struct be_adapter *adapter, u32 if_id, int pmac_id,
		    u32 domain);
int be_cmd_if_create(struct be_adapter *adapter, u32 cap_flags, u32 en_flags,
		     u32 *if_handle, u32 domain);
int be_cmd_if_destroy(struct be_adapter *adapter, int if_handle, u32 domain);
int be_cmd_eq_create(struct be_adapter *adapter, struct be_eq_obj *eqo);
int be_cmd_cq_create(struct be_adapter *adapter, struct be_queue_info *cq,
		     struct be_queue_info *eq, bool no_delay,
		     int num_cqe_dma_coalesce);
int be_cmd_mccq_create(struct be_adapter *adapter, struct be_queue_info *mccq,
		       struct be_queue_info *cq);
int be_cmd_txq_create(struct be_adapter *adapter, struct be_tx_obj *txo);
int be_cmd_rxq_create(struct be_adapter *adapter, struct be_queue_info *rxq,
		      u16 cq_id, u16 frag_size, u32 if_id, u32 rss, u8 *rss_id);
int be_cmd_q_destroy(struct be_adapter *adapter, struct be_queue_info *q,
		     int type);
int be_cmd_rxq_destroy(struct be_adapter *adapter, struct be_queue_info *q);
int be_cmd_link_status_query(struct be_adapter *adapter, u16 *link_speed,
			     u8 *link_status, u32 dom);
int be_cmd_reset(struct be_adapter *adapter);
int be_cmd_get_stats(struct be_adapter *adapter, struct be_dma_mem *nonemb_cmd);
int lancer_cmd_get_pport_stats(struct be_adapter *adapter,
			       struct be_dma_mem *nonemb_cmd);
int be_cmd_get_fw_ver(struct be_adapter *adapter, char *fw_ver,
		      char *fw_on_flash);
int be_cmd_modify_eqd(struct be_adapter *adapter, struct be_set_eqd *, int num);
int be_cmd_vlan_config(struct be_adapter *adapter, u32 if_id, u16 *vtag_array,
		       u32 num, bool promiscuous);
int be_cmd_rx_filter(struct be_adapter *adapter, u32 flags, u32 status);
int be_cmd_set_flow_control(struct be_adapter *adapter, u32 tx_fc, u32 rx_fc);
int be_cmd_get_flow_control(struct be_adapter *adapter, u32 *tx_fc, u32 *rx_fc);
int be_cmd_query_fw_cfg(struct be_adapter *adapter, u32 *port_num,
			u32 *function_mode, u32 *function_caps, u16 *asic_rev);
int be_cmd_reset_function(struct be_adapter *adapter);
int be_cmd_rss_config(struct be_adapter *adapter, u8 *rsstable,
		      u32 rss_hash_opts, u16 table_size);
int be_process_mcc(struct be_adapter *adapter);
int be_cmd_set_beacon_state(struct be_adapter *adapter, u8 port_num, u8 beacon,
			    u8 status, u8 state);
int be_cmd_get_beacon_state(struct be_adapter *adapter, u8 port_num,
			    u32 *state);
int be_cmd_write_flashrom(struct be_adapter *adapter, struct be_dma_mem *cmd,
			  u32 flash_oper, u32 flash_opcode, u32 buf_size);
int lancer_cmd_write_object(struct be_adapter *adapter, struct be_dma_mem *cmd,
			    u32 data_size, u32 data_offset,
			    const char *obj_name, u32 *data_written,
			    u8 *change_status, u8 *addn_status);
int lancer_cmd_read_object(struct be_adapter *adapter, struct be_dma_mem *cmd,
			   u32 data_size, u32 data_offset, const char *obj_name,
			   u32 *data_read, u32 *eof, u8 *addn_status);
int be_cmd_get_flash_crc(struct be_adapter *adapter, u8 *flashed_crc,
			 int offset);
int be_cmd_enable_magic_wol(struct be_adapter *adapter, u8 *mac,
			    struct be_dma_mem *nonemb_cmd);
int be_cmd_fw_init(struct be_adapter *adapter);
int be_cmd_fw_clean(struct be_adapter *adapter);
void be_async_mcc_enable(struct be_adapter *adapter);
void be_async_mcc_disable(struct be_adapter *adapter);
int be_cmd_loopback_test(struct be_adapter *adapter, u32 port_num,
			 u32 loopback_type, u32 pkt_size, u32 num_pkts,
			 u64 pattern);
int be_cmd_ddr_dma_test(struct be_adapter *adapter, u64 pattern, u32 byte_cnt,
			struct be_dma_mem *cmd);
int be_cmd_get_seeprom_data(struct be_adapter *adapter,
			    struct be_dma_mem *nonemb_cmd);
int be_cmd_set_loopback(struct be_adapter *adapter, u8 port_num,
			u8 loopback_type, u8 enable);
int be_cmd_get_phy_info(struct be_adapter *adapter);
int be_cmd_config_qos(struct be_adapter *adapter, u32 bps, u8 domain);
void be_detect_error(struct be_adapter *adapter);
int be_cmd_get_die_temperature(struct be_adapter *adapter);
int be_cmd_get_cntl_attributes(struct be_adapter *adapter);
int be_cmd_req_native_mode(struct be_adapter *adapter);
int be_cmd_get_reg_len(struct be_adapter *adapter, u32 *log_size);
void be_cmd_get_regs(struct be_adapter *adapter, u32 buf_len, void *buf);
int be_cmd_get_fn_privileges(struct be_adapter *adapter, u32 *privilege,
			     u32 domain);
int be_cmd_set_fn_privileges(struct be_adapter *adapter, u32 privileges,
			     u32 vf_num);
int be_cmd_get_mac_from_list(struct be_adapter *adapter, u8 *mac,
			     bool *pmac_id_active, u32 *pmac_id,
			     u32 if_handle, u8 domain);
int be_cmd_get_active_mac(struct be_adapter *adapter, u32 pmac_id, u8 *mac,
			  u32 if_handle, bool active, u32 domain);
int be_cmd_get_perm_mac(struct be_adapter *adapter, u8 *mac);
int be_cmd_set_mac_list(struct be_adapter *adapter, u8 *mac_array, u8 mac_count,
			u32 domain);
int be_cmd_set_mac(struct be_adapter *adapter, u8 *mac, int if_id, u32 dom);
int be_cmd_set_hsw_config(struct be_adapter *adapter, u16 pvid, u32 domain,
			  u16 intf_id, u16 hsw_mode);
int be_cmd_get_hsw_config(struct be_adapter *adapter, u16 *pvid, u32 domain,
			  u16 intf_id, u8 *mode);
int be_cmd_get_acpi_wol_cap(struct be_adapter *adapter);
int be_cmd_set_fw_log_level(struct be_adapter *adapter, u32 level);
int be_cmd_get_fw_log_level(struct be_adapter *adapter);
int be_cmd_get_ext_fat_capabilites(struct be_adapter *adapter,
				   struct be_dma_mem *cmd);
int be_cmd_set_ext_fat_capabilites(struct be_adapter *adapter,
				   struct be_dma_mem *cmd,
				   struct be_fat_conf_params *cfgs);
int lancer_physdev_ctrl(struct be_adapter *adapter, u32 mask);
int lancer_initiate_dump(struct be_adapter *adapter);
bool dump_present(struct be_adapter *adapter);
int lancer_test_and_set_rdy_state(struct be_adapter *adapter);
int be_cmd_query_port_name(struct be_adapter *adapter, u8 *port_name);
int be_cmd_get_func_config(struct be_adapter *adapter,
			   struct be_resources *res);
int be_cmd_get_profile_config(struct be_adapter *adapter,
			      struct be_resources *res, u8 domain);
int be_cmd_set_profile_config(struct be_adapter *adapter, void *desc,
			      int size, u8 version, u8 domain);
int be_cmd_get_active_profile(struct be_adapter *adapter, u16 *profile);
int be_cmd_get_if_id(struct be_adapter *adapter, struct be_vf_cfg *vf_cfg,
		     int vf_num);
int be_cmd_enable_vf(struct be_adapter *adapter, u8 domain);
int be_cmd_intr_set(struct be_adapter *adapter, bool intr_enable);
int be_cmd_set_logical_link_config(struct be_adapter *adapter,
					  int link_state, u8 domain);
int be_cmd_set_vxlan_port(struct be_adapter *adapter, __be16 port);
int be_cmd_manage_iface(struct be_adapter *adapter, u32 iface, u8 op);
