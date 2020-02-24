/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __T4_MSG_H
#define __T4_MSG_H

#include <linux/types.h>

enum {
	CPL_PASS_OPEN_REQ     = 0x1,
	CPL_PASS_ACCEPT_RPL   = 0x2,
	CPL_ACT_OPEN_REQ      = 0x3,
	CPL_SET_TCB_FIELD     = 0x5,
	CPL_GET_TCB           = 0x6,
	CPL_CLOSE_CON_REQ     = 0x8,
	CPL_CLOSE_LISTSRV_REQ = 0x9,
	CPL_ABORT_REQ         = 0xA,
	CPL_ABORT_RPL         = 0xB,
	CPL_RX_DATA_ACK       = 0xD,
	CPL_TX_PKT            = 0xE,
	CPL_L2T_WRITE_REQ     = 0x12,
	CPL_SMT_WRITE_REQ     = 0x14,
	CPL_TID_RELEASE       = 0x1A,
	CPL_SRQ_TABLE_REQ     = 0x1C,
	CPL_TX_DATA_ISO	      = 0x1F,

	CPL_CLOSE_LISTSRV_RPL = 0x20,
	CPL_GET_TCB_RPL       = 0x22,
	CPL_L2T_WRITE_RPL     = 0x23,
	CPL_PASS_OPEN_RPL     = 0x24,
	CPL_ACT_OPEN_RPL      = 0x25,
	CPL_PEER_CLOSE        = 0x26,
	CPL_ABORT_REQ_RSS     = 0x2B,
	CPL_ABORT_RPL_RSS     = 0x2D,
	CPL_SMT_WRITE_RPL     = 0x2E,

	CPL_RX_PHYS_ADDR      = 0x30,
	CPL_CLOSE_CON_RPL     = 0x32,
	CPL_ISCSI_HDR         = 0x33,
	CPL_RDMA_CQE          = 0x35,
	CPL_RDMA_CQE_READ_RSP = 0x36,
	CPL_RDMA_CQE_ERR      = 0x37,
	CPL_RX_DATA           = 0x39,
	CPL_SET_TCB_RPL       = 0x3A,
	CPL_RX_PKT            = 0x3B,
	CPL_RX_DDP_COMPLETE   = 0x3F,

	CPL_ACT_ESTABLISH     = 0x40,
	CPL_PASS_ESTABLISH    = 0x41,
	CPL_RX_DATA_DDP       = 0x42,
	CPL_PASS_ACCEPT_REQ   = 0x44,
	CPL_RX_ISCSI_CMP      = 0x45,
	CPL_TRACE_PKT_T5      = 0x48,
	CPL_RX_ISCSI_DDP      = 0x49,
	CPL_RX_TLS_CMP        = 0x4E,

	CPL_RDMA_READ_REQ     = 0x60,

	CPL_PASS_OPEN_REQ6    = 0x81,
	CPL_ACT_OPEN_REQ6     = 0x83,

	CPL_TX_TLS_PDU        = 0x88,
	CPL_TX_TLS_SFO        = 0x89,
	CPL_TX_SEC_PDU        = 0x8A,
	CPL_TX_TLS_ACK        = 0x8B,

	CPL_RDMA_TERMINATE    = 0xA2,
	CPL_RDMA_WRITE        = 0xA4,
	CPL_SGE_EGR_UPDATE    = 0xA5,
	CPL_RX_MPS_PKT        = 0xAF,

	CPL_TRACE_PKT         = 0xB0,
	CPL_TLS_DATA          = 0xB1,
	CPL_ISCSI_DATA	      = 0xB2,

	CPL_FW4_MSG           = 0xC0,
	CPL_FW4_PLD           = 0xC1,
	CPL_FW4_ACK           = 0xC3,
	CPL_SRQ_TABLE_RPL     = 0xCC,

	CPL_RX_PHYS_DSGL      = 0xD0,

	CPL_FW6_MSG           = 0xE0,
	CPL_FW6_PLD           = 0xE1,
	CPL_TX_TNL_LSO        = 0xEC,
	CPL_TX_PKT_LSO        = 0xED,
	CPL_TX_PKT_XT         = 0xEE,

	NUM_CPL_CMDS
};

enum CPL_error {
	CPL_ERR_NONE               = 0,
	CPL_ERR_TCAM_PARITY        = 1,
	CPL_ERR_TCAM_MISS          = 2,
	CPL_ERR_TCAM_FULL          = 3,
	CPL_ERR_BAD_LENGTH         = 15,
	CPL_ERR_BAD_ROUTE          = 18,
	CPL_ERR_CONN_RESET         = 20,
	CPL_ERR_CONN_EXIST_SYNRECV = 21,
	CPL_ERR_CONN_EXIST         = 22,
	CPL_ERR_ARP_MISS           = 23,
	CPL_ERR_BAD_SYN            = 24,
	CPL_ERR_CONN_TIMEDOUT      = 30,
	CPL_ERR_XMIT_TIMEDOUT      = 31,
	CPL_ERR_PERSIST_TIMEDOUT   = 32,
	CPL_ERR_FINWAIT2_TIMEDOUT  = 33,
	CPL_ERR_KEEPALIVE_TIMEDOUT = 34,
	CPL_ERR_RTX_NEG_ADVICE     = 35,
	CPL_ERR_PERSIST_NEG_ADVICE = 36,
	CPL_ERR_KEEPALV_NEG_ADVICE = 37,
	CPL_ERR_ABORT_FAILED       = 42,
	CPL_ERR_IWARP_FLM          = 50,
	CPL_CONTAINS_READ_RPL      = 60,
	CPL_CONTAINS_WRITE_RPL     = 61,
};

enum {
	CPL_CONN_POLICY_AUTO = 0,
	CPL_CONN_POLICY_ASK  = 1,
	CPL_CONN_POLICY_FILTER = 2,
	CPL_CONN_POLICY_DENY = 3
};

enum {
	ULP_MODE_NONE          = 0,
	ULP_MODE_ISCSI         = 2,
	ULP_MODE_RDMA          = 4,
	ULP_MODE_TCPDDP	       = 5,
	ULP_MODE_FCOE          = 6,
	ULP_MODE_TLS           = 8,
};

enum {
	ULP_CRC_HEADER = 1 << 0,
	ULP_CRC_DATA   = 1 << 1
};

enum {
	CPL_ABORT_SEND_RST = 0,
	CPL_ABORT_NO_RST,
};

enum {                     /* TX_PKT_XT checksum types */
	TX_CSUM_TCP    = 0,
	TX_CSUM_UDP    = 1,
	TX_CSUM_CRC16  = 4,
	TX_CSUM_CRC32  = 5,
	TX_CSUM_CRC32C = 6,
	TX_CSUM_FCOE   = 7,
	TX_CSUM_TCPIP  = 8,
	TX_CSUM_UDPIP  = 9,
	TX_CSUM_TCPIP6 = 10,
	TX_CSUM_UDPIP6 = 11,
	TX_CSUM_IP     = 12,
};

union opcode_tid {
	__be32 opcode_tid;
	u8 opcode;
};

#define CPL_OPCODE_S    24
#define CPL_OPCODE_V(x) ((x) << CPL_OPCODE_S)
#define CPL_OPCODE_G(x) (((x) >> CPL_OPCODE_S) & 0xFF)
#define TID_G(x)    ((x) & 0xFFFFFF)

/* tid is assumed to be 24-bits */
#define MK_OPCODE_TID(opcode, tid) (CPL_OPCODE_V(opcode) | (tid))

#define OPCODE_TID(cmd) ((cmd)->ot.opcode_tid)

/* extract the TID from a CPL command */
#define GET_TID(cmd) (TID_G(be32_to_cpu(OPCODE_TID(cmd))))

/* partitioning of TID fields that also carry a queue id */
#define TID_TID_S    0
#define TID_TID_M    0x3fff
#define TID_TID_V(x) ((x) << TID_TID_S)
#define TID_TID_G(x) (((x) >> TID_TID_S) & TID_TID_M)

#define TID_QID_S    14
#define TID_QID_M    0x3ff
#define TID_QID_V(x) ((x) << TID_QID_S)
#define TID_QID_G(x) (((x) >> TID_QID_S) & TID_QID_M)

struct rss_header {
	u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 channel:2;
	u8 filter_hit:1;
	u8 filter_tid:1;
	u8 hash_type:2;
	u8 ipv6:1;
	u8 send2fw:1;
#else
	u8 send2fw:1;
	u8 ipv6:1;
	u8 hash_type:2;
	u8 filter_tid:1;
	u8 filter_hit:1;
	u8 channel:2;
#endif
	__be16 qid;
	__be32 hash_val;
};

struct work_request_hdr {
	__be32 wr_hi;
	__be32 wr_mid;
	__be64 wr_lo;
};

/* wr_hi fields */
#define WR_OP_S    24
#define WR_OP_V(x) ((__u64)(x) << WR_OP_S)

#define WR_HDR struct work_request_hdr wr

/* option 0 fields */
#define TX_CHAN_S    2
#define TX_CHAN_V(x) ((x) << TX_CHAN_S)

#define ULP_MODE_S    8
#define ULP_MODE_V(x) ((x) << ULP_MODE_S)

#define RCV_BUFSIZ_S    12
#define RCV_BUFSIZ_M    0x3FFU
#define RCV_BUFSIZ_V(x) ((x) << RCV_BUFSIZ_S)

#define SMAC_SEL_S    28
#define SMAC_SEL_V(x) ((__u64)(x) << SMAC_SEL_S)

#define L2T_IDX_S    36
#define L2T_IDX_V(x) ((__u64)(x) << L2T_IDX_S)

#define WND_SCALE_S    50
#define WND_SCALE_V(x) ((__u64)(x) << WND_SCALE_S)

#define KEEP_ALIVE_S    54
#define KEEP_ALIVE_V(x) ((__u64)(x) << KEEP_ALIVE_S)
#define KEEP_ALIVE_F    KEEP_ALIVE_V(1ULL)

#define MSS_IDX_S    60
#define MSS_IDX_M    0xF
#define MSS_IDX_V(x) ((__u64)(x) << MSS_IDX_S)
#define MSS_IDX_G(x) (((x) >> MSS_IDX_S) & MSS_IDX_M)

/* option 2 fields */
#define RSS_QUEUE_S    0
#define RSS_QUEUE_M    0x3FF
#define RSS_QUEUE_V(x) ((x) << RSS_QUEUE_S)
#define RSS_QUEUE_G(x) (((x) >> RSS_QUEUE_S) & RSS_QUEUE_M)

#define RSS_QUEUE_VALID_S    10
#define RSS_QUEUE_VALID_V(x) ((x) << RSS_QUEUE_VALID_S)
#define RSS_QUEUE_VALID_F    RSS_QUEUE_VALID_V(1U)

#define RX_FC_DISABLE_S    20
#define RX_FC_DISABLE_V(x) ((x) << RX_FC_DISABLE_S)
#define RX_FC_DISABLE_F    RX_FC_DISABLE_V(1U)

#define RX_FC_VALID_S    22
#define RX_FC_VALID_V(x) ((x) << RX_FC_VALID_S)
#define RX_FC_VALID_F    RX_FC_VALID_V(1U)

#define RX_CHANNEL_S    26
#define RX_CHANNEL_V(x) ((x) << RX_CHANNEL_S)
#define RX_CHANNEL_F	RX_CHANNEL_V(1U)

#define WND_SCALE_EN_S    28
#define WND_SCALE_EN_V(x) ((x) << WND_SCALE_EN_S)
#define WND_SCALE_EN_F    WND_SCALE_EN_V(1U)

#define T5_OPT_2_VALID_S    31
#define T5_OPT_2_VALID_V(x) ((x) << T5_OPT_2_VALID_S)
#define T5_OPT_2_VALID_F    T5_OPT_2_VALID_V(1U)

struct cpl_pass_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be64 opt1;
};

/* option 0 fields */
#define NO_CONG_S    4
#define NO_CONG_V(x) ((x) << NO_CONG_S)
#define NO_CONG_F    NO_CONG_V(1U)

#define DELACK_S    5
#define DELACK_V(x) ((x) << DELACK_S)
#define DELACK_F    DELACK_V(1U)

#define NON_OFFLOAD_S		7
#define NON_OFFLOAD_V(x)	((x) << NON_OFFLOAD_S)
#define NON_OFFLOAD_F		NON_OFFLOAD_V(1U)

#define DSCP_S    22
#define DSCP_M    0x3F
#define DSCP_V(x) ((x) << DSCP_S)
#define DSCP_G(x) (((x) >> DSCP_S) & DSCP_M)

#define TCAM_BYPASS_S    48
#define TCAM_BYPASS_V(x) ((__u64)(x) << TCAM_BYPASS_S)
#define TCAM_BYPASS_F    TCAM_BYPASS_V(1ULL)

#define NAGLE_S    49
#define NAGLE_V(x) ((__u64)(x) << NAGLE_S)
#define NAGLE_F    NAGLE_V(1ULL)

/* option 1 fields */
#define SYN_RSS_ENABLE_S    0
#define SYN_RSS_ENABLE_V(x) ((x) << SYN_RSS_ENABLE_S)
#define SYN_RSS_ENABLE_F    SYN_RSS_ENABLE_V(1U)

#define SYN_RSS_QUEUE_S    2
#define SYN_RSS_QUEUE_V(x) ((x) << SYN_RSS_QUEUE_S)

#define CONN_POLICY_S    22
#define CONN_POLICY_V(x) ((x) << CONN_POLICY_S)

struct cpl_pass_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be64 opt1;
};

struct cpl_pass_open_rpl {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
};

