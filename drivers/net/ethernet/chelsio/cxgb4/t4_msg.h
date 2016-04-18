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
	CPL_TID_RELEASE       = 0x1A,
	CPL_TX_DATA_ISO	      = 0x1F,

	CPL_CLOSE_LISTSRV_RPL = 0x20,
	CPL_L2T_WRITE_RPL     = 0x23,
	CPL_PASS_OPEN_RPL     = 0x24,
	CPL_ACT_OPEN_RPL      = 0x25,
	CPL_PEER_CLOSE        = 0x26,
	CPL_ABORT_REQ_RSS     = 0x2B,
	CPL_ABORT_RPL_RSS     = 0x2D,

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
	CPL_TRACE_PKT_T5      = 0x48,
	CPL_RX_ISCSI_DDP      = 0x49,

	CPL_RDMA_READ_REQ     = 0x60,

	CPL_PASS_OPEN_REQ6    = 0x81,
	CPL_ACT_OPEN_REQ6     = 0x83,

	CPL_RDMA_TERMINATE    = 0xA2,
	CPL_RDMA_WRITE        = 0xA4,
	CPL_SGE_EGR_UPDATE    = 0xA5,

	CPL_TRACE_PKT         = 0xB0,
	CPL_ISCSI_DATA	      = 0xB2,

	CPL_FW4_MSG           = 0xC0,
	CPL_FW4_PLD           = 0xC1,
	CPL_FW4_ACK           = 0xC3,

	CPL_FW6_MSG           = 0xE0,
	CPL_FW6_PLD           = 0xE1,
	CPL_TX_PKT_LSO        = 0xED,
	CPL_TX_PKT_XT         = 0xEE,

	NUM_CPL_CMDS
};

enum CPL_error {
	CPL_ERR_NONE               = 0,
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

struct cpl_set_tcb_field {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 word_cookie;
	__be64 mask;
	__be64 val;
};

/* cpl_set_tcb_field.word_cookie fields */
#define TCB_WORD_S    0
#define TCB_WORD(x)   ((x) << TCB_WORD_S)

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

enum {
	ULP_TX_MEM_READ = 2,
	ULP_TX_MEM_WRITE = 3,
	ULP_TX_PKT = 4
};

enum {
	ULP_TX_SC_NOOP = 0x80,
	ULP_TX_SC_IMM  = 0x81,
	ULP_TX_SC_DSGL = 0x82,
	ULP_TX_SC_ISGL = 0x83
};

#define ULPTX_CMD_S    24
#define ULPTX_CMD_V(x) ((x) << ULPTX_CMD_S)

struct ulptx_sge_pair {
	__be32 len[2];
	__be64 addr[2];
};

struct ulptx_sgl {
	__be32 cmd_nsge;
	__be32 len0;
	__be64 addr0;
	struct ulptx_sge_pair sge[0];
};

struct ulptx_idata {
	__be32 cmd_more;
	__be32 len;
};

#define ULPTX_NSGE_S    0
#define ULPTX_NSGE_V(x) ((x) << ULPTX_NSGE_S)

#define ULPTX_MORE_S	23
#define ULPTX_MORE_V(x)	((x) << ULPTX_MORE_S)
#define ULPTX_MORE_F	ULPTX_MORE_V(1U)

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

/* ulp_mem_io.lock_addr fields */
#define ULP_MEMIO_ADDR_S    0
#define ULP_MEMIO_ADDR_V(x) ((x) << ULP_MEMIO_ADDR_S)

/* ulp_mem_io.dlen fields */
#define ULP_MEMIO_DATA_LEN_S    0
#define ULP_MEMIO_DATA_LEN_V(x) ((x) << ULP_MEMIO_DATA_LEN_S)

#endif  /* __T4_MSG_H */
