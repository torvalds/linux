/*
 * This file contains HW queue descriptor formats, config register
 * structures etc
 *
 * Copyright (C) 2015 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef Q_STRUCT_H
#define Q_STRUCT_H

/* Load transaction types for reading segment bytes specified by
 * NIC_SEND_GATHER_S[LD_TYPE].
 */
enum nic_send_ld_type_e {
	NIC_SEND_LD_TYPE_E_LDD = 0x0,
	NIC_SEND_LD_TYPE_E_LDT = 0x1,
	NIC_SEND_LD_TYPE_E_LDWB = 0x2,
	NIC_SEND_LD_TYPE_E_ENUM_LAST = 0x3,
};

enum ether_type_algorithm {
	ETYPE_ALG_NONE = 0x0,
	ETYPE_ALG_SKIP = 0x1,
	ETYPE_ALG_ENDPARSE = 0x2,
	ETYPE_ALG_VLAN = 0x3,
	ETYPE_ALG_VLAN_STRIP = 0x4,
};

enum layer3_type {
	L3TYPE_NONE = 0x00,
	L3TYPE_GRH = 0x01,
	L3TYPE_IPV4 = 0x04,
	L3TYPE_IPV4_OPTIONS = 0x05,
	L3TYPE_IPV6 = 0x06,
	L3TYPE_IPV6_OPTIONS = 0x07,
	L3TYPE_ET_STOP = 0x0D,
	L3TYPE_OTHER = 0x0E,
};

enum layer4_type {
	L4TYPE_NONE = 0x00,
	L4TYPE_IPSEC_ESP = 0x01,
	L4TYPE_IPFRAG = 0x02,
	L4TYPE_IPCOMP = 0x03,
	L4TYPE_TCP = 0x04,
	L4TYPE_UDP = 0x05,
	L4TYPE_SCTP = 0x06,
	L4TYPE_GRE = 0x07,
	L4TYPE_ROCE_BTH = 0x08,
	L4TYPE_OTHER = 0x0E,
};

/* CPI and RSSI configuration */
enum cpi_algorithm_type {
	CPI_ALG_NONE = 0x0,
	CPI_ALG_VLAN = 0x1,
	CPI_ALG_VLAN16 = 0x2,
	CPI_ALG_DIFF = 0x3,
};

enum rss_algorithm_type {
	RSS_ALG_NONE = 0x00,
	RSS_ALG_PORT = 0x01,
	RSS_ALG_IP = 0x02,
	RSS_ALG_TCP_IP = 0x03,
	RSS_ALG_UDP_IP = 0x04,
	RSS_ALG_SCTP_IP = 0x05,
	RSS_ALG_GRE_IP = 0x06,
	RSS_ALG_ROCE = 0x07,
};

enum rss_hash_cfg {
	RSS_HASH_L2ETC = 0x00,
	RSS_HASH_IP = 0x01,
	RSS_HASH_TCP = 0x02,
	RSS_HASH_TCP_SYN_DIS = 0x03,
	RSS_HASH_UDP = 0x04,
	RSS_HASH_L4ETC = 0x05,
	RSS_HASH_ROCE = 0x06,
	RSS_L3_BIDI = 0x07,
	RSS_L4_BIDI = 0x08,
};

/* Completion queue entry types */
enum cqe_type {
	CQE_TYPE_INVALID = 0x0,
	CQE_TYPE_RX = 0x2,
	CQE_TYPE_RX_SPLIT = 0x3,
	CQE_TYPE_RX_TCP = 0x4,
	CQE_TYPE_SEND = 0x8,
	CQE_TYPE_SEND_PTP = 0x9,
};

enum cqe_rx_tcp_status {
	CQE_RX_STATUS_VALID_TCP_CNXT = 0x00,
	CQE_RX_STATUS_INVALID_TCP_CNXT = 0x0F,
};