struct tcp_options {
	__be16 mss;
	__u8 wsf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8:4;
	__u8 unknown:1;
	__u8:1;
	__u8 sack:1;
	__u8 tstamp:1;
#else
	__u8 tstamp:1;
	__u8 sack:1;
	__u8:1;
	__u8 unknown:1;
	__u8:4;
#endif
};

struct cpl_pass_accept_req {
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 hdr_len;
	__be16 vlan;
	__be16 l2info;
	__be32 tos_stid;
	struct tcp_options tcpopt;
};

/* cpl_pass_accept_req.hdr_len fields */
#define SYN_RX_CHAN_S    0
#define SYN_RX_CHAN_M    0xF
#define SYN_RX_CHAN_V(x) ((x) << SYN_RX_CHAN_S)
#define SYN_RX_CHAN_G(x) (((x) >> SYN_RX_CHAN_S) & SYN_RX_CHAN_M)

#define TCP_HDR_LEN_S    10
#define TCP_HDR_LEN_M    0x3F
#define TCP_HDR_LEN_V(x) ((x) << TCP_HDR_LEN_S)
#define TCP_HDR_LEN_G(x) (((x) >> TCP_HDR_LEN_S) & TCP_HDR_LEN_M)

#define IP_HDR_LEN_S    16
#define IP_HDR_LEN_M    0x3FF
#define IP_HDR_LEN_V(x) ((x) << IP_HDR_LEN_S)
#define IP_HDR_LEN_G(x) (((x) >> IP_HDR_LEN_S) & IP_HDR_LEN_M)

#define ETH_HDR_LEN_S    26
#define ETH_HDR_LEN_M    0x1F
#define ETH_HDR_LEN_V(x) ((x) << ETH_HDR_LEN_S)
#define ETH_HDR_LEN_G(x) (((x) >> ETH_HDR_LEN_S) & ETH_HDR_LEN_M)

/* cpl_pass_accept_req.l2info fields */
#define SYN_MAC_IDX_S    0
#define SYN_MAC_IDX_M    0x1FF
#define SYN_MAC_IDX_V(x) ((x) << SYN_MAC_IDX_S)
#define SYN_MAC_IDX_G(x) (((x) >> SYN_MAC_IDX_S) & SYN_MAC_IDX_M)

#define SYN_XACT_MATCH_S    9
#define SYN_XACT_MATCH_V(x) ((x) << SYN_XACT_MATCH_S)
#define SYN_XACT_MATCH_F    SYN_XACT_MATCH_V(1U)

#define SYN_INTF_S    12
#define SYN_INTF_M    0xF
#define SYN_INTF_V(x) ((x) << SYN_INTF_S)
#define SYN_INTF_G(x) (((x) >> SYN_INTF_S) & SYN_INTF_M)

enum {                     /* TCP congestion control algorithms */
	CONG_ALG_RENO,
	CONG_ALG_TAHOE,
	CONG_ALG_NEWRENO,
	CONG_ALG_HIGHSPEED
};

#define CONG_CNTRL_S    14
#define CONG_CNTRL_M    0x3
#define CONG_CNTRL_V(x) ((x) << CONG_CNTRL_S)
#define CONG_CNTRL_G(x) (((x) >> CONG_CNTRL_S) & CONG_CNTRL_M)

#define T5_ISS_S    18
#define T5_ISS_V(x) ((x) << T5_ISS_S)
#define T5_ISS_F    T5_ISS_V(1U)

struct cpl_pass_accept_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 opt2;
	__be64 opt0;
};

/* option 2 fields */
#define RX_COALESCE_VALID_S    11
#define RX_COALESCE_VALID_V(x) ((x) << RX_COALESCE_VALID_S)
#define RX_COALESCE_VALID_F    RX_COALESCE_VALID_V(1U)

#define RX_COALESCE_S    12
#define RX_COALESCE_V(x) ((x) << RX_COALESCE_S)

#define PACE_S    16
#define PACE_V(x) ((x) << PACE_S)

#define TX_QUEUE_S    23
#define TX_QUEUE_M    0x7
#define TX_QUEUE_V(x) ((x) << TX_QUEUE_S)
#define TX_QUEUE_G(x) (((x) >> TX_QUEUE_S) & TX_QUEUE_M)

#define CCTRL_ECN_S    27
#define CCTRL_ECN_V(x) ((x) << CCTRL_ECN_S)
#define CCTRL_ECN_F    CCTRL_ECN_V(1U)

#define TSTAMPS_EN_S    29
#define TSTAMPS_EN_V(x) ((x) << TSTAMPS_EN_S)
#define TSTAMPS_EN_F    TSTAMPS_EN_V(1U)

#define SACK_EN_S    30
#define SACK_EN_V(x) ((x) << SACK_EN_S)
#define SACK_EN_F    SACK_EN_V(1U)

struct cpl_t5_pass_accept_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 opt2;
	__be64 opt0;
	__be32 iss;
	__be32 rsvd;
};

struct cpl_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be32 params;
	__be32 opt2;
};

#define FILTER_TUPLE_S  24
#define FILTER_TUPLE_M  0xFFFFFFFFFF
#define FILTER_TUPLE_V(x) ((x) << FILTER_TUPLE_S)
#define FILTER_TUPLE_G(x) (((x) >> FILTER_TUPLE_S) & FILTER_TUPLE_M)
struct cpl_t5_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be32 rsvd;
	__be32 opt2;
	__be64 params;
};

struct cpl_t6_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be32 rsvd;
	__be32 opt2;
	__be64 params;
	__be32 rsvd2;
	__be32 opt3;
};

struct cpl_act_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be32 params;
	__be32 opt2;
};

struct cpl_t5_act_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be32 rsvd;
	__be32 opt2;
	__be64 params;
};

struct cpl_t6_act_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be32 rsvd;
	__be32 opt2;
	__be64 params;
	__be32 rsvd2;
	__be32 opt3;
};

struct cpl_act_open_rpl {
	union opcode_tid ot;
	__be32 atid_status;
};

/* cpl_act_open_rpl.atid_status fields */
#define AOPEN_STATUS_S    0
#define AOPEN_STATUS_M    0xFF
#define AOPEN_STATUS_G(x) (((x) >> AOPEN_STATUS_S) & AOPEN_STATUS_M)

#define AOPEN_ATID_S    8
#define AOPEN_ATID_M    0xFFFFFF
#define AOPEN_ATID_G(x) (((x) >> AOPEN_ATID_S) & AOPEN_ATID_M)

struct cpl_pass_establish {
	union opcode_tid ot;
	__be32 rsvd;
	__be32 tos_stid;
	__be16 mac_idx;
	__be16 tcp_opt;
	__be32 snd_isn;
	__be32 rcv_isn;
};

/* cpl_pass_establish.tos_stid fields */
#define PASS_OPEN_TID_S    0
#define PASS_OPEN_TID_M    0xFFFFFF
#define PASS_OPEN_TID_V(x) ((x) << PASS_OPEN_TID_S)
#define PASS_OPEN_TID_G(x) (((x) >> PASS_OPEN_TID_S) & PASS_OPEN_TID_M)

#define PASS_OPEN_TOS_S    24
#define PASS_OPEN_TOS_M    0xFF
#define PASS_OPEN_TOS_V(x) ((x) << PASS_OPEN_TOS_S)
#define PASS_OPEN_TOS_G(x) (((x) >> PASS_OPEN_TOS_S) & PASS_OPEN_TOS_M)

/* cpl_pass_establish.tcp_opt fields (also applies to act_open_establish) */
#define TCPOPT_WSCALE_OK_S	5
#define TCPOPT_WSCALE_OK_M	0x1
#define TCPOPT_WSCALE_OK_G(x)	\
	(((x) >> TCPOPT_WSCALE_OK_S) & TCPOPT_WSCALE_OK_M)

#define TCPOPT_SACK_S		6
#define TCPOPT_SACK_M		0x1
#define TCPOPT_SACK_G(x)	(((x) >> TCPOPT_SACK_S) & TCPOPT_SACK_M)

#define TCPOPT_TSTAMP_S		7
#define TCPOPT_TSTAMP_M		0x1
#define TCPOPT_TSTAMP_G(x)	(((x) >> TCPOPT_TSTAMP_S) & TCPOPT_TSTAMP_M)

#define TCPOPT_SND_WSCALE_S	8
#define TCPOPT_SND_WSCALE_M	0xF
#define TCPOPT_SND_WSCALE_G(x)	\
	(((x) >> TCPOPT_SND_WSCALE_S) & TCPOPT_SND_WSCALE_M)

#define TCPOPT_MSS_S	12
#define TCPOPT_MSS_M	0xF
#define TCPOPT_MSS_G(x)	(((x) >> TCPOPT_MSS_S) & TCPOPT_MSS_M)

#define T6_TCP_HDR_LEN_S   8
#define T6_TCP_HDR_LEN_V(x) ((x) << T6_TCP_HDR_LEN_S)
#define T6_TCP_HDR_LEN_G(x) (((x) >> T6_TCP_HDR_LEN_S) & TCP_HDR_LEN_M)

#define T6_IP_HDR_LEN_S    14
#define T6_IP_HDR_LEN_V(x) ((x) << T6_IP_HDR_LEN_S)
#define T6_IP_HDR_LEN_G(x) (((x) >> T6_IP_HDR_LEN_S) & IP_HDR_LEN_M)

#define T6_ETH_HDR_LEN_S    24
#define T6_ETH_HDR_LEN_M    0xFF
#define T6_ETH_HDR_LEN_V(x) ((x) << T6_ETH_HDR_LEN_S)
#define T6_ETH_HDR_LEN_G(x) (((x) >> T6_ETH_HDR_LEN_S) & T6_ETH_HDR_LEN_M)

struct cpl_act_establish {
	union opcode_tid ot;
	__be32 rsvd;
	__be32 tos_atid;
	__be16 mac_idx;
	__be16 tcp_opt;
	__be32 snd_isn;
	__be32 rcv_isn;
};

struct cpl_get_tcb {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 cookie;
};

/* cpl_get_tcb.reply_ctrl fields */
#define QUEUENO_S    0
#define QUEUENO_V(x) ((x) << QUEUENO_S)

#define REPLY_CHAN_S    14
#define REPLY_CHAN_V(x) ((x) << REPLY_CHAN_S)
#define REPLY_CHAN_F    REPLY_CHAN_V(1U)

#define NO_REPLY_S    15
#define NO_REPLY_V(x) ((x) << NO_REPLY_S)
#define NO_REPLY_F    NO_REPLY_V(1U)

struct cpl_get_tcb_rpl {
	union opcode_tid ot;
	__u8 cookie;
	__u8 status;
	__be16 len;
};

struct cpl_set_tcb_field {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 word_cookie;
	__be64 mask;
	__be64 val;
};

/* cpl_set_tcb_field.word_cookie fields */
#define TCB_WORD_S	0
#define TCB_WORD_V(x)	((x) << TCB_WORD_S)

#define TCB_COOKIE_S    5
#define TCB_COOKIE_M    0x7
#define TCB_COOKIE_V(x) ((x) << TCB_COOKIE_S)
#define TCB_COOKIE_G(x) (((x) >> TCB_COOKIE_S) & TCB_COOKIE_M)

struct cpl_set_tcb_rpl {
	union opcode_tid ot;
	__be16 rsvd;
	u8 cookie;
	u8 status;
	__be64 oldval;
};

struct cpl_close_con_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd;
};

struct cpl_close_con_rpl {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
	__be32 snd_nxt;
	__be32 rcv_nxt;
};

struct cpl_close_listsvr_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 rsvd;
};

/* additional cpl_close_listsvr_req.reply_ctrl field */
#define LISTSVR_IPV6_S    14
#define LISTSVR_IPV6_V(x) ((x) << LISTSVR_IPV6_S)
#define LISTSVR_IPV6_F    LISTSVR_IPV6_V(1U)

struct cpl_close_listsvr_rpl {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
};

struct cpl_abort_req_rss {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
};

struct cpl_abort_req_rss6 {
	union opcode_tid ot;
	__be32 srqidx_status;
};

#define ABORT_RSS_STATUS_S    0
#define ABORT_RSS_STATUS_M    0xff
#define ABORT_RSS_STATUS_V(x) ((x) << ABORT_RSS_STATUS_S)
#define ABORT_RSS_STATUS_G(x) (((x) >> ABORT_RSS_STATUS_S) & ABORT_RSS_STATUS_M)

#define ABORT_RSS_SRQIDX_S    8
#define ABORT_RSS_SRQIDX_M    0xffffff
#define ABORT_RSS_SRQIDX_V(x) ((x) << ABORT_RSS_SRQIDX_S)
#define ABORT_RSS_SRQIDX_G(x) (((x) >> ABORT_RSS_SRQIDX_S) & ABORT_RSS_SRQIDX_M)

struct cpl_abort_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd0;
	u8 rsvd1;
	u8 cmd;
	u8 rsvd2[6];
};

struct cpl_abort_rpl_rss {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
};

struct cpl_abort_rpl_rss6 {
	union opcode_tid ot;
	__be32 srqidx_status;
};

struct cpl_abort_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd0;
	u8 rsvd1;
	u8 cmd;
	u8 rsvd2[6];
};

struct cpl_peer_close {
	union opcode_tid ot;
	__be32 rcv_nxt;
};

struct cpl_tid_release {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd;
};

struct cpl_tx_pkt_core {
	__be32 ctrl0;
	__be16 pack;
	__be16 len;
	__be64 ctrl1;
};

struct cpl_tx_pkt {
	WR_HDR;
	struct cpl_tx_pkt_core c;
};

