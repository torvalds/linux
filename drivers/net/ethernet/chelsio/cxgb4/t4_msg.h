/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2010 Chelsio Communications, Inc. All rights reserved.
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

	CPL_RDMA_READ_REQ     = 0x60,

	CPL_PASS_OPEN_REQ6    = 0x81,
	CPL_ACT_OPEN_REQ6     = 0x83,

	CPL_RDMA_TERMINATE    = 0xA2,
	CPL_RDMA_WRITE        = 0xA4,
	CPL_SGE_EGR_UPDATE    = 0xA5,

	CPL_TRACE_PKT         = 0xB0,

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

#define CPL_OPCODE(x) ((x) << 24)
#define G_CPL_OPCODE(x) (((x) >> 24) & 0xFF)
#define MK_OPCODE_TID(opcode, tid) (CPL_OPCODE(opcode) | (tid))
#define OPCODE_TID(cmd) ((cmd)->ot.opcode_tid)
#define GET_TID(cmd) (ntohl(OPCODE_TID(cmd)) & 0xFFFFFF)

/* partitioning of TID fields that also carry a queue id */
#define GET_TID_TID(x) ((x) & 0x3fff)
#define GET_TID_QID(x) (((x) >> 14) & 0x3ff)
#define TID_QID(x)     ((x) << 14)

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
#define S_WR_OP    24
#define V_WR_OP(x) ((__u64)(x) << S_WR_OP)

#define WR_HDR struct work_request_hdr wr

/* option 0 fields */
#define S_MSS_IDX    60
#define M_MSS_IDX    0xF
#define V_MSS_IDX(x) ((__u64)(x) << S_MSS_IDX)
#define G_MSS_IDX(x) (((x) >> S_MSS_IDX) & M_MSS_IDX)

/* option 2 fields */
#define S_RSS_QUEUE    0
#define M_RSS_QUEUE    0x3FF
#define V_RSS_QUEUE(x) ((x) << S_RSS_QUEUE)
#define G_RSS_QUEUE(x) (((x) >> S_RSS_QUEUE) & M_RSS_QUEUE)

struct cpl_pass_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
#define TX_CHAN(x)    ((x) << 2)
#define NO_CONG(x)    ((x) << 4)
#define DELACK(x)     ((x) << 5)
#define ULP_MODE(x)   ((x) << 8)
#define RCV_BUFSIZ(x) ((x) << 12)
#define RCV_BUFSIZ_MASK 0x3FFU
#define DSCP(x)       ((x) << 22)
#define SMAC_SEL(x)   ((u64)(x) << 28)
#define L2T_IDX(x)    ((u64)(x) << 36)
#define TCAM_BYPASS(x) ((u64)(x) << 48)
#define NAGLE(x)      ((u64)(x) << 49)
#define WND_SCALE(x)  ((u64)(x) << 50)
#define KEEP_ALIVE(x) ((u64)(x) << 54)
#define MSS_IDX(x)    ((u64)(x) << 60)
	__be64 opt1;
#define SYN_RSS_ENABLE   (1 << 0)
#define SYN_RSS_QUEUE(x) ((x) << 2)
#define CONN_POLICY_ASK  (1 << 22)
};

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

struct cpl_pass_accept_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 opt2;
#define RSS_QUEUE(x)         ((x) << 0)
#define RSS_QUEUE_VALID      (1 << 10)
#define RX_COALESCE_VALID(x) ((x) << 11)
#define RX_COALESCE(x)       ((x) << 12)
#define PACE(x)	      ((x) << 16)
#define TX_QUEUE(x)          ((x) << 23)
#define RX_CHANNEL(x)        ((x) << 26)
#define CCTRL_ECN(x)         ((x) << 27)
#define WND_SCALE_EN(x)      ((x) << 28)
#define TSTAMPS_EN(x)        ((x) << 29)
#define SACK_EN(x)           ((x) << 30)
	__be64 opt0;
};

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

#define S_FILTER_TUPLE  24
#define M_FILTER_TUPLE  0xFFFFFFFFFF
#define V_FILTER_TUPLE(x) ((x) << S_FILTER_TUPLE)
#define G_FILTER_TUPLE(x) (((x) >> S_FILTER_TUPLE) & M_FILTER_TUPLE)
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

struct cpl_act_open_rpl {
	union opcode_tid ot;
	__be32 atid_status;
#define GET_AOPEN_STATUS(x) ((x) & 0xff)
#define GET_AOPEN_ATID(x)   (((x) >> 8) & 0xffffff)
};