enum cqe_send_status {
	CQE_SEND_STATUS_GOOD = 0x00,
	CQE_SEND_STATUS_DESC_FAULT = 0x01,
	CQE_SEND_STATUS_HDR_CONS_ERR = 0x11,
	CQE_SEND_STATUS_SUBDESC_ERR = 0x12,
	CQE_SEND_STATUS_IMM_SIZE_OFLOW = 0x80,
	CQE_SEND_STATUS_CRC_SEQ_ERR = 0x81,
	CQE_SEND_STATUS_DATA_SEQ_ERR = 0x82,
	CQE_SEND_STATUS_MEM_SEQ_ERR = 0x83,
	CQE_SEND_STATUS_LOCK_VIOL = 0x84,
	CQE_SEND_STATUS_LOCK_UFLOW = 0x85,
	CQE_SEND_STATUS_DATA_FAULT = 0x86,
	CQE_SEND_STATUS_TSTMP_CONFLICT = 0x87,
	CQE_SEND_STATUS_TSTMP_TIMEOUT = 0x88,
	CQE_SEND_STATUS_MEM_FAULT = 0x89,
	CQE_SEND_STATUS_CSUM_OVERLAP = 0x8A,
	CQE_SEND_STATUS_CSUM_OVERFLOW = 0x8B,
};

enum cqe_rx_tcp_end_reason {
	CQE_RX_TCP_END_FIN_FLAG_DET = 0,
	CQE_RX_TCP_END_INVALID_FLAG = 1,
	CQE_RX_TCP_END_TIMEOUT = 2,
	CQE_RX_TCP_END_OUT_OF_SEQ = 3,
	CQE_RX_TCP_END_PKT_ERR = 4,
	CQE_RX_TCP_END_QS_DISABLED = 0x0F,
};

/* Packet protocol level error enumeration */
enum cqe_rx_err_level {
	CQE_RX_ERRLVL_RE = 0x0,
	CQE_RX_ERRLVL_L2 = 0x1,
	CQE_RX_ERRLVL_L3 = 0x2,
	CQE_RX_ERRLVL_L4 = 0x3,
};

/* Packet protocol level error type enumeration */
enum cqe_rx_err_opcode {
	CQE_RX_ERR_RE_NONE = 0x0,
	CQE_RX_ERR_RE_PARTIAL = 0x1,
	CQE_RX_ERR_RE_JABBER = 0x2,
	CQE_RX_ERR_RE_FCS = 0x7,
	CQE_RX_ERR_RE_TERMINATE = 0x9,
	CQE_RX_ERR_RE_RX_CTL = 0xb,
	CQE_RX_ERR_PREL2_ERR = 0x1f,
	CQE_RX_ERR_L2_FRAGMENT = 0x20,
	CQE_RX_ERR_L2_OVERRUN = 0x21,
	CQE_RX_ERR_L2_PFCS = 0x22,
	CQE_RX_ERR_L2_PUNY = 0x23,
	CQE_RX_ERR_L2_MAL = 0x24,
	CQE_RX_ERR_L2_OVERSIZE = 0x25,
	CQE_RX_ERR_L2_UNDERSIZE = 0x26,
	CQE_RX_ERR_L2_LENMISM = 0x27,
	CQE_RX_ERR_L2_PCLP = 0x28,
	CQE_RX_ERR_IP_NOT = 0x41,
	CQE_RX_ERR_IP_CHK = 0x42,
	CQE_RX_ERR_IP_MAL = 0x43,
	CQE_RX_ERR_IP_MALD = 0x44,
	CQE_RX_ERR_IP_HOP = 0x45,
	CQE_RX_ERR_L3_ICRC = 0x46,
	CQE_RX_ERR_L3_PCLP = 0x47,
	CQE_RX_ERR_L4_MAL = 0x61,
	CQE_RX_ERR_L4_CHK = 0x62,
	CQE_RX_ERR_UDP_LEN = 0x63,
	CQE_RX_ERR_L4_PORT = 0x64,
	CQE_RX_ERR_TCP_FLAG = 0x65,
	CQE_RX_ERR_TCP_OFFSET = 0x66,
	CQE_RX_ERR_L4_PCLP = 0x67,
	CQE_RX_ERR_RBDR_TRUNC = 0x70,
};