#define cpl_tx_pkt_xt cpl_tx_pkt

/* cpl_tx_pkt_core.ctrl0 fields */
#define TXPKT_VF_S    0
#define TXPKT_VF_V(x) ((x) << TXPKT_VF_S)

#define TXPKT_PF_S    8
#define TXPKT_PF_V(x) ((x) << TXPKT_PF_S)

#define TXPKT_VF_VLD_S    11
#define TXPKT_VF_VLD_V(x) ((x) << TXPKT_VF_VLD_S)
#define TXPKT_VF_VLD_F    TXPKT_VF_VLD_V(1U)

#define TXPKT_OVLAN_IDX_S    12
#define TXPKT_OVLAN_IDX_V(x) ((x) << TXPKT_OVLAN_IDX_S)

#define TXPKT_T5_OVLAN_IDX_S	12
#define TXPKT_T5_OVLAN_IDX_V(x)	((x) << TXPKT_T5_OVLAN_IDX_S)

#define TXPKT_INTF_S    16
#define TXPKT_INTF_V(x) ((x) << TXPKT_INTF_S)

#define TXPKT_INS_OVLAN_S    21
#define TXPKT_INS_OVLAN_V(x) ((x) << TXPKT_INS_OVLAN_S)
#define TXPKT_INS_OVLAN_F    TXPKT_INS_OVLAN_V(1U)

#define TXPKT_TSTAMP_S    23
#define TXPKT_TSTAMP_V(x) ((x) << TXPKT_TSTAMP_S)
#define TXPKT_TSTAMP_F    TXPKT_TSTAMP_V(1ULL)

#define TXPKT_OPCODE_S    24
#define TXPKT_OPCODE_V(x) ((x) << TXPKT_OPCODE_S)

/* cpl_tx_pkt_core.ctrl1 fields */
#define TXPKT_CSUM_END_S    12
#define TXPKT_CSUM_END_V(x) ((x) << TXPKT_CSUM_END_S)

#define TXPKT_CSUM_START_S    20
#define TXPKT_CSUM_START_V(x) ((x) << TXPKT_CSUM_START_S)

#define TXPKT_IPHDR_LEN_S    20
#define TXPKT_IPHDR_LEN_V(x) ((__u64)(x) << TXPKT_IPHDR_LEN_S)

#define TXPKT_CSUM_LOC_S    30
#define TXPKT_CSUM_LOC_V(x) ((__u64)(x) << TXPKT_CSUM_LOC_S)

#define TXPKT_ETHHDR_LEN_S    34
#define TXPKT_ETHHDR_LEN_V(x) ((__u64)(x) << TXPKT_ETHHDR_LEN_S)

#define T6_TXPKT_ETHHDR_LEN_S    32
#define T6_TXPKT_ETHHDR_LEN_V(x) ((__u64)(x) << T6_TXPKT_ETHHDR_LEN_S)

#define TXPKT_CSUM_TYPE_S    40
#define TXPKT_CSUM_TYPE_V(x) ((__u64)(x) << TXPKT_CSUM_TYPE_S)

#define TXPKT_VLAN_S    44
#define TXPKT_VLAN_V(x) ((__u64)(x) << TXPKT_VLAN_S)

#define TXPKT_VLAN_VLD_S    60
#define TXPKT_VLAN_VLD_V(x) ((__u64)(x) << TXPKT_VLAN_VLD_S)
#define TXPKT_VLAN_VLD_F    TXPKT_VLAN_VLD_V(1ULL)

#define TXPKT_IPCSUM_DIS_S    62
#define TXPKT_IPCSUM_DIS_V(x) ((__u64)(x) << TXPKT_IPCSUM_DIS_S)
#define TXPKT_IPCSUM_DIS_F    TXPKT_IPCSUM_DIS_V(1ULL)

#define TXPKT_L4CSUM_DIS_S    63
#define TXPKT_L4CSUM_DIS_V(x) ((__u64)(x) << TXPKT_L4CSUM_DIS_S)
#define TXPKT_L4CSUM_DIS_F    TXPKT_L4CSUM_DIS_V(1ULL)

struct cpl_tx_pkt_lso_core {
	__be32 lso_ctrl;
	__be16 ipid_ofst;
	__be16 mss;
	__be32 seqno_offset;
	__be32 len;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

/* cpl_tx_pkt_lso_core.lso_ctrl fields */
#define LSO_TCPHDR_LEN_S    0
#define LSO_TCPHDR_LEN_V(x) ((x) << LSO_TCPHDR_LEN_S)

#define LSO_IPHDR_LEN_S    4
#define LSO_IPHDR_LEN_V(x) ((x) << LSO_IPHDR_LEN_S)

#define LSO_ETHHDR_LEN_S    16
#define LSO_ETHHDR_LEN_V(x) ((x) << LSO_ETHHDR_LEN_S)

#define LSO_IPV6_S    20
#define LSO_IPV6_V(x) ((x) << LSO_IPV6_S)
#define LSO_IPV6_F    LSO_IPV6_V(1U)

#define LSO_LAST_SLICE_S    22
#define LSO_LAST_SLICE_V(x) ((x) << LSO_LAST_SLICE_S)
#define LSO_LAST_SLICE_F    LSO_LAST_SLICE_V(1U)

#define LSO_FIRST_SLICE_S    23
#define LSO_FIRST_SLICE_V(x) ((x) << LSO_FIRST_SLICE_S)
#define LSO_FIRST_SLICE_F    LSO_FIRST_SLICE_V(1U)

#define LSO_OPCODE_S    24
#define LSO_OPCODE_V(x) ((x) << LSO_OPCODE_S)

#define LSO_T5_XFER_SIZE_S	   0
#define LSO_T5_XFER_SIZE_V(x) ((x) << LSO_T5_XFER_SIZE_S)

struct cpl_tx_pkt_lso {
	WR_HDR;
	struct cpl_tx_pkt_lso_core c;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

struct cpl_iscsi_hdr {
	union opcode_tid ot;
	__be16 pdu_len_ddp;
	__be16 len;
	__be32 seq;
	__be16 urg;
	u8 rsvd;
	u8 status;
};

/* cpl_iscsi_hdr.pdu_len_ddp fields */
#define ISCSI_PDU_LEN_S    0
#define ISCSI_PDU_LEN_M    0x7FFF
#define ISCSI_PDU_LEN_V(x) ((x) << ISCSI_PDU_LEN_S)
#define ISCSI_PDU_LEN_G(x) (((x) >> ISCSI_PDU_LEN_S) & ISCSI_PDU_LEN_M)

#define ISCSI_DDP_S    15
#define ISCSI_DDP_V(x) ((x) << ISCSI_DDP_S)
#define ISCSI_DDP_F    ISCSI_DDP_V(1U)

struct cpl_rx_data_ddp {
	union opcode_tid ot;
	__be16 urg;
	__be16 len;
	__be32 seq;
	union {
		__be32 nxt_seq;
		__be32 ddp_report;
	};
	__be32 ulp_crc;
	__be32 ddpvld;
};

#define cpl_rx_iscsi_ddp cpl_rx_data_ddp

struct cpl_iscsi_data {
	union opcode_tid ot;
	__u8 rsvd0[2];
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd1;
	__u8 status;
};

struct cpl_rx_iscsi_cmp {
	union opcode_tid ot;
	__be16 pdu_len_ddp;
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd;
	__u8 status;
	__be32 ulp_crc;
	__be32 ddpvld;
};

struct cpl_tx_data_iso {
	__be32 op_to_scsi;
	__u8   reserved1;
	__u8   ahs_len;
	__be16 mpdu;
	__be32 burst_size;
	__be32 len;
	__be32 reserved2_seglen_offset;
	__be32 datasn_offset;
	__be32 buffer_offset;
	__be32 reserved3;

