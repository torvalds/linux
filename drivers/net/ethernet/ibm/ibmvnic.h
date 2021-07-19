/* SPDX-License-Identifier: GPL-2.0-or-later */
/**************************************************************************/
/*                                                                        */
/*  IBM System i and System p Virtual NIC Device Driver                   */
/*  Copyright (C) 2014 IBM Corp.                                          */
/*  Santiago Leon (santi_leon@yahoo.com)                                  */
/*  Thomas Falcon (tlfalcon@linux.vnet.ibm.com)                           */
/*  John Allen (jallen@linux.vnet.ibm.com)                                */
/*                                                                        */
/*                                                                        */
/* This module contains the implementation of a virtual ethernet device   */
/* for use with IBM i/pSeries LPAR Linux.  It utilizes the logical LAN    */
/* option of the RS/6000 Platform Architecture to interface with virtual */
/* ethernet NICs that are presented to the partition by the hypervisor.   */
/*                                                                        */
/**************************************************************************/

#define IBMVNIC_NAME		"ibmvnic"
#define IBMVNIC_DRIVER_VERSION	"1.0.1"
#define IBMVNIC_INVALID_MAP	-1
#define IBMVNIC_STATS_TIMEOUT	1
#define IBMVNIC_INIT_FAILED	2
#define IBMVNIC_OPEN_FAILED	3

/* basic structures plus 100 2k buffers */
#define IBMVNIC_IO_ENTITLEMENT_DEFAULT	610305

/* Initial module_parameters */
#define IBMVNIC_RX_WEIGHT		16
/* when changing this, update IBMVNIC_IO_ENTITLEMENT_DEFAULT */
#define IBMVNIC_BUFFS_PER_POOL	100
#define IBMVNIC_MAX_QUEUES	16
#define IBMVNIC_MAX_QUEUE_SZ   4096
#define IBMVNIC_MAX_IND_DESCS  16
#define IBMVNIC_IND_ARR_SZ	(IBMVNIC_MAX_IND_DESCS * 32)

#define IBMVNIC_TSO_BUF_SZ	65536
#define IBMVNIC_TSO_BUFS	64
#define IBMVNIC_TSO_POOL_MASK	0x80000000

#define IBMVNIC_MAX_LTB_SIZE ((1 << (MAX_ORDER - 1)) * PAGE_SIZE)
#define IBMVNIC_BUFFER_HLEN 500

#define IBMVNIC_RESET_DELAY 100

static const char ibmvnic_priv_flags[][ETH_GSTRING_LEN] = {
#define IBMVNIC_USE_SERVER_MAXES 0x1
	"use-server-maxes"
};

struct ibmvnic_login_buffer {
	__be32 len;
	__be32 version;
#define INITIAL_VERSION_LB 1
	__be32 num_txcomp_subcrqs;
	__be32 off_txcomp_subcrqs;
	__be32 num_rxcomp_subcrqs;
	__be32 off_rxcomp_subcrqs;
	__be32 login_rsp_ioba;
	__be32 login_rsp_len;
	__be32 client_data_offset;
	__be32 client_data_len;
} __packed __aligned(8);

struct ibmvnic_login_rsp_buffer {
	__be32 len;
	__be32 version;
#define INITIAL_VERSION_LRB 1
	__be32 num_txsubm_subcrqs;
	__be32 off_txsubm_subcrqs;
	__be32 num_rxadd_subcrqs;
	__be32 off_rxadd_subcrqs;
	__be32 off_rxadd_buff_size;
	__be32 num_supp_tx_desc;
	__be32 off_supp_tx_desc;
} __packed __aligned(8);

struct ibmvnic_query_ip_offload_buffer {
	__be32 len;
	__be32 version;
#define INITIAL_VERSION_IOB 1
	u8 ipv4_chksum;
	u8 ipv6_chksum;
	u8 tcp_ipv4_chksum;
	u8 tcp_ipv6_chksum;
	u8 udp_ipv4_chksum;
	u8 udp_ipv6_chksum;
	u8 large_tx_ipv4;
	u8 large_tx_ipv6;
	u8 large_rx_ipv4;
	u8 large_rx_ipv6;
	u8 reserved1[14];
	__be16 max_ipv4_header_size;
	__be16 max_ipv6_header_size;
	__be16 max_tcp_header_size;
	__be16 max_udp_header_size;
	__be32 max_large_tx_size;
	__be32 max_large_rx_size;
	u8 reserved2[16];
	u8 ipv6_extension_header;
#define IPV6_EH_NOT_SUPPORTED	0x00
#define IPV6_EH_SUPPORTED_LIM	0x01
#define IPV6_EH_SUPPORTED	0xFF
	u8 tcp_pseudosum_req;
#define TCP_PS_NOT_REQUIRED	0x00
#define TCP_PS_REQUIRED		0x01
	u8 reserved3[30];
	__be16 num_ipv6_ext_headers;
	__be32 off_ipv6_ext_headers;
	u8 reserved4[154];
} __packed __aligned(8);