struct cqe_rx_t {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64   cqe_type:4; /* W0 */
	u64   stdn_fault:1;
	u64   rsvd0:1;
	u64   rq_qs:7;
	u64   rq_idx:3;
	u64   rsvd1:12;
	u64   rss_alg:4;
	u64   rsvd2:4;
	u64   rb_cnt:4;
	u64   vlan_found:1;
	u64   vlan_stripped:1;
	u64   vlan2_found:1;
	u64   vlan2_stripped:1;
	u64   l4_type:4;
	u64   l3_type:4;
	u64   l2_present:1;
	u64   err_level:3;
	u64   err_opcode:8;

	u64   pkt_len:16; /* W1 */
	u64   l2_ptr:8;
	u64   l3_ptr:8;
	u64   l4_ptr:8;
	u64   cq_pkt_len:8;
	u64   align_pad:3;
	u64   rsvd3:1;
	u64   chan:12;

	u64   rss_tag:32; /* W2 */
	u64   vlan_tci:16;
	u64   vlan_ptr:8;
	u64   vlan2_ptr:8;

	u64   rb3_sz:16; /* W3 */
	u64   rb2_sz:16;
	u64   rb1_sz:16;
	u64   rb0_sz:16;

	u64   rb7_sz:16; /* W4 */
	u64   rb6_sz:16;
	u64   rb5_sz:16;
	u64   rb4_sz:16;

	u64   rb11_sz:16; /* W5 */
	u64   rb10_sz:16;
	u64   rb9_sz:16;
	u64   rb8_sz:16;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64   err_opcode:8;
	u64   err_level:3;
	u64   l2_present:1;
	u64   l3_type:4;
	u64   l4_type:4;
	u64   vlan2_stripped:1;
	u64   vlan2_found:1;
	u64   vlan_stripped:1;
	u64   vlan_found:1;
	u64   rb_cnt:4;
	u64   rsvd2:4;
	u64   rss_alg:4;
	u64   rsvd1:12;
	u64   rq_idx:3;
	u64   rq_qs:7;
	u64   rsvd0:1;
	u64   stdn_fault:1;
	u64   cqe_type:4; /* W0 */
	u64   chan:12;
	u64   rsvd3:1;
	u64   align_pad:3;
	u64   cq_pkt_len:8;
	u64   l4_ptr:8;
	u64   l3_ptr:8;
	u64   l2_ptr:8;
	u64   pkt_len:16; /* W1 */
	u64   vlan2_ptr:8;
	u64   vlan_ptr:8;
	u64   vlan_tci:16;
	u64   rss_tag:32; /* W2 */
	u64   rb0_sz:16;
	u64   rb1_sz:16;
	u64   rb2_sz:16;
	u64   rb3_sz:16; /* W3 */
	u64   rb4_sz:16;
	u64   rb5_sz:16;
	u64   rb6_sz:16;
	u64   rb7_sz:16; /* W4 */
	u64   rb8_sz:16;
	u64   rb9_sz:16;
	u64   rb10_sz:16;
	u64   rb11_sz:16; /* W5 */
#endif
	u64   rb0_ptr:64;
	u64   rb1_ptr:64;
	u64   rb2_ptr:64;
	u64   rb3_ptr:64;
	u64   rb4_ptr:64;
	u64   rb5_ptr:64;
	u64   rb6_ptr:64;
	u64   rb7_ptr:64;
	u64   rb8_ptr:64;
	u64   rb9_ptr:64;
	u64   rb10_ptr:64;
	u64   rb11_ptr:64;
};

struct cqe_rx_tcp_err_t {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64   cqe_type:4; /* W0 */
	u64   rsvd0:60;