	/* encapsulated CPL_TX_DATA follows here */
};

/* cpl_tx_data_iso.op_to_scsi fields */
#define CPL_TX_DATA_ISO_OP_S	24
#define CPL_TX_DATA_ISO_OP_M	0xff
#define CPL_TX_DATA_ISO_OP_V(x)	((x) << CPL_TX_DATA_ISO_OP_S)
#define CPL_TX_DATA_ISO_OP_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_OP_S) & CPL_TX_DATA_ISO_OP_M)

#define CPL_TX_DATA_ISO_FIRST_S		23
#define CPL_TX_DATA_ISO_FIRST_M		0x1
#define CPL_TX_DATA_ISO_FIRST_V(x)	((x) << CPL_TX_DATA_ISO_FIRST_S)
#define CPL_TX_DATA_ISO_FIRST_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_FIRST_S) & CPL_TX_DATA_ISO_FIRST_M)
#define CPL_TX_DATA_ISO_FIRST_F	CPL_TX_DATA_ISO_FIRST_V(1U)

#define CPL_TX_DATA_ISO_LAST_S		22
#define CPL_TX_DATA_ISO_LAST_M		0x1
#define CPL_TX_DATA_ISO_LAST_V(x)	((x) << CPL_TX_DATA_ISO_LAST_S)
#define CPL_TX_DATA_ISO_LAST_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_LAST_S) & CPL_TX_DATA_ISO_LAST_M)
#define CPL_TX_DATA_ISO_LAST_F	CPL_TX_DATA_ISO_LAST_V(1U)

#define CPL_TX_DATA_ISO_CPLHDRLEN_S	21
#define CPL_TX_DATA_ISO_CPLHDRLEN_M	0x1
#define CPL_TX_DATA_ISO_CPLHDRLEN_V(x)	((x) << CPL_TX_DATA_ISO_CPLHDRLEN_S)
#define CPL_TX_DATA_ISO_CPLHDRLEN_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_CPLHDRLEN_S) & CPL_TX_DATA_ISO_CPLHDRLEN_M)
#define CPL_TX_DATA_ISO_CPLHDRLEN_F	CPL_TX_DATA_ISO_CPLHDRLEN_V(1U)

#define CPL_TX_DATA_ISO_HDRCRC_S	20
#define CPL_TX_DATA_ISO_HDRCRC_M	0x1
#define CPL_TX_DATA_ISO_HDRCRC_V(x)	((x) << CPL_TX_DATA_ISO_HDRCRC_S)
#define CPL_TX_DATA_ISO_HDRCRC_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_HDRCRC_S) & CPL_TX_DATA_ISO_HDRCRC_M)
#define CPL_TX_DATA_ISO_HDRCRC_F	CPL_TX_DATA_ISO_HDRCRC_V(1U)

#define CPL_TX_DATA_ISO_PLDCRC_S	19
#define CPL_TX_DATA_ISO_PLDCRC_M	0x1
#define CPL_TX_DATA_ISO_PLDCRC_V(x)	((x) << CPL_TX_DATA_ISO_PLDCRC_S)
#define CPL_TX_DATA_ISO_PLDCRC_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_PLDCRC_S) & CPL_TX_DATA_ISO_PLDCRC_M)
#define CPL_TX_DATA_ISO_PLDCRC_F	CPL_TX_DATA_ISO_PLDCRC_V(1U)

#define CPL_TX_DATA_ISO_IMMEDIATE_S	18
#define CPL_TX_DATA_ISO_IMMEDIATE_M	0x1
#define CPL_TX_DATA_ISO_IMMEDIATE_V(x)	((x) << CPL_TX_DATA_ISO_IMMEDIATE_S)
#define CPL_TX_DATA_ISO_IMMEDIATE_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_IMMEDIATE_S) & CPL_TX_DATA_ISO_IMMEDIATE_M)
#define CPL_TX_DATA_ISO_IMMEDIATE_F	CPL_TX_DATA_ISO_IMMEDIATE_V(1U)

#define CPL_TX_DATA_ISO_SCSI_S		16
#define CPL_TX_DATA_ISO_SCSI_M		0x3
#define CPL_TX_DATA_ISO_SCSI_V(x)	((x) << CPL_TX_DATA_ISO_SCSI_S)
#define CPL_TX_DATA_ISO_SCSI_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_SCSI_S) & CPL_TX_DATA_ISO_SCSI_M)

/* cpl_tx_data_iso.reserved2_seglen_offset fields */
#define CPL_TX_DATA_ISO_SEGLEN_OFFSET_S		0
#define CPL_TX_DATA_ISO_SEGLEN_OFFSET_M		0xffffff
#define CPL_TX_DATA_ISO_SEGLEN_OFFSET_V(x)	\
	((x) << CPL_TX_DATA_ISO_SEGLEN_OFFSET_S)
#define CPL_TX_DATA_ISO_SEGLEN_OFFSET_G(x)	\
	(((x) >> CPL_TX_DATA_ISO_SEGLEN_OFFSET_S) & \
	 CPL_TX_DATA_ISO_SEGLEN_OFFSET_M)

struct cpl_rx_data {
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 seq;
	__be16 urg;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 dack_mode:2;
	u8 psh:1;
	u8 heartbeat:1;
	u8 ddp_off:1;
	u8 :3;
#else
	u8 :3;
	u8 ddp_off:1;
	u8 heartbeat:1;
	u8 psh:1;
	u8 dack_mode:2;
#endif
	u8 status;
};

struct cpl_rx_data_ack {
	WR_HDR;
	union opcode_tid ot;
	__be32 credit_dack;
};

/* cpl_rx_data_ack.ack_seq fields */
#define RX_CREDITS_S    0
#define RX_CREDITS_V(x) ((x) << RX_CREDITS_S)

#define RX_FORCE_ACK_S    28
#define RX_FORCE_ACK_V(x) ((x) << RX_FORCE_ACK_S)
#define RX_FORCE_ACK_F    RX_FORCE_ACK_V(1U)

#define RX_DACK_MODE_S    29
#define RX_DACK_MODE_M    0x3
#define RX_DACK_MODE_V(x) ((x) << RX_DACK_MODE_S)
#define RX_DACK_MODE_G(x) (((x) >> RX_DACK_MODE_S) & RX_DACK_MODE_M)

#define RX_DACK_CHANGE_S    31
#define RX_DACK_CHANGE_V(x) ((x) << RX_DACK_CHANGE_S)
#define RX_DACK_CHANGE_F    RX_DACK_CHANGE_V(1U)

struct cpl_rx_pkt {
	struct rss_header rsshdr;
	u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 iff:4;
	u8 csum_calc:1;
	u8 ipmi_pkt:1;
	u8 vlan_ex:1;
	u8 ip_frag:1;
#else
	u8 ip_frag:1;
	u8 vlan_ex:1;
	u8 ipmi_pkt:1;
	u8 csum_calc:1;
	u8 iff:4;
#endif
	__be16 csum;
	__be16 vlan;
	__be16 len;
	__be32 l2info;
	__be16 hdr_len;
	__be16 err_vec;
};

#define RX_T6_ETHHDR_LEN_M    0xFF
#define RX_T6_ETHHDR_LEN_G(x) (((x) >> RX_ETHHDR_LEN_S) & RX_T6_ETHHDR_LEN_M)

#define RXF_PSH_S    20
#define RXF_PSH_V(x) ((x) << RXF_PSH_S)
#define RXF_PSH_F    RXF_PSH_V(1U)

#define RXF_SYN_S    21
#define RXF_SYN_V(x) ((x) << RXF_SYN_S)
#define RXF_SYN_F    RXF_SYN_V(1U)

#define RXF_UDP_S    22
#define RXF_UDP_V(x) ((x) << RXF_UDP_S)
#define RXF_UDP_F    RXF_UDP_V(1U)

#define RXF_TCP_S    23
#define RXF_TCP_V(x) ((x) << RXF_TCP_S)
#define RXF_TCP_F    RXF_TCP_V(1U)

#define RXF_IP_S    24
#define RXF_IP_V(x) ((x) << RXF_IP_S)
#define RXF_IP_F    RXF_IP_V(1U)

#define RXF_IP6_S    25
#define RXF_IP6_V(x) ((x) << RXF_IP6_S)
#define RXF_IP6_F    RXF_IP6_V(1U)

#define RXF_SYN_COOKIE_S    26
#define RXF_SYN_COOKIE_V(x) ((x) << RXF_SYN_COOKIE_S)
#define RXF_SYN_COOKIE_F    RXF_SYN_COOKIE_V(1U)

#define RXF_FCOE_S    26
#define RXF_FCOE_V(x) ((x) << RXF_FCOE_S)
#define RXF_FCOE_F    RXF_FCOE_V(1U)

#define RXF_LRO_S    27
#define RXF_LRO_V(x) ((x) << RXF_LRO_S)
#define RXF_LRO_F    RXF_LRO_V(1U)

/* rx_pkt.l2info fields */
#define RX_ETHHDR_LEN_S    0
#define RX_ETHHDR_LEN_M    0x1F
#define RX_ETHHDR_LEN_V(x) ((x) << RX_ETHHDR_LEN_S)
#define RX_ETHHDR_LEN_G(x) (((x) >> RX_ETHHDR_LEN_S) & RX_ETHHDR_LEN_M)

#define RX_T5_ETHHDR_LEN_S    0
#define RX_T5_ETHHDR_LEN_M    0x3F
#define RX_T5_ETHHDR_LEN_V(x) ((x) << RX_T5_ETHHDR_LEN_S)
#define RX_T5_ETHHDR_LEN_G(x) (((x) >> RX_T5_ETHHDR_LEN_S) & RX_T5_ETHHDR_LEN_M)

#define RX_MACIDX_S    8
#define RX_MACIDX_M    0x1FF
#define RX_MACIDX_V(x) ((x) << RX_MACIDX_S)
#define RX_MACIDX_G(x) (((x) >> RX_MACIDX_S) & RX_MACIDX_M)

#define RXF_SYN_S    21
#define RXF_SYN_V(x) ((x) << RXF_SYN_S)
#define RXF_SYN_F    RXF_SYN_V(1U)

#define RX_CHAN_S    28
#define RX_CHAN_M    0xF
#define RX_CHAN_V(x) ((x) << RX_CHAN_S)
#define RX_CHAN_G(x) (((x) >> RX_CHAN_S) & RX_CHAN_M)

/* rx_pkt.hdr_len fields */
#define RX_TCPHDR_LEN_S    0
#define RX_TCPHDR_LEN_M    0x3F
#define RX_TCPHDR_LEN_V(x) ((x) << RX_TCPHDR_LEN_S)
#define RX_TCPHDR_LEN_G(x) (((x) >> RX_TCPHDR_LEN_S) & RX_TCPHDR_LEN_M)

#define RX_IPHDR_LEN_S    6
#define RX_IPHDR_LEN_M    0x3FF
#define RX_IPHDR_LEN_V(x) ((x) << RX_IPHDR_LEN_S)
#define RX_IPHDR_LEN_G(x) (((x) >> RX_IPHDR_LEN_S) & RX_IPHDR_LEN_M)

/* rx_pkt.err_vec fields */
#define RXERR_CSUM_S    13
#define RXERR_CSUM_V(x) ((x) << RXERR_CSUM_S)
#define RXERR_CSUM_F    RXERR_CSUM_V(1U)

#define T6_COMPR_RXERR_LEN_S    1
#define T6_COMPR_RXERR_LEN_V(x) ((x) << T6_COMPR_RXERR_LEN_S)
#define T6_COMPR_RXERR_LEN_F    T6_COMPR_RXERR_LEN_V(1U)

#define T6_COMPR_RXERR_VEC_S    0
#define T6_COMPR_RXERR_VEC_M    0x3F
#define T6_COMPR_RXERR_VEC_V(x) ((x) << T6_COMPR_RXERR_LEN_S)
#define T6_COMPR_RXERR_VEC_G(x) \
		(((x) >> T6_COMPR_RXERR_VEC_S) & T6_COMPR_RXERR_VEC_M)

/* Logical OR of RX_ERROR_CSUM, RX_ERROR_CSIP */
#define T6_COMPR_RXERR_SUM_S    4
#define T6_COMPR_RXERR_SUM_V(x) ((x) << T6_COMPR_RXERR_SUM_S)
#define T6_COMPR_RXERR_SUM_F    T6_COMPR_RXERR_SUM_V(1U)

#define T6_RX_TNLHDR_LEN_S    8
#define T6_RX_TNLHDR_LEN_M    0xFF
#define T6_RX_TNLHDR_LEN_V(x) ((x) << T6_RX_TNLHDR_LEN_S)
#define T6_RX_TNLHDR_LEN_G(x) (((x) >> T6_RX_TNLHDR_LEN_S) & T6_RX_TNLHDR_LEN_M)

struct cpl_trace_pkt {
	u8 opcode;
	u8 intf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 runt:4;
	u8 filter_hit:4;
	u8 :6;
	u8 err:1;
	u8 trunc:1;
#else
	u8 filter_hit:4;
	u8 runt:4;
	u8 trunc:1;
	u8 err:1;
	u8 :6;
#endif
	__be16 rsvd;
	__be16 len;
	__be64 tstamp;
};

struct cpl_t5_trace_pkt {
	__u8 opcode;
	__u8 intf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 runt:4;
	__u8 filter_hit:4;
	__u8:6;
	__u8 err:1;
	__u8 trunc:1;
#else
	__u8 filter_hit:4;
	__u8 runt:4;
	__u8 trunc:1;
	__u8 err:1;
	__u8:6;
#endif
	__be16 rsvd;
	__be16 len;
	__be64 tstamp;
	__be64 rsvd1;
};

struct cpl_l2t_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 params;
	__be16 l2t_idx;
	__be16 vlan;
	u8 dst_mac[6];
};

/* cpl_l2t_write_req.params fields */
#define L2T_W_INFO_S    2
#define L2T_W_INFO_V(x) ((x) << L2T_W_INFO_S)

#define L2T_W_PORT_S    8
#define L2T_W_PORT_V(x) ((x) << L2T_W_PORT_S)

#define L2T_W_NOREPLY_S    15
#define L2T_W_NOREPLY_V(x) ((x) << L2T_W_NOREPLY_S)
#define L2T_W_NOREPLY_F    L2T_W_NOREPLY_V(1U)

#define CPL_L2T_VLAN_NONE 0xfff

struct cpl_l2t_write_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd[3];
};

struct cpl_smt_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
	__be16 pfvf1;
	u8 src_mac1[6];
	__be16 pfvf0;
	u8 src_mac0[6];
};

struct cpl_t6_smt_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
	__be64 tag;
	__be16 pfvf0;
	u8 src_mac0[6];
	__be32 local_ip;
	__be32 rsvd;
};

struct cpl_smt_write_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd[3];
};

/* cpl_smt_{read,write}_req.params fields */
#define SMTW_OVLAN_IDX_S	16
#define SMTW_OVLAN_IDX_V(x)	((x) << SMTW_OVLAN_IDX_S)

#define SMTW_IDX_S	20
#define SMTW_IDX_V(x)	((x) << SMTW_IDX_S)

#define SMTW_NORPL_S	31
#define SMTW_NORPL_V(x)	((x) << SMTW_NORPL_S)
#define SMTW_NORPL_F	SMTW_NORPL_V(1U)

struct cpl_rdma_terminate {
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
};

struct cpl_sge_egr_update {
	__be32 opcode_qid;
	__be16 cidx;
	__be16 pidx;
};

/* cpl_sge_egr_update.ot fields */
#define EGR_QID_S    0
#define EGR_QID_M    0x1FFFF
#define EGR_QID_G(x) (((x) >> EGR_QID_S) & EGR_QID_M)

/* cpl_fw*.type values */
enum {
	FW_TYPE_CMD_RPL = 0,
	FW_TYPE_WR_RPL = 1,
	FW_TYPE_CQE = 2,
	FW_TYPE_OFLD_CONNECTION_WR_RPL = 3,
	FW_TYPE_RSSCPL = 4,
};

struct cpl_fw4_pld {
	u8 opcode;
	u8 rsvd0[3];
	u8 type;
	u8 rsvd1;
	__be16 len;
	__be64 data;
	__be64 rsvd2;
};

struct cpl_fw6_pld {
	u8 opcode;
	u8 rsvd[5];
	__be16 len;
	__be64 data[4];
};

struct cpl_fw4_msg {
	u8 opcode;
	u8 type;
	__be16 rsvd0;
	__be32 rsvd1;
	__be64 data[2];
};

struct cpl_fw4_ack {
	union opcode_tid ot;
	u8 credits;
	u8 rsvd0[2];
	u8 seq_vld;
	__be32 snd_nxt;
	__be32 snd_una;
	__be64 rsvd1;
};

enum {
	CPL_FW4_ACK_FLAGS_SEQVAL	= 0x1,	/* seqn valid */
	CPL_FW4_ACK_FLAGS_CH		= 0x2,	/* channel change complete */
	CPL_FW4_ACK_FLAGS_FLOWC		= 0x4,	/* fw_flowc_wr complete */
};

#define CPL_FW4_ACK_FLOWID_S    0
#define CPL_FW4_ACK_FLOWID_M    0xffffff
#define CPL_FW4_ACK_FLOWID_G(x) \
	(((x) >> CPL_FW4_ACK_FLOWID_S) & CPL_FW4_ACK_FLOWID_M)

struct cpl_fw6_msg {
	u8 opcode;
	u8 type;
	__be16 rsvd0;
	__be32 rsvd1;
	__be64 data[4];
};

/* cpl_fw6_msg.type values */
enum {
	FW6_TYPE_CMD_RPL = 0,
	FW6_TYPE_WR_RPL = 1,
	FW6_TYPE_CQE = 2,
	FW6_TYPE_OFLD_CONNECTION_WR_RPL = 3,
	FW6_TYPE_RSSCPL = FW_TYPE_RSSCPL,
};

struct cpl_fw6_msg_ofld_connection_wr_rpl {
	__u64   cookie;
	__be32  tid;    /* or atid in case of active failure */
	__u8    t_state;
	__u8    retval;
	__u8    rsvd[2];
};

struct cpl_tx_data {
	union opcode_tid ot;
	__be32 len;
	__be32 rsvd;
	__be32 flags;
};

/* cpl_tx_data.flags field */
#define TX_FORCE_S	13
#define TX_FORCE_V(x)	((x) << TX_FORCE_S)

#define T6_TX_FORCE_S		20
#define T6_TX_FORCE_V(x)	((x) << T6_TX_FORCE_S)
#define T6_TX_FORCE_F		T6_TX_FORCE_V(1U)

#define TX_URG_S    16
#define TX_URG_V(x) ((x) << TX_URG_S)

#define TX_SHOVE_S    14
#define TX_SHOVE_V(x) ((x) << TX_SHOVE_S)

#define TX_ULP_MODE_S    10
#define TX_ULP_MODE_M    0x7
#define TX_ULP_MODE_V(x) ((x) << TX_ULP_MODE_S)
#define TX_ULP_MODE_G(x) (((x) >> TX_ULP_MODE_S) & TX_ULP_MODE_M)

enum {
	ULP_TX_MEM_READ = 2,
	ULP_TX_MEM_WRITE = 3,
	ULP_TX_PKT = 4
};

enum {
	ULP_TX_SC_NOOP = 0x80,
	ULP_TX_SC_IMM  = 0x81,
	ULP_TX_SC_DSGL = 0x82,
	ULP_TX_SC_ISGL = 0x83,
	ULP_TX_SC_MEMRD = 0x86
};