struct cpl_pass_establish {
	union opcode_tid ot;
	__be32 rsvd;
	__be32 tos_stid;
#define PASS_OPEN_TID(x) ((x) << 0)
#define PASS_OPEN_TOS(x) ((x) << 24)
#define GET_PASS_OPEN_TID(x)	(((x) >> 0) & 0xFFFFFF)
#define GET_POPEN_TID(x) ((x) & 0xffffff)
#define GET_POPEN_TOS(x) (((x) >> 24) & 0xff)
	__be16 mac_idx;
	__be16 tcp_opt;
#define GET_TCPOPT_WSCALE_OK(x)  (((x) >> 5) & 1)
#define GET_TCPOPT_SACK(x)       (((x) >> 6) & 1)
#define GET_TCPOPT_TSTAMP(x)     (((x) >> 7) & 1)
#define GET_TCPOPT_SND_WSCALE(x) (((x) >> 8) & 0xf)
#define GET_TCPOPT_MSS(x)        (((x) >> 12) & 0xf)
	__be32 snd_isn;
	__be32 rcv_isn;
};

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
#define QUEUENO(x)    ((x) << 0)
#define REPLY_CHAN(x) ((x) << 14)
#define NO_REPLY(x)   ((x) << 15)
	__be16 cookie;
};

struct cpl_set_tcb_field {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 word_cookie;
#define TCB_WORD(x)   ((x) << 0)
#define TCB_COOKIE(x) ((x) << 5)
#define GET_TCB_COOKIE(x) (((x) >> 5) & 7)
	__be64 mask;
	__be64 val;
};

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
#define LISTSVR_IPV6(x) ((x) << 14)
	__be16 rsvd;
};

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
#define TXPKT_VF(x)        ((x) << 0)
#define TXPKT_PF(x)        ((x) << 8)
#define TXPKT_VF_VLD       (1 << 11)
#define TXPKT_OVLAN_IDX(x) ((x) << 12)
#define TXPKT_INTF(x)      ((x) << 16)
#define TXPKT_INS_OVLAN    (1 << 21)
#define TXPKT_OPCODE(x)    ((x) << 24)
	__be16 pack;
	__be16 len;
	__be64 ctrl1;
#define TXPKT_CSUM_END(x)   ((x) << 12)
#define TXPKT_CSUM_START(x) ((x) << 20)
#define TXPKT_IPHDR_LEN(x)  ((u64)(x) << 20)
#define TXPKT_CSUM_LOC(x)   ((u64)(x) << 30)
#define TXPKT_ETHHDR_LEN(x) ((u64)(x) << 34)
#define TXPKT_CSUM_TYPE(x)  ((u64)(x) << 40)
#define TXPKT_VLAN(x)       ((u64)(x) << 44)
#define TXPKT_VLAN_VLD      (1ULL << 60)
#define TXPKT_IPCSUM_DIS    (1ULL << 62)
#define TXPKT_L4CSUM_DIS    (1ULL << 63)
};

struct cpl_tx_pkt {
	WR_HDR;
	struct cpl_tx_pkt_core c;
};

#define cpl_tx_pkt_xt cpl_tx_pkt

struct cpl_tx_pkt_lso_core {
	__be32 lso_ctrl;
#define LSO_TCPHDR_LEN(x) ((x) << 0)
#define LSO_IPHDR_LEN(x)  ((x) << 4)
#define LSO_ETHHDR_LEN(x) ((x) << 16)
#define LSO_IPV6(x)       ((x) << 20)
#define LSO_LAST_SLICE    (1 << 22)
#define LSO_FIRST_SLICE   (1 << 23)
#define LSO_OPCODE(x)     ((x) << 24)
	__be16 ipid_ofst;
	__be16 mss;
	__be32 seqno_offset;
	__be32 len;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

struct cpl_tx_pkt_lso {
	WR_HDR;
	struct cpl_tx_pkt_lso_core c;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

struct cpl_iscsi_hdr {
	union opcode_tid ot;
	__be16 pdu_len_ddp;
#define ISCSI_PDU_LEN(x) ((x) & 0x7FFF)
#define ISCSI_DDP        (1 << 15)
	__be16 len;
	__be32 seq;
	__be16 urg;
	u8 rsvd;
	u8 status;
};

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
#define RX_CREDITS(x)   ((x) << 0)
#define RX_FORCE_ACK(x) ((x) << 28)
};

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
#define RXF_UDP (1 << 22)
#define RXF_TCP (1 << 23)
#define RXF_IP  (1 << 24)
#define RXF_IP6 (1 << 25)
	__be16 hdr_len;
	__be16 err_vec;
};