struct ibmvnic_control_ip_offload_buffer {
	__be32 len;
	__be32 version;
#define INITIAL_VERSION_IOB 1
	u8 ipv4_chksum;
	u8 ipv6_chksum;
	u8 tcp_ipv4_chksum;
	u8 tcp_ipv6_chksum;
	u8 udp_ipv4_chksum;
	u8 udp_ipv6_chksum;
	u8 large_tx_ipv4;
	u8 large_tx_ipv6;
	u8 bad_packet_rx;
	u8 large_rx_ipv4;
	u8 large_rx_ipv6;
	u8 reserved4[111];
} __packed __aligned(8);

struct ibmvnic_fw_component {
	u8 name[48];
	__be32 trace_buff_size;
	u8 correlator;
	u8 trace_level;
	u8 parent_correlator;
	u8 error_check_level;
	u8 trace_on;
	u8 reserved[7];
	u8 description[192];
} __packed __aligned(8);

struct ibmvnic_fw_trace_entry {
	__be32 trace_id;
	u8 num_valid_data;
	u8 reserved[3];
	__be64 pmc_registers;
	__be64 timebase;
	__be64 trace_data[5];
} __packed __aligned(8);

struct ibmvnic_statistics {
	__be32 version;
	__be32 promiscuous;
	__be64 rx_packets;
	__be64 rx_bytes;
	__be64 tx_packets;
	__be64 tx_bytes;
	__be64 ucast_tx_packets;
	__be64 ucast_rx_packets;
	__be64 mcast_tx_packets;
	__be64 mcast_rx_packets;
	__be64 bcast_tx_packets;
	__be64 bcast_rx_packets;
	__be64 align_errors;
	__be64 fcs_errors;
	__be64 single_collision_frames;
	__be64 multi_collision_frames;
	__be64 sqe_test_errors;
	__be64 deferred_tx;
	__be64 late_collisions;
	__be64 excess_collisions;
	__be64 internal_mac_tx_errors;
	__be64 carrier_sense;
	__be64 too_long_frames;
	__be64 internal_mac_rx_errors;
	u8 reserved[72];
} __packed __aligned(8);

#define NUM_TX_STATS 3
struct ibmvnic_tx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 dropped_packets;
};

#define NUM_RX_STATS 3
struct ibmvnic_rx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 interrupts;
};

struct ibmvnic_acl_buffer {
	__be32 len;
	__be32 version;
#define INITIAL_VERSION_IOB 1
	u8 mac_acls_restrict;
	u8 vlan_acls_restrict;
	u8 reserved1[22];
	__be32 num_mac_addrs;
	__be32 offset_mac_addrs;
	__be32 num_vlan_ids;
	__be32 offset_vlan_ids;
	u8 reserved2[80];
} __packed __aligned(8);

/* descriptors have been changed, how should this be defined?  1? 4? */

#define IBMVNIC_TX_DESC_VERSIONS 3

/* is this still needed? */
struct ibmvnic_tx_comp_desc {
	u8 first;
	u8 num_comps;
	__be16 rcs[5];
	__be32 correlators[5];
} __packed __aligned(8);

/* some flags that included in v0 descriptor, which is gone
 * only used for IBMVNIC_TCP_CHKSUM and IBMVNIC_UDP_CHKSUM
 * and only in some offload_flags variable that doesn't seem
 * to be used anywhere, can probably be removed?
 */

#define IBMVNIC_TCP_CHKSUM		0x20
#define IBMVNIC_UDP_CHKSUM		0x08