#define ULPTX_CMD_S    24
#define ULPTX_CMD_V(x) ((x) << ULPTX_CMD_S)

#define ULPTX_LEN16_S    0
#define ULPTX_LEN16_M    0xFF
#define ULPTX_LEN16_V(x) ((x) << ULPTX_LEN16_S)

#define ULP_TX_SC_MORE_S 23
#define ULP_TX_SC_MORE_V(x) ((x) << ULP_TX_SC_MORE_S)
#define ULP_TX_SC_MORE_F  ULP_TX_SC_MORE_V(1U)

struct ulptx_sge_pair {
	__be32 len[2];
	__be64 addr[2];
};

struct ulptx_sgl {
	__be32 cmd_nsge;
	__be32 len0;
	__be64 addr0;
	struct ulptx_sge_pair sge[];
};

struct ulptx_idata {
	__be32 cmd_more;
	__be32 len;
};

struct ulp_txpkt {
	__be32 cmd_dest;
	__be32 len;
};

#define ULPTX_CMD_S    24
#define ULPTX_CMD_M    0xFF
#define ULPTX_CMD_V(x) ((x) << ULPTX_CMD_S)

#define ULPTX_NSGE_S    0
#define ULPTX_NSGE_V(x) ((x) << ULPTX_NSGE_S)

#define ULPTX_MORE_S	23
#define ULPTX_MORE_V(x)	((x) << ULPTX_MORE_S)
#define ULPTX_MORE_F	ULPTX_MORE_V(1U)

#define ULP_TXPKT_DEST_S    16
#define ULP_TXPKT_DEST_M    0x3
#define ULP_TXPKT_DEST_V(x) ((x) << ULP_TXPKT_DEST_S)

#define ULP_TXPKT_FID_S     4
#define ULP_TXPKT_FID_M     0x7ff
#define ULP_TXPKT_FID_V(x)  ((x) << ULP_TXPKT_FID_S)

#define ULP_TXPKT_RO_S      3
#define ULP_TXPKT_RO_V(x) ((x) << ULP_TXPKT_RO_S)
#define ULP_TXPKT_RO_F ULP_TXPKT_RO_V(1U)

enum cpl_tx_tnl_lso_type {
	TX_TNL_TYPE_OPAQUE,
	TX_TNL_TYPE_NVGRE,
	TX_TNL_TYPE_VXLAN,
	TX_TNL_TYPE_GENEVE,
};

struct cpl_tx_tnl_lso {
	__be32 op_to_IpIdSplitOut;
	__be16 IpIdOffsetOut;
	__be16 UdpLenSetOut_to_TnlHdrLen;
	__be64 r1;
	__be32 Flow_to_TcpHdrLen;
	__be16 IpIdOffset;
	__be16 IpIdSplit_to_Mss;
	__be32 TCPSeqOffset;
	__be32 EthLenOffset_Size;
	/* encapsulated CPL (TX_PKT_XT) follows here */
};

#define CPL_TX_TNL_LSO_OPCODE_S		24
#define CPL_TX_TNL_LSO_OPCODE_M		0xff
#define CPL_TX_TNL_LSO_OPCODE_V(x)      ((x) << CPL_TX_TNL_LSO_OPCODE_S)
#define CPL_TX_TNL_LSO_OPCODE_G(x)      \
	(((x) >> CPL_TX_TNL_LSO_OPCODE_S) & CPL_TX_TNL_LSO_OPCODE_M)

#define CPL_TX_TNL_LSO_FIRST_S		23
#define CPL_TX_TNL_LSO_FIRST_M		0x1
#define CPL_TX_TNL_LSO_FIRST_V(x)	((x) << CPL_TX_TNL_LSO_FIRST_S)
#define CPL_TX_TNL_LSO_FIRST_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_FIRST_S) & CPL_TX_TNL_LSO_FIRST_M)
#define CPL_TX_TNL_LSO_FIRST_F		CPL_TX_TNL_LSO_FIRST_V(1U)

#define CPL_TX_TNL_LSO_LAST_S		22
#define CPL_TX_TNL_LSO_LAST_M		0x1
#define CPL_TX_TNL_LSO_LAST_V(x)	((x) << CPL_TX_TNL_LSO_LAST_S)
#define CPL_TX_TNL_LSO_LAST_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_LAST_S) & CPL_TX_TNL_LSO_LAST_M)
#define CPL_TX_TNL_LSO_LAST_F		CPL_TX_TNL_LSO_LAST_V(1U)

#define CPL_TX_TNL_LSO_ETHHDRLENXOUT_S	21
#define CPL_TX_TNL_LSO_ETHHDRLENXOUT_M	0x1
#define CPL_TX_TNL_LSO_ETHHDRLENXOUT_V(x) \
	((x) << CPL_TX_TNL_LSO_ETHHDRLENXOUT_S)
#define CPL_TX_TNL_LSO_ETHHDRLENXOUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_ETHHDRLENXOUT_S) & \
	 CPL_TX_TNL_LSO_ETHHDRLENXOUT_M)
#define CPL_TX_TNL_LSO_ETHHDRLENXOUT_F CPL_TX_TNL_LSO_ETHHDRLENXOUT_V(1U)

#define CPL_TX_TNL_LSO_IPV6OUT_S	20
#define CPL_TX_TNL_LSO_IPV6OUT_M	0x1
#define CPL_TX_TNL_LSO_IPV6OUT_V(x)	((x) << CPL_TX_TNL_LSO_IPV6OUT_S)
#define CPL_TX_TNL_LSO_IPV6OUT_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_IPV6OUT_S) & CPL_TX_TNL_LSO_IPV6OUT_M)
#define CPL_TX_TNL_LSO_IPV6OUT_F        CPL_TX_TNL_LSO_IPV6OUT_V(1U)

#define CPL_TX_TNL_LSO_ETHHDRLEN_S	16
#define CPL_TX_TNL_LSO_ETHHDRLEN_M	0xf
#define CPL_TX_TNL_LSO_ETHHDRLEN_V(x)	((x) << CPL_TX_TNL_LSO_ETHHDRLEN_S)
#define CPL_TX_TNL_LSO_ETHHDRLEN_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_ETHHDRLEN_S) & CPL_TX_TNL_LSO_ETHHDRLEN_M)

#define CPL_TX_TNL_LSO_IPHDRLEN_S	4
#define CPL_TX_TNL_LSO_IPHDRLEN_M	0xfff
#define CPL_TX_TNL_LSO_IPHDRLEN_V(x)	((x) << CPL_TX_TNL_LSO_IPHDRLEN_S)
#define CPL_TX_TNL_LSO_IPHDRLEN_G(x)    \
	(((x) >> CPL_TX_TNL_LSO_IPHDRLEN_S) & CPL_TX_TNL_LSO_IPHDRLEN_M)

#define CPL_TX_TNL_LSO_TCPHDRLEN_S	0
#define CPL_TX_TNL_LSO_TCPHDRLEN_M	0xf
#define CPL_TX_TNL_LSO_TCPHDRLEN_V(x)	((x) << CPL_TX_TNL_LSO_TCPHDRLEN_S)
#define CPL_TX_TNL_LSO_TCPHDRLEN_G(x)   \
	(((x) >> CPL_TX_TNL_LSO_TCPHDRLEN_S) & CPL_TX_TNL_LSO_TCPHDRLEN_M)

#define CPL_TX_TNL_LSO_MSS_S            0
#define CPL_TX_TNL_LSO_MSS_M            0x3fff
#define CPL_TX_TNL_LSO_MSS_V(x)         ((x) << CPL_TX_TNL_LSO_MSS_S)
#define CPL_TX_TNL_LSO_MSS_G(x)         \
	(((x) >> CPL_TX_TNL_LSO_MSS_S) & CPL_TX_TNL_LSO_MSS_M)

#define CPL_TX_TNL_LSO_SIZE_S		0
#define CPL_TX_TNL_LSO_SIZE_M		0xfffffff
#define CPL_TX_TNL_LSO_SIZE_V(x)	((x) << CPL_TX_TNL_LSO_SIZE_S)
#define CPL_TX_TNL_LSO_SIZE_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_SIZE_S) & CPL_TX_TNL_LSO_SIZE_M)

#define CPL_TX_TNL_LSO_ETHHDRLENOUT_S   16
#define CPL_TX_TNL_LSO_ETHHDRLENOUT_M   0xf
#define CPL_TX_TNL_LSO_ETHHDRLENOUT_V(x) \
	((x) << CPL_TX_TNL_LSO_ETHHDRLENOUT_S)
#define CPL_TX_TNL_LSO_ETHHDRLENOUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_ETHHDRLENOUT_S) & CPL_TX_TNL_LSO_ETHHDRLENOUT_M)

#define CPL_TX_TNL_LSO_IPHDRLENOUT_S    4
#define CPL_TX_TNL_LSO_IPHDRLENOUT_M    0xfff
#define CPL_TX_TNL_LSO_IPHDRLENOUT_V(x) ((x) << CPL_TX_TNL_LSO_IPHDRLENOUT_S)
#define CPL_TX_TNL_LSO_IPHDRLENOUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_IPHDRLENOUT_S) & CPL_TX_TNL_LSO_IPHDRLENOUT_M)

#define CPL_TX_TNL_LSO_IPHDRCHKOUT_S    3
#define CPL_TX_TNL_LSO_IPHDRCHKOUT_M    0x1
#define CPL_TX_TNL_LSO_IPHDRCHKOUT_V(x) ((x) << CPL_TX_TNL_LSO_IPHDRCHKOUT_S)
#define CPL_TX_TNL_LSO_IPHDRCHKOUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_IPHDRCHKOUT_S) & CPL_TX_TNL_LSO_IPHDRCHKOUT_M)
#define CPL_TX_TNL_LSO_IPHDRCHKOUT_F    CPL_TX_TNL_LSO_IPHDRCHKOUT_V(1U)

#define CPL_TX_TNL_LSO_IPLENSETOUT_S	2
#define CPL_TX_TNL_LSO_IPLENSETOUT_M	0x1
#define CPL_TX_TNL_LSO_IPLENSETOUT_V(x) ((x) << CPL_TX_TNL_LSO_IPLENSETOUT_S)
#define CPL_TX_TNL_LSO_IPLENSETOUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_IPLENSETOUT_S) & CPL_TX_TNL_LSO_IPLENSETOUT_M)
#define CPL_TX_TNL_LSO_IPLENSETOUT_F	CPL_TX_TNL_LSO_IPLENSETOUT_V(1U)

#define CPL_TX_TNL_LSO_IPIDINCOUT_S	1
#define CPL_TX_TNL_LSO_IPIDINCOUT_M	0x1
#define CPL_TX_TNL_LSO_IPIDINCOUT_V(x)  ((x) << CPL_TX_TNL_LSO_IPIDINCOUT_S)
#define CPL_TX_TNL_LSO_IPIDINCOUT_G(x)  \
	(((x) >> CPL_TX_TNL_LSO_IPIDINCOUT_S) & CPL_TX_TNL_LSO_IPIDINCOUT_M)
#define CPL_TX_TNL_LSO_IPIDINCOUT_F     CPL_TX_TNL_LSO_IPIDINCOUT_V(1U)

#define CPL_TX_TNL_LSO_UDPCHKCLROUT_S   14
#define CPL_TX_TNL_LSO_UDPCHKCLROUT_M   0x1
#define CPL_TX_TNL_LSO_UDPCHKCLROUT_V(x) \
	((x) << CPL_TX_TNL_LSO_UDPCHKCLROUT_S)
#define CPL_TX_TNL_LSO_UDPCHKCLROUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_UDPCHKCLROUT_S) & \
	 CPL_TX_TNL_LSO_UDPCHKCLROUT_M)
#define CPL_TX_TNL_LSO_UDPCHKCLROUT_F   CPL_TX_TNL_LSO_UDPCHKCLROUT_V(1U)

#define CPL_TX_TNL_LSO_UDPLENSETOUT_S   15
#define CPL_TX_TNL_LSO_UDPLENSETOUT_M   0x1
#define CPL_TX_TNL_LSO_UDPLENSETOUT_V(x) \
	((x) << CPL_TX_TNL_LSO_UDPLENSETOUT_S)
#define CPL_TX_TNL_LSO_UDPLENSETOUT_G(x) \
	(((x) >> CPL_TX_TNL_LSO_UDPLENSETOUT_S) & \
	 CPL_TX_TNL_LSO_UDPLENSETOUT_M)
#define CPL_TX_TNL_LSO_UDPLENSETOUT_F   CPL_TX_TNL_LSO_UDPLENSETOUT_V(1U)

#define CPL_TX_TNL_LSO_TNLTYPE_S	12
#define CPL_TX_TNL_LSO_TNLTYPE_M	0x3
#define CPL_TX_TNL_LSO_TNLTYPE_V(x)	((x) << CPL_TX_TNL_LSO_TNLTYPE_S)
#define CPL_TX_TNL_LSO_TNLTYPE_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_TNLTYPE_S) & CPL_TX_TNL_LSO_TNLTYPE_M)

#define S_CPL_TX_TNL_LSO_ETHHDRLEN	16
#define M_CPL_TX_TNL_LSO_ETHHDRLEN	0xf
#define V_CPL_TX_TNL_LSO_ETHHDRLEN(x)	((x) << S_CPL_TX_TNL_LSO_ETHHDRLEN)
#define G_CPL_TX_TNL_LSO_ETHHDRLEN(x)	\
	(((x) >> S_CPL_TX_TNL_LSO_ETHHDRLEN) & M_CPL_TX_TNL_LSO_ETHHDRLEN)