/* rx_pkt.l2info fields */
#define S_RX_ETHHDR_LEN    0
#define M_RX_ETHHDR_LEN    0x1F
#define V_RX_ETHHDR_LEN(x) ((x) << S_RX_ETHHDR_LEN)
#define G_RX_ETHHDR_LEN(x) (((x) >> S_RX_ETHHDR_LEN) & M_RX_ETHHDR_LEN)

#define S_RX_T5_ETHHDR_LEN    0
#define M_RX_T5_ETHHDR_LEN    0x3F
#define V_RX_T5_ETHHDR_LEN(x) ((x) << S_RX_T5_ETHHDR_LEN)
#define G_RX_T5_ETHHDR_LEN(x) (((x) >> S_RX_T5_ETHHDR_LEN) & M_RX_T5_ETHHDR_LEN)

#define S_RX_MACIDX    8
#define M_RX_MACIDX    0x1FF
#define V_RX_MACIDX(x) ((x) << S_RX_MACIDX)
#define G_RX_MACIDX(x) (((x) >> S_RX_MACIDX) & M_RX_MACIDX)

#define S_RXF_SYN    21
#define V_RXF_SYN(x) ((x) << S_RXF_SYN)
#define F_RXF_SYN    V_RXF_SYN(1U)

#define S_RX_CHAN    28
#define M_RX_CHAN    0xF
#define V_RX_CHAN(x) ((x) << S_RX_CHAN)
#define G_RX_CHAN(x) (((x) >> S_RX_CHAN) & M_RX_CHAN)

/* rx_pkt.hdr_len fields */
#define S_RX_TCPHDR_LEN    0
#define M_RX_TCPHDR_LEN    0x3F
#define V_RX_TCPHDR_LEN(x) ((x) << S_RX_TCPHDR_LEN)
#define G_RX_TCPHDR_LEN(x) (((x) >> S_RX_TCPHDR_LEN) & M_RX_TCPHDR_LEN)

#define S_RX_IPHDR_LEN    6
#define M_RX_IPHDR_LEN    0x3FF
#define V_RX_IPHDR_LEN(x) ((x) << S_RX_IPHDR_LEN)
#define G_RX_IPHDR_LEN(x) (((x) >> S_RX_IPHDR_LEN) & M_RX_IPHDR_LEN)

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
#define L2T_W_INFO(x)    ((x) << 2)
#define L2T_W_PORT(x)    ((x) << 8)
#define L2T_W_NOREPLY(x) ((x) << 15)
	__be16 l2t_idx;
	__be16 vlan;
	u8 dst_mac[6];
};

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
#define EGR_QID(x) ((x) & 0x1FFFF)
	__be16 cidx;
	__be16 pidx;
};

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

struct ulptx_sge_pair {
	__be32 len[2];
	__be64 addr[2];
};

struct ulptx_sgl {
	__be32 cmd_nsge;
#define ULPTX_CMD(x) ((x) << 24)
#define ULPTX_NSGE(x) ((x) << 0)
#define ULPTX_MORE (1U << 23)
	__be32 len0;
	__be64 addr0;
	struct ulptx_sge_pair sge[0];
};

struct ulp_mem_io {
	WR_HDR;
	__be32 cmd;
#define ULP_MEMIO_ORDER(x) ((x) << 23)
	__be32 len16;             /* command length */
	__be32 dlen;              /* data length in 32-byte units */
#define ULP_MEMIO_DATA_LEN(x) ((x) << 0)
	__be32 lock_addr;
#define ULP_MEMIO_ADDR(x) ((x) << 0)
#define ULP_MEMIO_LOCK(x) ((x) << 31)
};

#define S_T5_ULP_MEMIO_IMM    23
#define V_T5_ULP_MEMIO_IMM(x) ((x) << S_T5_ULP_MEMIO_IMM)
#define F_T5_ULP_MEMIO_IMM    V_T5_ULP_MEMIO_IMM(1U)

#define S_T5_ULP_MEMIO_ORDER    22
#define V_T5_ULP_MEMIO_ORDER(x) ((x) << S_T5_ULP_MEMIO_ORDER)
#define F_T5_ULP_MEMIO_ORDER    V_T5_ULP_MEMIO_ORDER(1U)

#endif  /* __T4_MSG_H */