	u64   rsvd1:4; /* W1 */
	u64   partial_first:1;
	u64   rsvd2:27;
	u64   rbdr_bytes:8;
	u64   rsvd3:24;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64   rsvd0:60;
	u64   cqe_type:4;

	u64   rsvd3:24;
	u64   rbdr_bytes:8;
	u64   rsvd2:27;
	u64   partial_first:1;
	u64   rsvd1:4;
#endif
};

struct cqe_rx_tcp_t {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64   cqe_type:4; /* W0 */
	u64   rsvd0:52;
	u64   cq_tcp_status:8;

	u64   rsvd1:32; /* W1 */
	u64   tcp_cntx_bytes:8;
	u64   rsvd2:8;
	u64   tcp_err_bytes:16;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64   cq_tcp_status:8;
	u64   rsvd0:52;
	u64   cqe_type:4; /* W0 */

	u64   tcp_err_bytes:16;
	u64   rsvd2:8;
	u64   tcp_cntx_bytes:8;
	u64   rsvd1:32; /* W1 */
#endif
};

struct cqe_send_t {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64   cqe_type:4; /* W0 */
	u64   rsvd0:4;
	u64   sqe_ptr:16;
	u64   rsvd1:4;
	u64   rsvd2:10;
	u64   sq_qs:7;
	u64   sq_idx:3;
	u64   rsvd3:8;
	u64   send_status:8;

	u64   ptp_timestamp:64; /* W1 */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64   send_status:8;
	u64   rsvd3:8;
	u64   sq_idx:3;
	u64   sq_qs:7;
	u64   rsvd2:10;
	u64   rsvd1:4;
	u64   sqe_ptr:16;
	u64   rsvd0:4;
	u64   cqe_type:4; /* W0 */

	u64   ptp_timestamp:64; /* W1 */
#endif
};

union cq_desc_t {
	u64    u[64];
	struct cqe_send_t snd_hdr;
	struct cqe_rx_t rx_hdr;
	struct cqe_rx_tcp_t rx_tcp_hdr;
	struct cqe_rx_tcp_err_t rx_tcp_err_hdr;
};

struct rbdr_entry_t {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64   rsvd0:15;
	u64   buf_addr:42;
	u64   cache_align:7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64   cache_align:7;
	u64   buf_addr:42;
	u64   rsvd0:15;
#endif
};

/* TCP reassembly context */
struct rbe_tcp_cnxt_t {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64   tcp_pkt_cnt:12;
	u64   rsvd1:4;
	u64   align_hdr_bytes:4;
	u64   align_ptr_bytes:4;
	u64   ptr_bytes:16;
	u64   rsvd2:24;
	u64   cqe_type:4;
	u64   rsvd0:54;
	u64   tcp_end_reason:2;
	u64   tcp_status:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64   tcp_status:4;
	u64   tcp_end_reason:2;
	u64   rsvd0:54;
	u64   cqe_type:4;
	u64   rsvd2:24;
	u64   ptr_bytes:16;
	u64   align_ptr_bytes:4;
	u64   align_hdr_bytes:4;
	u64   rsvd1:4;
	u64   tcp_pkt_cnt:12;
#endif
};

/* Always Big endian */
struct rx_hdr_t {
	u64   opaque:32;
	u64   rss_flow:8;
	u64   skip_length:6;
	u64   disable_rss:1;
	u64   disable_tcp_reassembly:1;
	u64   nodrop:1;
	u64   dest_alg:2;
	u64   rsvd0:2;
	u64   dest_rq:11;
};

enum send_l4_csum_type {
	SEND_L4_CSUM_DISABLE = 0x00,
	SEND_L4_CSUM_UDP = 0x01,
	SEND_L4_CSUM_TCP = 0x02,
	SEND_L4_CSUM_SCTP = 0x03,
};

enum send_crc_alg {
	SEND_CRCALG_CRC32 = 0x00,
	SEND_CRCALG_CRC32C = 0x01,
	SEND_CRCALG_ICRC = 0x02,
};