#define CPL_TX_TNL_LSO_TNLHDRLEN_S      0
#define CPL_TX_TNL_LSO_TNLHDRLEN_M      0xfff
#define CPL_TX_TNL_LSO_TNLHDRLEN_V(x)	((x) << CPL_TX_TNL_LSO_TNLHDRLEN_S)
#define CPL_TX_TNL_LSO_TNLHDRLEN_G(x)   \
	(((x) >> CPL_TX_TNL_LSO_TNLHDRLEN_S) & CPL_TX_TNL_LSO_TNLHDRLEN_M)

#define CPL_TX_TNL_LSO_IPV6_S		20
#define CPL_TX_TNL_LSO_IPV6_M		0x1
#define CPL_TX_TNL_LSO_IPV6_V(x)	((x) << CPL_TX_TNL_LSO_IPV6_S)
#define CPL_TX_TNL_LSO_IPV6_G(x)	\
	(((x) >> CPL_TX_TNL_LSO_IPV6_S) & CPL_TX_TNL_LSO_IPV6_M)
#define CPL_TX_TNL_LSO_IPV6_F		CPL_TX_TNL_LSO_IPV6_V(1U)

#define ULP_TX_SC_MORE_S 23
#define ULP_TX_SC_MORE_V(x) ((x) << ULP_TX_SC_MORE_S)
#define ULP_TX_SC_MORE_F  ULP_TX_SC_MORE_V(1U)

struct ulp_mem_io {
	WR_HDR;
	__be32 cmd;
	__be32 len16;             /* command length */
	__be32 dlen;              /* data length in 32-byte units */
	__be32 lock_addr;
};

#define ULP_MEMIO_LOCK_S    31
#define ULP_MEMIO_LOCK_V(x) ((x) << ULP_MEMIO_LOCK_S)
#define ULP_MEMIO_LOCK_F    ULP_MEMIO_LOCK_V(1U)

/* additional ulp_mem_io.cmd fields */
#define ULP_MEMIO_ORDER_S    23
#define ULP_MEMIO_ORDER_V(x) ((x) << ULP_MEMIO_ORDER_S)
#define ULP_MEMIO_ORDER_F    ULP_MEMIO_ORDER_V(1U)

#define T5_ULP_MEMIO_IMM_S    23
#define T5_ULP_MEMIO_IMM_V(x) ((x) << T5_ULP_MEMIO_IMM_S)
#define T5_ULP_MEMIO_IMM_F    T5_ULP_MEMIO_IMM_V(1U)

#define T5_ULP_MEMIO_ORDER_S    22
#define T5_ULP_MEMIO_ORDER_V(x) ((x) << T5_ULP_MEMIO_ORDER_S)
#define T5_ULP_MEMIO_ORDER_F    T5_ULP_MEMIO_ORDER_V(1U)

#define T5_ULP_MEMIO_FID_S	4
#define T5_ULP_MEMIO_FID_M	0x7ff
#define T5_ULP_MEMIO_FID_V(x)	((x) << T5_ULP_MEMIO_FID_S)

/* ulp_mem_io.lock_addr fields */
#define ULP_MEMIO_ADDR_S    0
#define ULP_MEMIO_ADDR_V(x) ((x) << ULP_MEMIO_ADDR_S)

/* ulp_mem_io.dlen fields */
#define ULP_MEMIO_DATA_LEN_S    0
#define ULP_MEMIO_DATA_LEN_V(x) ((x) << ULP_MEMIO_DATA_LEN_S)

#define ULPTX_NSGE_S    0
#define ULPTX_NSGE_M    0xFFFF
#define ULPTX_NSGE_V(x) ((x) << ULPTX_NSGE_S)
#define ULPTX_NSGE_G(x) (((x) >> ULPTX_NSGE_S) & ULPTX_NSGE_M)

struct ulptx_sc_memrd {
	__be32 cmd_to_len;
	__be32 addr;
};

#define ULP_TXPKT_DATAMODIFY_S       23
#define ULP_TXPKT_DATAMODIFY_M       0x1
#define ULP_TXPKT_DATAMODIFY_V(x)    ((x) << ULP_TXPKT_DATAMODIFY_S)
#define ULP_TXPKT_DATAMODIFY_G(x)    \
	(((x) >> ULP_TXPKT_DATAMODIFY_S) & ULP_TXPKT_DATAMODIFY__M)
#define ULP_TXPKT_DATAMODIFY_F       ULP_TXPKT_DATAMODIFY_V(1U)

#define ULP_TXPKT_CHANNELID_S        22
#define ULP_TXPKT_CHANNELID_M        0x1
#define ULP_TXPKT_CHANNELID_V(x)     ((x) << ULP_TXPKT_CHANNELID_S)
#define ULP_TXPKT_CHANNELID_G(x)     \
	(((x) >> ULP_TXPKT_CHANNELID_S) & ULP_TXPKT_CHANNELID_M)
#define ULP_TXPKT_CHANNELID_F        ULP_TXPKT_CHANNELID_V(1U)

#define SCMD_SEQ_NO_CTRL_S      29
#define SCMD_SEQ_NO_CTRL_M      0x3
#define SCMD_SEQ_NO_CTRL_V(x)   ((x) << SCMD_SEQ_NO_CTRL_S)
#define SCMD_SEQ_NO_CTRL_G(x)   \
	(((x) >> SCMD_SEQ_NO_CTRL_S) & SCMD_SEQ_NO_CTRL_M)

/* StsFieldPrsnt- Status field at the end of the TLS PDU */
#define SCMD_STATUS_PRESENT_S   28
#define SCMD_STATUS_PRESENT_M   0x1
#define SCMD_STATUS_PRESENT_V(x)    ((x) << SCMD_STATUS_PRESENT_S)
#define SCMD_STATUS_PRESENT_G(x)    \
	(((x) >> SCMD_STATUS_PRESENT_S) & SCMD_STATUS_PRESENT_M)
#define SCMD_STATUS_PRESENT_F   SCMD_STATUS_PRESENT_V(1U)

/* ProtoVersion - Protocol Version 0: 1.2, 1:1.1, 2:DTLS, 3:Generic,
 * 3-15: Reserved.
 */
#define SCMD_PROTO_VERSION_S    24
#define SCMD_PROTO_VERSION_M    0xf
#define SCMD_PROTO_VERSION_V(x) ((x) << SCMD_PROTO_VERSION_S)
#define SCMD_PROTO_VERSION_G(x) \
	(((x) >> SCMD_PROTO_VERSION_S) & SCMD_PROTO_VERSION_M)

/* EncDecCtrl - Encryption/Decryption Control. 0: Encrypt, 1: Decrypt */
#define SCMD_ENC_DEC_CTRL_S     23
#define SCMD_ENC_DEC_CTRL_M     0x1
#define SCMD_ENC_DEC_CTRL_V(x)  ((x) << SCMD_ENC_DEC_CTRL_S)
#define SCMD_ENC_DEC_CTRL_G(x)  \
	(((x) >> SCMD_ENC_DEC_CTRL_S) & SCMD_ENC_DEC_CTRL_M)
#define SCMD_ENC_DEC_CTRL_F SCMD_ENC_DEC_CTRL_V(1U)

/* CipherAuthSeqCtrl - Cipher Authentication Sequence Control. */
#define SCMD_CIPH_AUTH_SEQ_CTRL_S       22
#define SCMD_CIPH_AUTH_SEQ_CTRL_M       0x1
#define SCMD_CIPH_AUTH_SEQ_CTRL_V(x)    \
	((x) << SCMD_CIPH_AUTH_SEQ_CTRL_S)
#define SCMD_CIPH_AUTH_SEQ_CTRL_G(x)    \
	(((x) >> SCMD_CIPH_AUTH_SEQ_CTRL_S) & SCMD_CIPH_AUTH_SEQ_CTRL_M)
#define SCMD_CIPH_AUTH_SEQ_CTRL_F   SCMD_CIPH_AUTH_SEQ_CTRL_V(1U)

/* CiphMode -  Cipher Mode. 0: NOP, 1:AES-CBC, 2:AES-GCM, 3:AES-CTR,
 * 4:Generic-AES, 5-15: Reserved.
 */
#define SCMD_CIPH_MODE_S    18
#define SCMD_CIPH_MODE_M    0xf
#define SCMD_CIPH_MODE_V(x) ((x) << SCMD_CIPH_MODE_S)
#define SCMD_CIPH_MODE_G(x) \
	(((x) >> SCMD_CIPH_MODE_S) & SCMD_CIPH_MODE_M)

/* AuthMode - Auth Mode. 0: NOP, 1:SHA1, 2:SHA2-224, 3:SHA2-256
 * 4-15: Reserved
 */
#define SCMD_AUTH_MODE_S    14
#define SCMD_AUTH_MODE_M    0xf
#define SCMD_AUTH_MODE_V(x) ((x) << SCMD_AUTH_MODE_S)
#define SCMD_AUTH_MODE_G(x) \
	(((x) >> SCMD_AUTH_MODE_S) & SCMD_AUTH_MODE_M)

/* HmacCtrl - HMAC Control. 0:NOP, 1:No truncation, 2:Support HMAC Truncation
 * per RFC 4366, 3:IPSec 96 bits, 4-7:Reserved
 */
#define SCMD_HMAC_CTRL_S    11
#define SCMD_HMAC_CTRL_M    0x7
#define SCMD_HMAC_CTRL_V(x) ((x) << SCMD_HMAC_CTRL_S)
#define SCMD_HMAC_CTRL_G(x) \
	(((x) >> SCMD_HMAC_CTRL_S) & SCMD_HMAC_CTRL_M)

/* IvSize - IV size in units of 2 bytes */
#define SCMD_IV_SIZE_S  7
#define SCMD_IV_SIZE_M  0xf
#define SCMD_IV_SIZE_V(x)   ((x) << SCMD_IV_SIZE_S)
#define SCMD_IV_SIZE_G(x)   \
	(((x) >> SCMD_IV_SIZE_S) & SCMD_IV_SIZE_M)

/* NumIVs - Number of IVs */
#define SCMD_NUM_IVS_S  0
#define SCMD_NUM_IVS_M  0x7f
#define SCMD_NUM_IVS_V(x)   ((x) << SCMD_NUM_IVS_S)
#define SCMD_NUM_IVS_G(x)   \
	(((x) >> SCMD_NUM_IVS_S) & SCMD_NUM_IVS_M)

/* EnbDbgId - If this is enabled upper 20 (63:44) bits if SeqNumber
 * (below) are used as Cid (connection id for debug status), these
 * bits are padded to zero for forming the 64 bit
 * sequence number for TLS
 */
#define SCMD_ENB_DBGID_S  31
#define SCMD_ENB_DBGID_M  0x1
#define SCMD_ENB_DBGID_V(x)   ((x) << SCMD_ENB_DBGID_S)
#define SCMD_ENB_DBGID_G(x)   \
	(((x) >> SCMD_ENB_DBGID_S) & SCMD_ENB_DBGID_M)

/* IV generation in SW. */
#define SCMD_IV_GEN_CTRL_S      30
#define SCMD_IV_GEN_CTRL_M      0x1
#define SCMD_IV_GEN_CTRL_V(x)   ((x) << SCMD_IV_GEN_CTRL_S)
#define SCMD_IV_GEN_CTRL_G(x)   \
	(((x) >> SCMD_IV_GEN_CTRL_S) & SCMD_IV_GEN_CTRL_M)
#define SCMD_IV_GEN_CTRL_F  SCMD_IV_GEN_CTRL_V(1U)

/* More frags */
#define SCMD_MORE_FRAGS_S   20
#define SCMD_MORE_FRAGS_M   0x1
#define SCMD_MORE_FRAGS_V(x)    ((x) << SCMD_MORE_FRAGS_S)
#define SCMD_MORE_FRAGS_G(x)    (((x) >> SCMD_MORE_FRAGS_S) & SCMD_MORE_FRAGS_M)

/*last frag */
#define SCMD_LAST_FRAG_S    19
#define SCMD_LAST_FRAG_M    0x1
#define SCMD_LAST_FRAG_V(x) ((x) << SCMD_LAST_FRAG_S)
#define SCMD_LAST_FRAG_G(x) (((x) >> SCMD_LAST_FRAG_S) & SCMD_LAST_FRAG_M)

/* TlsCompPdu */
#define SCMD_TLS_COMPPDU_S    18
#define SCMD_TLS_COMPPDU_M    0x1
#define SCMD_TLS_COMPPDU_V(x) ((x) << SCMD_TLS_COMPPDU_S)
#define SCMD_TLS_COMPPDU_G(x) (((x) >> SCMD_TLS_COMPPDU_S) & SCMD_TLS_COMPPDU_M)

/* KeyCntxtInline - Key context inline after the scmd  OR PayloadOnly*/
#define SCMD_KEY_CTX_INLINE_S   17
#define SCMD_KEY_CTX_INLINE_M   0x1
#define SCMD_KEY_CTX_INLINE_V(x)    ((x) << SCMD_KEY_CTX_INLINE_S)
#define SCMD_KEY_CTX_INLINE_G(x)    \
	(((x) >> SCMD_KEY_CTX_INLINE_S) & SCMD_KEY_CTX_INLINE_M)
#define SCMD_KEY_CTX_INLINE_F   SCMD_KEY_CTX_INLINE_V(1U)