struct ibmvnic_tx_desc {
	u8 first;
	u8 type;

#define IBMVNIC_TX_DESC 0x10
	u8 n_crq_elem;
	u8 n_sge;
	u8 flags1;
#define IBMVNIC_TX_COMP_NEEDED		0x80
#define IBMVNIC_TX_CHKSUM_OFFLOAD	0x40
#define IBMVNIC_TX_LSO			0x20
#define IBMVNIC_TX_PROT_TCP		0x10
#define IBMVNIC_TX_PROT_UDP		0x08
#define IBMVNIC_TX_PROT_IPV4		0x04
#define IBMVNIC_TX_PROT_IPV6		0x02
#define IBMVNIC_TX_VLAN_PRESENT		0x01
	u8 flags2;
#define IBMVNIC_TX_VLAN_INSERT		0x80
	__be16 mss;
	u8 reserved[4];
	__be32 correlator;
	__be16 vlan_id;
	__be16 dma_reg;
	__be32 sge_len;
	__be64 ioba;
} __packed __aligned(8);

struct ibmvnic_hdr_desc {
	u8 first;
	u8 type;
#define IBMVNIC_HDR_DESC		0x11
	u8 len;
	u8 l2_len;
	__be16 l3_len;
	u8 l4_len;
	u8 flag;
	u8 data[24];
} __packed __aligned(8);

struct ibmvnic_hdr_ext_desc {
	u8 first;
	u8 type;
#define IBMVNIC_HDR_EXT_DESC		0x12
	u8 len;
	u8 data[29];
} __packed __aligned(8);

struct ibmvnic_sge_desc {
	u8 first;
	u8 type;
#define IBMVNIC_SGE_DESC		0x30
	__be16 sge1_dma_reg;
	__be32 sge1_len;
	__be64 sge1_ioba;
	__be16 reserved;
	__be16 sge2_dma_reg;
	__be32 sge2_len;
	__be64 sge2_ioba;
} __packed __aligned(8);

struct ibmvnic_rx_comp_desc {
	u8 first;
	u8 flags;
#define IBMVNIC_IP_CHKSUM_GOOD		0x80
#define IBMVNIC_TCP_UDP_CHKSUM_GOOD	0x40
#define IBMVNIC_END_FRAME			0x20
#define IBMVNIC_EXACT_MC			0x10
#define IBMVNIC_VLAN_STRIPPED			0x08
	__be16 off_frame_data;
	__be32 len;
	__be64 correlator;
	__be16 vlan_tci;
	__be16 rc;
	u8 reserved[12];
} __packed __aligned(8);

struct ibmvnic_generic_scrq {
	u8 first;
	u8 reserved[31];
} __packed __aligned(8);

struct ibmvnic_rx_buff_add_desc {
	u8 first;
	u8 reserved[7];
	__be64 correlator;
	__be32 ioba;
	u8 map_id;
	__be32 len:24;
	u8 reserved2[8];
} __packed __aligned(8);

struct ibmvnic_rc {
	u8 code; /* one of enum ibmvnic_rc_codes */
	u8 detailed_data[3];
} __packed __aligned(4);