enum send_load_type {
	SEND_LD_TYPE_LDD = 0x00,
	SEND_LD_TYPE_LDT = 0x01,
	SEND_LD_TYPE_LDWB = 0x02,
};

enum send_mem_alg_type {
	SEND_MEMALG_SET = 0x00,
	SEND_MEMALG_ADD = 0x08,
	SEND_MEMALG_SUB = 0x09,
	SEND_MEMALG_ADDLEN = 0x0A,
	SEND_MEMALG_SUBLEN = 0x0B,
};

enum send_mem_dsz_type {
	SEND_MEMDSZ_B64 = 0x00,
	SEND_MEMDSZ_B32 = 0x01,
	SEND_MEMDSZ_B8 = 0x03,
};

enum sq_subdesc_type {
	SQ_DESC_TYPE_INVALID = 0x00,
	SQ_DESC_TYPE_HEADER = 0x01,
	SQ_DESC_TYPE_CRC = 0x02,
	SQ_DESC_TYPE_IMMEDIATE = 0x03,
	SQ_DESC_TYPE_GATHER = 0x04,
	SQ_DESC_TYPE_MEMORY = 0x05,
};

struct sq_crc_subdesc {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64    rsvd1:32;
	u64    crc_ival:32;
	u64    subdesc_type:4;
	u64    crc_alg:2;
	u64    rsvd0:10;
	u64    crc_insert_pos:16;
	u64    hdr_start:16;
	u64    crc_len:16;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64    crc_len:16;
	u64    hdr_start:16;
	u64    crc_insert_pos:16;
	u64    rsvd0:10;
	u64    crc_alg:2;
	u64    subdesc_type:4;
	u64    crc_ival:32;
	u64    rsvd1:32;
#endif
};

struct sq_gather_subdesc {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64    subdesc_type:4; /* W0 */
	u64    ld_type:2;
	u64    rsvd0:42;
	u64    size:16;

	u64    rsvd1:15; /* W1 */
	u64    addr:49;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64    size:16;
	u64    rsvd0:42;
	u64    ld_type:2;
	u64    subdesc_type:4; /* W0 */

	u64    addr:49;
	u64    rsvd1:15; /* W1 */
#endif
};

/* SQ immediate subdescriptor */
struct sq_imm_subdesc {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64    subdesc_type:4; /* W0 */
	u64    rsvd0:46;
	u64    len:14;

	u64    data:64; /* W1 */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64    len:14;
	u64    rsvd0:46;
	u64    subdesc_type:4; /* W0 */

	u64    data:64; /* W1 */
#endif
};

struct sq_mem_subdesc {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64    subdesc_type:4; /* W0 */
	u64    mem_alg:4;
	u64    mem_dsz:2;
	u64    wmem:1;
	u64    rsvd0:21;
	u64    offset:32;

	u64    rsvd1:15; /* W1 */
	u64    addr:49;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64    offset:32;
	u64    rsvd0:21;
	u64    wmem:1;
	u64    mem_dsz:2;
	u64    mem_alg:4;
	u64    subdesc_type:4; /* W0 */

	u64    addr:49;
	u64    rsvd1:15; /* W1 */
#endif
};

struct sq_hdr_subdesc {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64    subdesc_type:4;
	u64    tso:1;
	u64    post_cqe:1; /* Post CQE on no error also */
	u64    dont_send:1;
	u64    tstmp:1;
	u64    subdesc_cnt:8;
	u64    csum_l4:2;
	u64    csum_l3:1;
	u64    csum_inner_l4:2;
	u64    csum_inner_l3:1;
	u64    rsvd0:2;
	u64    l4_offset:8;
	u64    l3_offset:8;
	u64    rsvd1:4;
	u64    tot_len:20; /* W0 */