/* TLSFragEnable - 0: Host created TLS PDUs, 1: TLS Framgmentation in ASIC */
#define SCMD_TLS_FRAG_ENABLE_S  16
#define SCMD_TLS_FRAG_ENABLE_M  0x1
#define SCMD_TLS_FRAG_ENABLE_V(x)   ((x) << SCMD_TLS_FRAG_ENABLE_S)
#define SCMD_TLS_FRAG_ENABLE_G(x)   \
	(((x) >> SCMD_TLS_FRAG_ENABLE_S) & SCMD_TLS_FRAG_ENABLE_M)
#define SCMD_TLS_FRAG_ENABLE_F  SCMD_TLS_FRAG_ENABLE_V(1U)

/* MacOnly - Only send the MAC and discard PDU. This is valid for hash only
 * modes, in this case TLS_TX  will drop the PDU and only
 * send back the MAC bytes.
 */
#define SCMD_MAC_ONLY_S 15
#define SCMD_MAC_ONLY_M 0x1
#define SCMD_MAC_ONLY_V(x)  ((x) << SCMD_MAC_ONLY_S)
#define SCMD_MAC_ONLY_G(x)  \
	(((x) >> SCMD_MAC_ONLY_S) & SCMD_MAC_ONLY_M)
#define SCMD_MAC_ONLY_F SCMD_MAC_ONLY_V(1U)

/* AadIVDrop - Drop the AAD and IV fields. Useful in protocols
 * which have complex AAD and IV formations Eg:AES-CCM
 */
#define SCMD_AADIVDROP_S 14
#define SCMD_AADIVDROP_M 0x1
#define SCMD_AADIVDROP_V(x)  ((x) << SCMD_AADIVDROP_S)
#define SCMD_AADIVDROP_G(x)  \
	(((x) >> SCMD_AADIVDROP_S) & SCMD_AADIVDROP_M)
#define SCMD_AADIVDROP_F SCMD_AADIVDROP_V(1U)

/* HdrLength - Length of all headers excluding TLS header
 * present before start of crypto PDU/payload.
 */
#define SCMD_HDR_LEN_S  0
#define SCMD_HDR_LEN_M  0x3fff
#define SCMD_HDR_LEN_V(x)   ((x) << SCMD_HDR_LEN_S)
#define SCMD_HDR_LEN_G(x)   \
	(((x) >> SCMD_HDR_LEN_S) & SCMD_HDR_LEN_M)

struct cpl_tx_sec_pdu {
	__be32 op_ivinsrtofst;
	__be32 pldlen;
	__be32 aadstart_cipherstop_hi;
	__be32 cipherstop_lo_authinsert;
	__be32 seqno_numivs;
	__be32 ivgen_hdrlen;
	__be64 scmd1;
};

#define CPL_TX_SEC_PDU_OPCODE_S     24
#define CPL_TX_SEC_PDU_OPCODE_M     0xff
#define CPL_TX_SEC_PDU_OPCODE_V(x)  ((x) << CPL_TX_SEC_PDU_OPCODE_S)
#define CPL_TX_SEC_PDU_OPCODE_G(x)  \
	(((x) >> CPL_TX_SEC_PDU_OPCODE_S) & CPL_TX_SEC_PDU_OPCODE_M)

/* RX Channel Id */
#define CPL_TX_SEC_PDU_RXCHID_S  22
#define CPL_TX_SEC_PDU_RXCHID_M  0x1
#define CPL_TX_SEC_PDU_RXCHID_V(x)   ((x) << CPL_TX_SEC_PDU_RXCHID_S)
#define CPL_TX_SEC_PDU_RXCHID_G(x)   \
	(((x) >> CPL_TX_SEC_PDU_RXCHID_S) & CPL_TX_SEC_PDU_RXCHID_M)
#define CPL_TX_SEC_PDU_RXCHID_F  CPL_TX_SEC_PDU_RXCHID_V(1U)

/* Ack Follows */
#define CPL_TX_SEC_PDU_ACKFOLLOWS_S  21
#define CPL_TX_SEC_PDU_ACKFOLLOWS_M  0x1
#define CPL_TX_SEC_PDU_ACKFOLLOWS_V(x)   ((x) << CPL_TX_SEC_PDU_ACKFOLLOWS_S)
#define CPL_TX_SEC_PDU_ACKFOLLOWS_G(x)   \
	(((x) >> CPL_TX_SEC_PDU_ACKFOLLOWS_S) & CPL_TX_SEC_PDU_ACKFOLLOWS_M)
#define CPL_TX_SEC_PDU_ACKFOLLOWS_F  CPL_TX_SEC_PDU_ACKFOLLOWS_V(1U)

/* Loopback bit in cpl_tx_sec_pdu */
#define CPL_TX_SEC_PDU_ULPTXLPBK_S  20
#define CPL_TX_SEC_PDU_ULPTXLPBK_M  0x1
#define CPL_TX_SEC_PDU_ULPTXLPBK_V(x)   ((x) << CPL_TX_SEC_PDU_ULPTXLPBK_S)
#define CPL_TX_SEC_PDU_ULPTXLPBK_G(x)   \
	(((x) >> CPL_TX_SEC_PDU_ULPTXLPBK_S) & CPL_TX_SEC_PDU_ULPTXLPBK_M)
#define CPL_TX_SEC_PDU_ULPTXLPBK_F  CPL_TX_SEC_PDU_ULPTXLPBK_V(1U)

/* Length of cpl header encapsulated */
#define CPL_TX_SEC_PDU_CPLLEN_S     16
#define CPL_TX_SEC_PDU_CPLLEN_M     0xf
#define CPL_TX_SEC_PDU_CPLLEN_V(x)  ((x) << CPL_TX_SEC_PDU_CPLLEN_S)
#define CPL_TX_SEC_PDU_CPLLEN_G(x)  \
	(((x) >> CPL_TX_SEC_PDU_CPLLEN_S) & CPL_TX_SEC_PDU_CPLLEN_M)

/* PlaceHolder */
#define CPL_TX_SEC_PDU_PLACEHOLDER_S    10
#define CPL_TX_SEC_PDU_PLACEHOLDER_M    0x1
#define CPL_TX_SEC_PDU_PLACEHOLDER_V(x) ((x) << CPL_TX_SEC_PDU_PLACEHOLDER_S)
#define CPL_TX_SEC_PDU_PLACEHOLDER_G(x) \
	(((x) >> CPL_TX_SEC_PDU_PLACEHOLDER_S) & \
	 CPL_TX_SEC_PDU_PLACEHOLDER_M)

/* IvInsrtOffset: Insertion location for IV */
#define CPL_TX_SEC_PDU_IVINSRTOFST_S    0
#define CPL_TX_SEC_PDU_IVINSRTOFST_M    0x3ff
#define CPL_TX_SEC_PDU_IVINSRTOFST_V(x) ((x) << CPL_TX_SEC_PDU_IVINSRTOFST_S)
#define CPL_TX_SEC_PDU_IVINSRTOFST_G(x) \
	(((x) >> CPL_TX_SEC_PDU_IVINSRTOFST_S) & \
	 CPL_TX_SEC_PDU_IVINSRTOFST_M)

/* AadStartOffset: Offset in bytes for AAD start from
 * the first byte following the pkt headers (0-255 bytes)
 */
#define CPL_TX_SEC_PDU_AADSTART_S   24
#define CPL_TX_SEC_PDU_AADSTART_M   0xff
#define CPL_TX_SEC_PDU_AADSTART_V(x)    ((x) << CPL_TX_SEC_PDU_AADSTART_S)
#define CPL_TX_SEC_PDU_AADSTART_G(x)    \
	(((x) >> CPL_TX_SEC_PDU_AADSTART_S) & \
	 CPL_TX_SEC_PDU_AADSTART_M)

/* AadStopOffset: offset in bytes for AAD stop/end from the first byte following
 * the pkt headers (0-511 bytes)
 */
#define CPL_TX_SEC_PDU_AADSTOP_S    15
#define CPL_TX_SEC_PDU_AADSTOP_M    0x1ff
#define CPL_TX_SEC_PDU_AADSTOP_V(x) ((x) << CPL_TX_SEC_PDU_AADSTOP_S)
#define CPL_TX_SEC_PDU_AADSTOP_G(x) \
	(((x) >> CPL_TX_SEC_PDU_AADSTOP_S) & CPL_TX_SEC_PDU_AADSTOP_M)

/* CipherStartOffset: offset in bytes for encryption/decryption start from the
 * first byte following the pkt headers (0-1023 bytes)
 */
#define CPL_TX_SEC_PDU_CIPHERSTART_S    5
#define CPL_TX_SEC_PDU_CIPHERSTART_M    0x3ff
#define CPL_TX_SEC_PDU_CIPHERSTART_V(x) ((x) << CPL_TX_SEC_PDU_CIPHERSTART_S)
#define CPL_TX_SEC_PDU_CIPHERSTART_G(x) \
	(((x) >> CPL_TX_SEC_PDU_CIPHERSTART_S) & \
	 CPL_TX_SEC_PDU_CIPHERSTART_M)

/* CipherStopOffset: offset in bytes for encryption/decryption end
 * from end of the payload of this command (0-511 bytes)
 */
#define CPL_TX_SEC_PDU_CIPHERSTOP_HI_S      0
#define CPL_TX_SEC_PDU_CIPHERSTOP_HI_M      0x1f
#define CPL_TX_SEC_PDU_CIPHERSTOP_HI_V(x)   \
	((x) << CPL_TX_SEC_PDU_CIPHERSTOP_HI_S)
#define CPL_TX_SEC_PDU_CIPHERSTOP_HI_G(x)   \
	(((x) >> CPL_TX_SEC_PDU_CIPHERSTOP_HI_S) & \
	 CPL_TX_SEC_PDU_CIPHERSTOP_HI_M)

#define CPL_TX_SEC_PDU_CIPHERSTOP_LO_S      28
#define CPL_TX_SEC_PDU_CIPHERSTOP_LO_M      0xf
#define CPL_TX_SEC_PDU_CIPHERSTOP_LO_V(x)   \
	((x) << CPL_TX_SEC_PDU_CIPHERSTOP_LO_S)
#define CPL_TX_SEC_PDU_CIPHERSTOP_LO_G(x)   \
	(((x) >> CPL_TX_SEC_PDU_CIPHERSTOP_LO_S) & \
	 CPL_TX_SEC_PDU_CIPHERSTOP_LO_M)

/* AuthStartOffset: offset in bytes for authentication start from
 * the first byte following the pkt headers (0-1023)
 */
#define CPL_TX_SEC_PDU_AUTHSTART_S  18
#define CPL_TX_SEC_PDU_AUTHSTART_M  0x3ff
#define CPL_TX_SEC_PDU_AUTHSTART_V(x)   ((x) << CPL_TX_SEC_PDU_AUTHSTART_S)
#define CPL_TX_SEC_PDU_AUTHSTART_G(x)   \
	(((x) >> CPL_TX_SEC_PDU_AUTHSTART_S) & \
	 CPL_TX_SEC_PDU_AUTHSTART_M)

/* AuthStopOffset: offset in bytes for authentication
 * end from end of the payload of this command (0-511 Bytes)
 */
#define CPL_TX_SEC_PDU_AUTHSTOP_S   9
#define CPL_TX_SEC_PDU_AUTHSTOP_M   0x1ff
#define CPL_TX_SEC_PDU_AUTHSTOP_V(x)    ((x) << CPL_TX_SEC_PDU_AUTHSTOP_S)
#define CPL_TX_SEC_PDU_AUTHSTOP_G(x)    \
	(((x) >> CPL_TX_SEC_PDU_AUTHSTOP_S) & \
	 CPL_TX_SEC_PDU_AUTHSTOP_M)

/* AuthInsrtOffset: offset in bytes for authentication insertion
 * from end of the payload of this command (0-511 bytes)
 */
#define CPL_TX_SEC_PDU_AUTHINSERT_S 0
#define CPL_TX_SEC_PDU_AUTHINSERT_M 0x1ff
#define CPL_TX_SEC_PDU_AUTHINSERT_V(x)  ((x) << CPL_TX_SEC_PDU_AUTHINSERT_S)
#define CPL_TX_SEC_PDU_AUTHINSERT_G(x)  \
	(((x) >> CPL_TX_SEC_PDU_AUTHINSERT_S) & \
	 CPL_TX_SEC_PDU_AUTHINSERT_M)

struct cpl_rx_phys_dsgl {
	__be32 op_to_tid;
	__be32 pcirlxorder_to_noofsgentr;
	struct rss_header rss_hdr_int;
};

#define CPL_RX_PHYS_DSGL_OPCODE_S       24
#define CPL_RX_PHYS_DSGL_OPCODE_M       0xff
#define CPL_RX_PHYS_DSGL_OPCODE_V(x)    ((x) << CPL_RX_PHYS_DSGL_OPCODE_S)
#define CPL_RX_PHYS_DSGL_OPCODE_G(x)    \
	(((x) >> CPL_RX_PHYS_DSGL_OPCODE_S) & CPL_RX_PHYS_DSGL_OPCODE_M)

#define CPL_RX_PHYS_DSGL_ISRDMA_S       23
#define CPL_RX_PHYS_DSGL_ISRDMA_M       0x1
#define CPL_RX_PHYS_DSGL_ISRDMA_V(x)    ((x) << CPL_RX_PHYS_DSGL_ISRDMA_S)
#define CPL_RX_PHYS_DSGL_ISRDMA_G(x)    \
	(((x) >> CPL_RX_PHYS_DSGL_ISRDMA_S) & CPL_RX_PHYS_DSGL_ISRDMA_M)
#define CPL_RX_PHYS_DSGL_ISRDMA_F       CPL_RX_PHYS_DSGL_ISRDMA_V(1U)