struct ibmvnic_generic_crq {
	u8 first;
	u8 cmd;
	u8 params[10];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_version_exchange {
	u8 first;
	u8 cmd;
	__be16 version;
#define IBMVNIC_INITIAL_VERSION 1
	u8 reserved[8];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_capability {
	u8 first;
	u8 cmd;
	__be16 capability; /* one of ibmvnic_capabilities */
	__be64 number;
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_login {
	u8 first;
	u8 cmd;
	u8 reserved[6];
	__be32 ioba;
	__be32 len;
} __packed __aligned(8);

struct ibmvnic_phys_parms {
	u8 first;
	u8 cmd;
	u8 flags1;
#define IBMVNIC_EXTERNAL_LOOPBACK	0x80
#define IBMVNIC_INTERNAL_LOOPBACK	0x40
#define IBMVNIC_PROMISC		0x20
#define IBMVNIC_PHYS_LINK_ACTIVE	0x10
#define IBMVNIC_AUTONEG_DUPLEX	0x08
#define IBMVNIC_FULL_DUPLEX	0x04
#define IBMVNIC_HALF_DUPLEX	0x02
#define IBMVNIC_CAN_CHG_PHYS_PARMS	0x01
	u8 flags2;
#define IBMVNIC_LOGICAL_LNK_ACTIVE 0x80
	__be32 speed;
#define IBMVNIC_AUTONEG		0x80000000
#define IBMVNIC_10MBPS		0x40000000
#define IBMVNIC_100MBPS		0x20000000
#define IBMVNIC_1GBPS		0x10000000
#define IBMVNIC_10GBPS		0x08000000
#define IBMVNIC_40GBPS		0x04000000
#define IBMVNIC_100GBPS		0x02000000
#define IBMVNIC_25GBPS		0x01000000
#define IBMVNIC_50GBPS		0x00800000
#define IBMVNIC_200GBPS		0x00400000
	__be32 mtu;
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_logical_link_state {
	u8 first;
	u8 cmd;
	u8 link_state;
#define IBMVNIC_LOGICAL_LNK_DN 0x00
#define IBMVNIC_LOGICAL_LNK_UP 0x01
#define IBMVNIC_LOGICAL_LNK_QUERY 0xff
	u8 reserved[9];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_query_ip_offload {
	u8 first;
	u8 cmd;
	u8 reserved[2];
	__be32 len;
	__be32 ioba;
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_control_ip_offload {
	u8 first;
	u8 cmd;
	u8 reserved[2];
	__be32 ioba;
	__be32 len;
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_request_statistics {
	u8 first;
	u8 cmd;
	u8 flags;
#define IBMVNIC_PHYSICAL_PORT	0x80
	u8 reserved1;
	__be32 ioba;
	__be32 len;
	u8 reserved[4];
} __packed __aligned(8);

struct ibmvnic_error_indication {
	u8 first;
	u8 cmd;
	u8 flags;
#define IBMVNIC_FATAL_ERROR	0x80
	u8 reserved1;
	__be32 error_id;
	__be32 detail_error_sz;
	__be16 error_cause;
	u8 reserved2[2];
} __packed __aligned(8);

struct ibmvnic_link_state_indication {
	u8 first;
	u8 cmd;
	u8 reserved1[2];
	u8 phys_link_state;
	u8 logical_link_state;
	u8 reserved2[10];
} __packed __aligned(8);

struct ibmvnic_change_mac_addr {
	u8 first;
	u8 cmd;
	u8 mac_addr[6];
	u8 reserved[4];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_multicast_ctrl {
	u8 first;
	u8 cmd;
	u8 mac_addr[6];
	u8 flags;
#define IBMVNIC_ENABLE_MC		0x80
#define IBMVNIC_DISABLE_MC		0x40
#define IBMVNIC_ENABLE_ALL		0x20
#define IBMVNIC_DISABLE_ALL	0x10
	u8 reserved1;
	__be16 reserved2; /* was num_enabled_mc_addr; */
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_get_vpd_size {
	u8 first;
	u8 cmd;
	u8 reserved[14];
} __packed __aligned(8);

struct ibmvnic_get_vpd_size_rsp {
	u8 first;
	u8 cmd;
	u8 reserved[2];
	__be64 len;
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_get_vpd {
	u8 first;
	u8 cmd;
	u8 reserved1[2];
	__be32 ioba;
	__be32 len;
	u8 reserved[4];
} __packed __aligned(8);

struct ibmvnic_get_vpd_rsp {
	u8 first;
	u8 cmd;
	u8 reserved[10];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_acl_change_indication {
	u8 first;
	u8 cmd;
	__be16 change_type;
#define IBMVNIC_MAC_ACL 0
#define IBMVNIC_VLAN_ACL 1
	u8 reserved[12];
} __packed __aligned(8);

struct ibmvnic_acl_query {
	u8 first;
	u8 cmd;
	u8 reserved1[2];
	__be32 ioba;
	__be32 len;
	u8 reserved2[4];
} __packed __aligned(8);

struct ibmvnic_tune {
	u8 first;
	u8 cmd;
	u8 reserved1[2];
	__be32 ioba;
	__be32 len;
	u8 reserved2[4];
} __packed __aligned(8);

struct ibmvnic_request_map {
	u8 first;
	u8 cmd;
	u8 reserved1;
	u8 map_id;
	__be32 ioba;
	__be32 len;
	u8 reserved2[4];
} __packed __aligned(8);

struct ibmvnic_request_map_rsp {
	u8 first;
	u8 cmd;
	u8 reserved1;
	u8 map_id;
	u8 reserved2[8];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_request_unmap {
	u8 first;
	u8 cmd;
	u8 reserved1;
	u8 map_id;
	u8 reserved2[12];
} __packed __aligned(8);

struct ibmvnic_request_unmap_rsp {
	u8 first;
	u8 cmd;
	u8 reserved1;
	u8 map_id;
	u8 reserved2[8];
	struct ibmvnic_rc rc;
} __packed __aligned(8);

struct ibmvnic_query_map {
	u8 first;
	u8 cmd;
	u8 reserved[14];
} __packed __aligned(8);

struct ibmvnic_query_map_rsp {
	u8 first;
	u8 cmd;
	u8 reserved;
	u8 page_size;
	__be32 tot_pages;
	__be32 free_pages;
	struct ibmvnic_rc rc;
} __packed __aligned(8);

union ibmvnic_crq {
	struct ibmvnic_generic_crq generic;
	struct ibmvnic_version_exchange version_exchange;
	struct ibmvnic_version_exchange version_exchange_rsp;
	struct ibmvnic_capability query_capability;
	struct ibmvnic_capability query_capability_rsp;
	struct ibmvnic_capability request_capability;
	struct ibmvnic_capability request_capability_rsp;
	struct ibmvnic_login login;
	struct ibmvnic_generic_crq login_rsp;
	struct ibmvnic_phys_parms query_phys_parms;
	struct ibmvnic_phys_parms query_phys_parms_rsp;
	struct ibmvnic_phys_parms query_phys_capabilities;
	struct ibmvnic_phys_parms query_phys_capabilities_rsp;
	struct ibmvnic_phys_parms set_phys_parms;
	struct ibmvnic_phys_parms set_phys_parms_rsp;
	struct ibmvnic_logical_link_state logical_link_state;
	struct ibmvnic_logical_link_state logical_link_state_rsp;
	struct ibmvnic_query_ip_offload query_ip_offload;
	struct ibmvnic_query_ip_offload query_ip_offload_rsp;
	struct ibmvnic_control_ip_offload control_ip_offload;
	struct ibmvnic_control_ip_offload control_ip_offload_rsp;
	struct ibmvnic_request_statistics request_statistics;
	struct ibmvnic_generic_crq request_statistics_rsp;
	struct ibmvnic_error_indication error_indication;
	struct ibmvnic_link_state_indication link_state_indication;
	struct ibmvnic_change_mac_addr change_mac_addr;
	struct ibmvnic_change_mac_addr change_mac_addr_rsp;
	struct ibmvnic_multicast_ctrl multicast_ctrl;
	struct ibmvnic_multicast_ctrl multicast_ctrl_rsp;
	struct ibmvnic_get_vpd_size get_vpd_size;
	struct ibmvnic_get_vpd_size_rsp get_vpd_size_rsp;
	struct ibmvnic_get_vpd get_vpd;
	struct ibmvnic_get_vpd_rsp get_vpd_rsp;
	struct ibmvnic_acl_change_indication acl_change_indication;
	struct ibmvnic_acl_query acl_query;
	struct ibmvnic_generic_crq acl_query_rsp;
	struct ibmvnic_tune tune;
	struct ibmvnic_generic_crq tune_rsp;
	struct ibmvnic_request_map request_map;
	struct ibmvnic_request_map_rsp request_map_rsp;
	struct ibmvnic_request_unmap request_unmap;
	struct ibmvnic_request_unmap_rsp request_unmap_rsp;
	struct ibmvnic_query_map query_map;
	struct ibmvnic_query_map_rsp query_map_rsp;
};

enum ibmvnic_rc_codes {
	SUCCESS = 0,
	PARTIALSUCCESS = 1,
	PERMISSION = 2,
	NOMEMORY = 3,
	PARAMETER = 4,
	UNKNOWNCOMMAND = 5,
	ABORTED = 6,
	INVALIDSTATE = 7,
	INVALIDIOBA = 8,
	INVALIDLENGTH = 9,
	UNSUPPORTEDOPTION = 10,
};

enum ibmvnic_capabilities {
	MIN_TX_QUEUES = 1,
	MIN_RX_QUEUES = 2,
	MIN_RX_ADD_QUEUES = 3,
	MAX_TX_QUEUES = 4,
	MAX_RX_QUEUES = 5,
	MAX_RX_ADD_QUEUES = 6,
	REQ_TX_QUEUES = 7,
	REQ_RX_QUEUES = 8,
	REQ_RX_ADD_QUEUES = 9,
	MIN_TX_ENTRIES_PER_SUBCRQ = 10,
	MIN_RX_ADD_ENTRIES_PER_SUBCRQ = 11,
	MAX_TX_ENTRIES_PER_SUBCRQ = 12,
	MAX_RX_ADD_ENTRIES_PER_SUBCRQ = 13,
	REQ_TX_ENTRIES_PER_SUBCRQ = 14,
	REQ_RX_ADD_ENTRIES_PER_SUBCRQ = 15,
	TCP_IP_OFFLOAD = 16,
	PROMISC_REQUESTED = 17,
	PROMISC_SUPPORTED = 18,
	MIN_MTU = 19,
	MAX_MTU = 20,
	REQ_MTU = 21,
	MAX_MULTICAST_FILTERS = 22,
	VLAN_HEADER_INSERTION = 23,
	RX_VLAN_HEADER_INSERTION = 24,
	MAX_TX_SG_ENTRIES = 25,
	RX_SG_SUPPORTED = 26,
	RX_SG_REQUESTED = 27,
	OPT_TX_COMP_SUB_QUEUES = 28,
	OPT_RX_COMP_QUEUES = 29,
	OPT_RX_BUFADD_Q_PER_RX_COMP_Q = 30,
	OPT_TX_ENTRIES_PER_SUBCRQ = 31,
	OPT_RXBA_ENTRIES_PER_SUBCRQ = 32,
	TX_RX_DESC_REQ = 33,
};

enum ibmvnic_error_cause {
	ADAPTER_PROBLEM = 0,
	BUS_PROBLEM = 1,
	FW_PROBLEM = 2,
	DD_PROBLEM = 3,
	EEH_RECOVERY = 4,
	FW_UPDATED = 5,
	LOW_MEMORY = 6,
};

enum ibmvnic_commands {
	VERSION_EXCHANGE = 0x01,
	VERSION_EXCHANGE_RSP = 0x81,
	QUERY_CAPABILITY = 0x02,
	QUERY_CAPABILITY_RSP = 0x82,
	REQUEST_CAPABILITY = 0x03,
	REQUEST_CAPABILITY_RSP = 0x83,
	LOGIN = 0x04,
	LOGIN_RSP = 0x84,
	QUERY_PHYS_PARMS = 0x05,
	QUERY_PHYS_PARMS_RSP = 0x85,
	QUERY_PHYS_CAPABILITIES = 0x06,
	QUERY_PHYS_CAPABILITIES_RSP = 0x86,
	SET_PHYS_PARMS = 0x07,
	SET_PHYS_PARMS_RSP = 0x87,
	ERROR_INDICATION = 0x08,
	LOGICAL_LINK_STATE = 0x0C,
	LOGICAL_LINK_STATE_RSP = 0x8C,
	REQUEST_STATISTICS = 0x0D,
	REQUEST_STATISTICS_RSP = 0x8D,
	COLLECT_FW_TRACE = 0x11,
	COLLECT_FW_TRACE_RSP = 0x91,
	LINK_STATE_INDICATION = 0x12,
	CHANGE_MAC_ADDR = 0x13,
	CHANGE_MAC_ADDR_RSP = 0x93,
	MULTICAST_CTRL = 0x14,
	MULTICAST_CTRL_RSP = 0x94,
	GET_VPD_SIZE = 0x15,
	GET_VPD_SIZE_RSP = 0x95,
	GET_VPD = 0x16,
	GET_VPD_RSP = 0x96,
	TUNE = 0x17,
	TUNE_RSP = 0x97,
	QUERY_IP_OFFLOAD = 0x18,
	QUERY_IP_OFFLOAD_RSP = 0x98,
	CONTROL_IP_OFFLOAD = 0x19,
	CONTROL_IP_OFFLOAD_RSP = 0x99,
	ACL_CHANGE_INDICATION = 0x1A,
	ACL_QUERY = 0x1B,
	ACL_QUERY_RSP = 0x9B,
	QUERY_MAP = 0x1D,
	QUERY_MAP_RSP = 0x9D,
	REQUEST_MAP = 0x1E,
	REQUEST_MAP_RSP = 0x9E,
	REQUEST_UNMAP = 0x1F,
	REQUEST_UNMAP_RSP = 0x9F,
	VLAN_CTRL = 0x20,
	VLAN_CTRL_RSP = 0xA0,
};

enum ibmvnic_crq_type {
	IBMVNIC_CRQ_CMD			= 0x80,
	IBMVNIC_CRQ_CMD_RSP		= 0x80,
	IBMVNIC_CRQ_INIT_CMD		= 0xC0,
	IBMVNIC_CRQ_INIT_RSP		= 0xC0,
	IBMVNIC_CRQ_XPORT_EVENT		= 0xFF,
};

enum ibmvfc_crq_format {
	IBMVNIC_CRQ_INIT                 = 0x01,
	IBMVNIC_CRQ_INIT_COMPLETE        = 0x02,
	IBMVNIC_PARTITION_MIGRATED       = 0x06,
	IBMVNIC_DEVICE_FAILOVER          = 0x08,
};

struct ibmvnic_crq_queue {
	union ibmvnic_crq *msgs;
	int size, cur;
	dma_addr_t msg_token;
	/* Used for serialization of msgs, cur */
	spinlock_t lock;
	bool active;
	char name[32];
};

union sub_crq {
	struct ibmvnic_generic_scrq generic;
	struct ibmvnic_tx_comp_desc tx_comp;
	struct ibmvnic_tx_desc v1;
	struct ibmvnic_hdr_desc hdr;
	struct ibmvnic_hdr_ext_desc hdr_ext;
	struct ibmvnic_sge_desc sge;
	struct ibmvnic_rx_comp_desc rx_comp;
	struct ibmvnic_rx_buff_add_desc rx_add;
};

struct ibmvnic_ind_xmit_queue {
	union sub_crq *indir_arr;
	dma_addr_t indir_dma;
	int index;
};

struct ibmvnic_sub_crq_queue {
	union sub_crq *msgs;
	int size, cur;
	dma_addr_t msg_token;
	unsigned long crq_num;
	unsigned long hw_irq;
	unsigned int irq;
	unsigned int pool_index;
	int scrq_num;
	/* Used for serialization of msgs, cur */
	spinlock_t lock;
	struct sk_buff *rx_skb_top;
	struct ibmvnic_adapter *adapter;
	struct ibmvnic_ind_xmit_queue ind_buf;
	atomic_t used;
	char name[32];
	u64 handle;
} ____cacheline_aligned;

struct ibmvnic_long_term_buff {
	unsigned char *buff;
	dma_addr_t addr;
	u64 size;
	u8 map_id;
};

struct ibmvnic_tx_buff {
	struct sk_buff *skb;
	int index;
	int pool_index;
	int num_entries;
};

struct ibmvnic_tx_pool {
	struct ibmvnic_tx_buff *tx_buff;
	int *free_map;
	int consumer_index;
	int producer_index;
	struct ibmvnic_long_term_buff long_term_buff;
	int num_buffers;
	int buf_size;
} ____cacheline_aligned;

struct ibmvnic_rx_buff {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned char *data;
	int size;
	int pool_index;
};

struct ibmvnic_rx_pool {
	struct ibmvnic_rx_buff *rx_buff;
	int size;
	int index;
	int buff_size;
	atomic_t available;
	int *free_map;
	int next_free;
	int next_alloc;
	int active;
	struct ibmvnic_long_term_buff long_term_buff;
} ____cacheline_aligned;

struct ibmvnic_vpd {
	unsigned char *buff;
	dma_addr_t dma_addr;
	u64 len;
};

enum vnic_state {VNIC_PROBING = 1,
		 VNIC_PROBED,
		 VNIC_OPENING,
		 VNIC_OPEN,
		 VNIC_CLOSING,
		 VNIC_CLOSED,
		 VNIC_REMOVING,
		 VNIC_REMOVED};

enum ibmvnic_reset_reason {VNIC_RESET_FAILOVER = 1,
			   VNIC_RESET_MOBILITY,
			   VNIC_RESET_FATAL,
			   VNIC_RESET_NON_FATAL,
			   VNIC_RESET_TIMEOUT,
			   VNIC_RESET_CHANGE_PARAM};

struct ibmvnic_rwi {
	enum ibmvnic_reset_reason reset_reason;
	struct list_head list;
};

struct ibmvnic_tunables {
	u64 rx_queues;
	u64 tx_queues;
	u64 rx_entries;
	u64 tx_entries;
	u64 mtu;
};

struct ibmvnic_adapter {
	struct vio_dev *vdev;
	struct net_device *netdev;
	struct ibmvnic_crq_queue crq;
	u8 mac_addr[ETH_ALEN];
	struct ibmvnic_query_ip_offload_buffer ip_offload_buf;
	dma_addr_t ip_offload_tok;
	struct ibmvnic_control_ip_offload_buffer ip_offload_ctrl;
	dma_addr_t ip_offload_ctrl_tok;
	u32 msg_enable;
	u32 priv_flags;

	/* Vital Product Data (VPD) */
	struct ibmvnic_vpd *vpd;
	char fw_version[32];

	/* Statistics */
	struct ibmvnic_statistics stats;
	dma_addr_t stats_token;
	struct completion stats_done;
	int replenish_no_mem;
	int replenish_add_buff_success;
	int replenish_add_buff_failure;
	int replenish_task_cycles;
	int tx_send_failed;
	int tx_map_failed;

	struct ibmvnic_tx_queue_stats *tx_stats_buffers;
	struct ibmvnic_rx_queue_stats *rx_stats_buffers;

	int phys_link_state;
	int logical_link_state;

	u32 speed;
	u8 duplex;

	/* login data */
	struct ibmvnic_login_buffer *login_buf;
	dma_addr_t login_buf_token;
	int login_buf_sz;

	struct ibmvnic_login_rsp_buffer *login_rsp_buf;
	dma_addr_t login_rsp_buf_token;
	int login_rsp_buf_sz;

	atomic_t running_cap_crqs;
	bool wait_capability;

	struct ibmvnic_sub_crq_queue **tx_scrq ____cacheline_aligned;
	struct ibmvnic_sub_crq_queue **rx_scrq ____cacheline_aligned;

	/* rx structs */
	struct napi_struct *napi;
	struct ibmvnic_rx_pool *rx_pool;
	u64 promisc;

	struct ibmvnic_tx_pool *tx_pool;
	struct ibmvnic_tx_pool *tso_pool;
	struct completion init_done;
	int init_done_rc;

	struct completion fw_done;
	/* Used for serialization of device commands */
	struct mutex fw_lock;
	int fw_done_rc;

	struct completion reset_done;
	int reset_done_rc;
	bool wait_for_reset;

	/* partner capabilities */
	u64 min_tx_queues;
	u64 min_rx_queues;
	u64 min_rx_add_queues;
	u64 max_tx_queues;
	u64 max_rx_queues;
	u64 max_rx_add_queues;
	u64 req_tx_queues;
	u64 req_rx_queues;
	u64 req_rx_add_queues;
	u64 min_tx_entries_per_subcrq;
	u64 min_rx_add_entries_per_subcrq;
	u64 max_tx_entries_per_subcrq;
	u64 max_rx_add_entries_per_subcrq;
	u64 req_tx_entries_per_subcrq;
	u64 req_rx_add_entries_per_subcrq;
	u64 tcp_ip_offload;
	u64 promisc_requested;
	u64 promisc_supported;
	u64 min_mtu;
	u64 max_mtu;
	u64 req_mtu;
	u64 max_multicast_filters;
	u64 vlan_header_insertion;
	u64 rx_vlan_header_insertion;
	u64 max_tx_sg_entries;
	u64 rx_sg_supported;
	u64 rx_sg_requested;
	u64 opt_tx_comp_sub_queues;
	u64 opt_rx_comp_queues;
	u64 opt_rx_bufadd_q_per_rx_comp_q;
	u64 opt_tx_entries_per_subcrq;
	u64 opt_rxba_entries_per_subcrq;
	__be64 tx_rx_desc_req;
	u8 map_id;
	u32 num_active_rx_scrqs;
	u32 num_active_rx_pools;
	u32 num_active_rx_napi;
	u32 num_active_tx_scrqs;
	u32 num_active_tx_pools;
	u32 cur_rx_buf_sz;

	struct tasklet_struct tasklet;
	enum vnic_state state;
	/* Used for serialization of state field. When taking both state
	 * and rwi locks, take state lock first.
	 */
	spinlock_t state_lock;
	enum ibmvnic_reset_reason reset_reason;
	struct list_head rwi_list;
	/* Used for serialization of rwi_list. When taking both state
	 * and rwi locks, take state lock first
	 */
	spinlock_t rwi_lock;
	struct work_struct ibmvnic_reset;
	struct delayed_work ibmvnic_delayed_reset;
	unsigned long resetting;
	bool napi_enabled, from_passive_init;
	bool login_pending;
	/* last device reset time */
	unsigned long last_reset_time;

	bool failover_pending;
	bool force_reset_recovery;

	struct ibmvnic_tunables desired;
	struct ibmvnic_tunables fallback;
};