	u64    rsvd2:24;
	u64    inner_l4_offset:8;
	u64    inner_l3_offset:8;
	u64    tso_start:8;
	u64    rsvd3:2;
	u64    tso_max_paysize:14; /* W1 */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64    tot_len:20;
	u64    rsvd1:4;
	u64    l3_offset:8;
	u64    l4_offset:8;
	u64    rsvd0:2;
	u64    csum_inner_l3:1;
	u64    csum_inner_l4:2;
	u64    csum_l3:1;
	u64    csum_l4:2;
	u64    subdesc_cnt:8;
	u64    tstmp:1;
	u64    dont_send:1;
	u64    post_cqe:1; /* Post CQE on no error also */
	u64    tso:1;
	u64    subdesc_type:4; /* W0 */

	u64    tso_max_paysize:14;
	u64    rsvd3:2;
	u64    tso_start:8;
	u64    inner_l3_offset:8;
	u64    inner_l4_offset:8;
	u64    rsvd2:24; /* W1 */
#endif
};

/* Queue config register formats */
struct rq_cfg {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_2_63:62;
	u64 ena:1;
	u64 tcp_ena:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64 tcp_ena:1;
	u64 ena:1;
	u64 reserved_2_63:62;
#endif
};

struct cq_cfg {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_43_63:21;
	u64 ena:1;
	u64 reset:1;
	u64 caching:1;
	u64 reserved_35_39:5;
	u64 qsize:3;
	u64 reserved_25_31:7;
	u64 avg_con:9;
	u64 reserved_0_15:16;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64 reserved_0_15:16;
	u64 avg_con:9;
	u64 reserved_25_31:7;
	u64 qsize:3;
	u64 reserved_35_39:5;
	u64 caching:1;
	u64 reset:1;
	u64 ena:1;
	u64 reserved_43_63:21;
#endif
};

struct sq_cfg {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_32_63:32;
	u64 cq_limit:8;
	u64 reserved_20_23:4;
	u64 ena:1;
	u64 reserved_18_18:1;
	u64 reset:1;
	u64 ldwb:1;
	u64 reserved_11_15:5;
	u64 qsize:3;
	u64 reserved_3_7:5;
	u64 tstmp_bgx_intf:3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64 tstmp_bgx_intf:3;
	u64 reserved_3_7:5;
	u64 qsize:3;
	u64 reserved_11_15:5;
	u64 ldwb:1;
	u64 reset:1;
	u64 reserved_18_18:1;
	u64 ena:1;
	u64 reserved_20_23:4;
	u64 cq_limit:8;
	u64 reserved_32_63:32;
#endif
};

struct rbdr_cfg {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_45_63:19;
	u64 ena:1;
	u64 reset:1;
	u64 ldwb:1;
	u64 reserved_36_41:6;
	u64 qsize:4;
	u64 reserved_25_31:7;
	u64 avg_con:9;
	u64 reserved_12_15:4;
	u64 lines:12;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64 lines:12;
	u64 reserved_12_15:4;
	u64 avg_con:9;
	u64 reserved_25_31:7;
	u64 qsize:4;
	u64 reserved_36_41:6;
	u64 ldwb:1;
	u64 reset:1;
	u64 ena: 1;
	u64 reserved_45_63:19;
#endif
};

struct qs_cfg {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_32_63:32;
	u64 ena:1;
	u64 reserved_27_30:4;
	u64 sq_ins_ena:1;
	u64 sq_ins_pos:6;
	u64 lock_ena:1;
	u64 lock_viol_cqe_ena:1;
	u64 send_tstmp_ena:1;
	u64 be:1;
	u64 reserved_7_15:9;
	u64 vnic:7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u64 vnic:7;
	u64 reserved_7_15:9;
	u64 be:1;
	u64 send_tstmp_ena:1;
	u64 lock_viol_cqe_ena:1;
	u64 lock_ena:1;
	u64 sq_ins_pos:6;
	u64 sq_ins_ena:1;
	u64 reserved_27_30:4;
	u64 ena:1;
	u64 reserved_32_63:32;
#endif
};

#endif /* Q_STRUCT_H */