#define CPL_RX_PHYS_DSGL_RSVD1_S        20
#define CPL_RX_PHYS_DSGL_RSVD1_M        0x7
#define CPL_RX_PHYS_DSGL_RSVD1_V(x)     ((x) << CPL_RX_PHYS_DSGL_RSVD1_S)
#define CPL_RX_PHYS_DSGL_RSVD1_G(x)     \
	(((x) >> CPL_RX_PHYS_DSGL_RSVD1_S) & \
	 CPL_RX_PHYS_DSGL_RSVD1_M)

#define CPL_RX_PHYS_DSGL_PCIRLXORDER_S          31
#define CPL_RX_PHYS_DSGL_PCIRLXORDER_M          0x1
#define CPL_RX_PHYS_DSGL_PCIRLXORDER_V(x)       \
	((x) << CPL_RX_PHYS_DSGL_PCIRLXORDER_S)
#define CPL_RX_PHYS_DSGL_PCIRLXORDER_G(x)       \
	(((x) >> CPL_RX_PHYS_DSGL_PCIRLXORDER_S) & \
	 CPL_RX_PHYS_DSGL_PCIRLXORDER_M)
#define CPL_RX_PHYS_DSGL_PCIRLXORDER_F  CPL_RX_PHYS_DSGL_PCIRLXORDER_V(1U)

#define CPL_RX_PHYS_DSGL_PCINOSNOOP_S           30
#define CPL_RX_PHYS_DSGL_PCINOSNOOP_M           0x1
#define CPL_RX_PHYS_DSGL_PCINOSNOOP_V(x)        \
	((x) << CPL_RX_PHYS_DSGL_PCINOSNOOP_S)
#define CPL_RX_PHYS_DSGL_PCINOSNOOP_G(x)        \
	(((x) >> CPL_RX_PHYS_DSGL_PCINOSNOOP_S) & \
	 CPL_RX_PHYS_DSGL_PCINOSNOOP_M)

#define CPL_RX_PHYS_DSGL_PCINOSNOOP_F   CPL_RX_PHYS_DSGL_PCINOSNOOP_V(1U)

#define CPL_RX_PHYS_DSGL_PCITPHNTENB_S          29
#define CPL_RX_PHYS_DSGL_PCITPHNTENB_M          0x1
#define CPL_RX_PHYS_DSGL_PCITPHNTENB_V(x)       \
	((x) << CPL_RX_PHYS_DSGL_PCITPHNTENB_S)
#define CPL_RX_PHYS_DSGL_PCITPHNTENB_G(x)       \
	(((x) >> CPL_RX_PHYS_DSGL_PCITPHNTENB_S) & \
	 CPL_RX_PHYS_DSGL_PCITPHNTENB_M)
#define CPL_RX_PHYS_DSGL_PCITPHNTENB_F  CPL_RX_PHYS_DSGL_PCITPHNTENB_V(1U)

#define CPL_RX_PHYS_DSGL_PCITPHNT_S     27
#define CPL_RX_PHYS_DSGL_PCITPHNT_M     0x3
#define CPL_RX_PHYS_DSGL_PCITPHNT_V(x)  ((x) << CPL_RX_PHYS_DSGL_PCITPHNT_S)
#define CPL_RX_PHYS_DSGL_PCITPHNT_G(x)  \
	(((x) >> CPL_RX_PHYS_DSGL_PCITPHNT_S) & \
	 CPL_RX_PHYS_DSGL_PCITPHNT_M)

#define CPL_RX_PHYS_DSGL_DCAID_S        16
#define CPL_RX_PHYS_DSGL_DCAID_M        0x7ff
#define CPL_RX_PHYS_DSGL_DCAID_V(x)     ((x) << CPL_RX_PHYS_DSGL_DCAID_S)
#define CPL_RX_PHYS_DSGL_DCAID_G(x)     \
	(((x) >> CPL_RX_PHYS_DSGL_DCAID_S) & \
	 CPL_RX_PHYS_DSGL_DCAID_M)

#define CPL_RX_PHYS_DSGL_NOOFSGENTR_S           0
#define CPL_RX_PHYS_DSGL_NOOFSGENTR_M           0xffff
#define CPL_RX_PHYS_DSGL_NOOFSGENTR_V(x)        \
	((x) << CPL_RX_PHYS_DSGL_NOOFSGENTR_S)
#define CPL_RX_PHYS_DSGL_NOOFSGENTR_G(x)        \
	(((x) >> CPL_RX_PHYS_DSGL_NOOFSGENTR_S) & \
	 CPL_RX_PHYS_DSGL_NOOFSGENTR_M)

struct cpl_rx_mps_pkt {
	__be32 op_to_r1_hi;
	__be32 r1_lo_length;
};

#define CPL_RX_MPS_PKT_OP_S     24
#define CPL_RX_MPS_PKT_OP_M     0xff
#define CPL_RX_MPS_PKT_OP_V(x)  ((x) << CPL_RX_MPS_PKT_OP_S)
#define CPL_RX_MPS_PKT_OP_G(x)  \
	(((x) >> CPL_RX_MPS_PKT_OP_S) & CPL_RX_MPS_PKT_OP_M)

#define CPL_RX_MPS_PKT_TYPE_S           20
#define CPL_RX_MPS_PKT_TYPE_M           0xf
#define CPL_RX_MPS_PKT_TYPE_V(x)        ((x) << CPL_RX_MPS_PKT_TYPE_S)
#define CPL_RX_MPS_PKT_TYPE_G(x)        \
	(((x) >> CPL_RX_MPS_PKT_TYPE_S) & CPL_RX_MPS_PKT_TYPE_M)

enum {
	X_CPL_RX_MPS_PKT_TYPE_PAUSE = 1 << 0,
	X_CPL_RX_MPS_PKT_TYPE_PPP   = 1 << 1,
	X_CPL_RX_MPS_PKT_TYPE_QFC   = 1 << 2,
	X_CPL_RX_MPS_PKT_TYPE_PTP   = 1 << 3
};

struct cpl_srq_table_req {
	WR_HDR;
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[2];
	__u8 idx;
	__be64 rsvd_pdid;
	__be32 qlen_qbase;
	__be16 cur_msn;
	__be16 max_msn;
};

struct cpl_srq_table_rpl {
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[2];
	__u8 idx;
	__be64 rsvd_pdid;
	__be32 qlen_qbase;
	__be16 cur_msn;
	__be16 max_msn;
};

/* cpl_srq_table_{req,rpl}.params fields */
#define SRQT_QLEN_S   28
#define SRQT_QLEN_M   0xF
#define SRQT_QLEN_V(x) ((x) << SRQT_QLEN_S)
#define SRQT_QLEN_G(x) (((x) >> SRQT_QLEN_S) & SRQT_QLEN_M)

#define SRQT_QBASE_S    0
#define SRQT_QBASE_M   0x3FFFFFF
#define SRQT_QBASE_V(x) ((x) << SRQT_QBASE_S)
#define SRQT_QBASE_G(x) (((x) >> SRQT_QBASE_S) & SRQT_QBASE_M)

#define SRQT_PDID_S    0
#define SRQT_PDID_M   0xFF
#define SRQT_PDID_V(x) ((x) << SRQT_PDID_S)
#define SRQT_PDID_G(x) (((x) >> SRQT_PDID_S) & SRQT_PDID_M)

#define SRQT_IDX_S    0
#define SRQT_IDX_M    0xF
#define SRQT_IDX_V(x) ((x) << SRQT_IDX_S)
#define SRQT_IDX_G(x) (((x) >> SRQT_IDX_S) & SRQT_IDX_M)

struct cpl_tx_tls_sfo {
	__be32 op_to_seg_len;
	__be32 pld_len;
	__be32 type_protover;
	__be32 r1_lo;
	__be32 seqno_numivs;
	__be32 ivgen_hdrlen;
	__be64 scmd1;
};

/* cpl_tx_tls_sfo macros */
#define CPL_TX_TLS_SFO_OPCODE_S         24
#define CPL_TX_TLS_SFO_OPCODE_V(x)      ((x) << CPL_TX_TLS_SFO_OPCODE_S)

#define CPL_TX_TLS_SFO_DATA_TYPE_S      20
#define CPL_TX_TLS_SFO_DATA_TYPE_V(x)   ((x) << CPL_TX_TLS_SFO_DATA_TYPE_S)

#define CPL_TX_TLS_SFO_CPL_LEN_S        16
#define CPL_TX_TLS_SFO_CPL_LEN_V(x)     ((x) << CPL_TX_TLS_SFO_CPL_LEN_S)

#define CPL_TX_TLS_SFO_SEG_LEN_S        0
#define CPL_TX_TLS_SFO_SEG_LEN_M        0xffff
#define CPL_TX_TLS_SFO_SEG_LEN_V(x)     ((x) << CPL_TX_TLS_SFO_SEG_LEN_S)
#define CPL_TX_TLS_SFO_SEG_LEN_G(x)     \
	(((x) >> CPL_TX_TLS_SFO_SEG_LEN_S) & CPL_TX_TLS_SFO_SEG_LEN_M)

#define CPL_TX_TLS_SFO_TYPE_S           24
#define CPL_TX_TLS_SFO_TYPE_M           0xff
#define CPL_TX_TLS_SFO_TYPE_V(x)        ((x) << CPL_TX_TLS_SFO_TYPE_S)
#define CPL_TX_TLS_SFO_TYPE_G(x)        \
	(((x) >> CPL_TX_TLS_SFO_TYPE_S) & CPL_TX_TLS_SFO_TYPE_M)

#define CPL_TX_TLS_SFO_PROTOVER_S       8
#define CPL_TX_TLS_SFO_PROTOVER_M       0xffff
#define CPL_TX_TLS_SFO_PROTOVER_V(x)    ((x) << CPL_TX_TLS_SFO_PROTOVER_S)
#define CPL_TX_TLS_SFO_PROTOVER_G(x)    \
	(((x) >> CPL_TX_TLS_SFO_PROTOVER_S) & CPL_TX_TLS_SFO_PROTOVER_M)

struct cpl_tls_data {
	struct rss_header rsshdr;
	union opcode_tid ot;
	__be32 length_pkd;
	__be32 seq;
	__be32 r1;
};

#define CPL_TLS_DATA_OPCODE_S           24
#define CPL_TLS_DATA_OPCODE_M           0xff
#define CPL_TLS_DATA_OPCODE_V(x)        ((x) << CPL_TLS_DATA_OPCODE_S)
#define CPL_TLS_DATA_OPCODE_G(x)        \
	(((x) >> CPL_TLS_DATA_OPCODE_S) & CPL_TLS_DATA_OPCODE_M)

#define CPL_TLS_DATA_TID_S              0
#define CPL_TLS_DATA_TID_M              0xffffff
#define CPL_TLS_DATA_TID_V(x)           ((x) << CPL_TLS_DATA_TID_S)
#define CPL_TLS_DATA_TID_G(x)           \
	(((x) >> CPL_TLS_DATA_TID_S) & CPL_TLS_DATA_TID_M)

#define CPL_TLS_DATA_LENGTH_S           0
#define CPL_TLS_DATA_LENGTH_M           0xffff
#define CPL_TLS_DATA_LENGTH_V(x)        ((x) << CPL_TLS_DATA_LENGTH_S)
#define CPL_TLS_DATA_LENGTH_G(x)        \
	(((x) >> CPL_TLS_DATA_LENGTH_S) & CPL_TLS_DATA_LENGTH_M)

struct cpl_rx_tls_cmp {
	struct rss_header rsshdr;
	union opcode_tid ot;
	__be32 pdulength_length;
	__be32 seq;
	__be32 ddp_report;
	__be32 r;
	__be32 ddp_valid;
};

#define CPL_RX_TLS_CMP_OPCODE_S         24
#define CPL_RX_TLS_CMP_OPCODE_M         0xff
#define CPL_RX_TLS_CMP_OPCODE_V(x)      ((x) << CPL_RX_TLS_CMP_OPCODE_S)
#define CPL_RX_TLS_CMP_OPCODE_G(x)      \
	(((x) >> CPL_RX_TLS_CMP_OPCODE_S) & CPL_RX_TLS_CMP_OPCODE_M)

#define CPL_RX_TLS_CMP_TID_S            0
#define CPL_RX_TLS_CMP_TID_M            0xffffff
#define CPL_RX_TLS_CMP_TID_V(x)         ((x) << CPL_RX_TLS_CMP_TID_S)
#define CPL_RX_TLS_CMP_TID_G(x)         \
	(((x) >> CPL_RX_TLS_CMP_TID_S) & CPL_RX_TLS_CMP_TID_M)

#define CPL_RX_TLS_CMP_PDULENGTH_S      16
#define CPL_RX_TLS_CMP_PDULENGTH_M      0xffff
#define CPL_RX_TLS_CMP_PDULENGTH_V(x)   ((x) << CPL_RX_TLS_CMP_PDULENGTH_S)
#define CPL_RX_TLS_CMP_PDULENGTH_G(x)   \
	(((x) >> CPL_RX_TLS_CMP_PDULENGTH_S) & CPL_RX_TLS_CMP_PDULENGTH_M)

#define CPL_RX_TLS_CMP_LENGTH_S         0
#define CPL_RX_TLS_CMP_LENGTH_M         0xffff
#define CPL_RX_TLS_CMP_LENGTH_V(x)      ((x) << CPL_RX_TLS_CMP_LENGTH_S)
#define CPL_RX_TLS_CMP_LENGTH_G(x)      \
	(((x) >> CPL_RX_TLS_CMP_LENGTH_S) & CPL_RX_TLS_CMP_LENGTH_M)
#endif  /* __T4_MSG_H */
