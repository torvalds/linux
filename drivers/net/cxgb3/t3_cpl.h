/*
 * Copyright (c) 2004-2007 Chelsio, Inc. All rights reserved.
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
#ifndef T3_CPL_H
#define T3_CPL_H

#if !defined(__LITTLE_ENDIAN_BITFIELD) && !defined(__BIG_ENDIAN_BITFIELD)
# include <asm/byteorder.h>
#endif

enum CPL_opcode {
	CPL_PASS_OPEN_REQ = 0x1,
	CPL_PASS_ACCEPT_RPL = 0x2,
	CPL_ACT_OPEN_REQ = 0x3,
	CPL_SET_TCB = 0x4,
	CPL_SET_TCB_FIELD = 0x5,
	CPL_GET_TCB = 0x6,
	CPL_PCMD = 0x7,
	CPL_CLOSE_CON_REQ = 0x8,
	CPL_CLOSE_LISTSRV_REQ = 0x9,
	CPL_ABORT_REQ = 0xA,
	CPL_ABORT_RPL = 0xB,
	CPL_TX_DATA = 0xC,
	CPL_RX_DATA_ACK = 0xD,
	CPL_TX_PKT = 0xE,
	CPL_RTE_DELETE_REQ = 0xF,
	CPL_RTE_WRITE_REQ = 0x10,
	CPL_RTE_READ_REQ = 0x11,
	CPL_L2T_WRITE_REQ = 0x12,
	CPL_L2T_READ_REQ = 0x13,
	CPL_SMT_WRITE_REQ = 0x14,
	CPL_SMT_READ_REQ = 0x15,
	CPL_TX_PKT_LSO = 0x16,
	CPL_PCMD_READ = 0x17,
	CPL_BARRIER = 0x18,
	CPL_TID_RELEASE = 0x1A,

	CPL_CLOSE_LISTSRV_RPL = 0x20,
	CPL_ERROR = 0x21,
	CPL_GET_TCB_RPL = 0x22,
	CPL_L2T_WRITE_RPL = 0x23,
	CPL_PCMD_READ_RPL = 0x24,
	CPL_PCMD_RPL = 0x25,
	CPL_PEER_CLOSE = 0x26,
	CPL_RTE_DELETE_RPL = 0x27,
	CPL_RTE_WRITE_RPL = 0x28,
	CPL_RX_DDP_COMPLETE = 0x29,
	CPL_RX_PHYS_ADDR = 0x2A,
	CPL_RX_PKT = 0x2B,
	CPL_RX_URG_NOTIFY = 0x2C,
	CPL_SET_TCB_RPL = 0x2D,
	CPL_SMT_WRITE_RPL = 0x2E,
	CPL_TX_DATA_ACK = 0x2F,

	CPL_ABORT_REQ_RSS = 0x30,
	CPL_ABORT_RPL_RSS = 0x31,
	CPL_CLOSE_CON_RPL = 0x32,
	CPL_ISCSI_HDR = 0x33,
	CPL_L2T_READ_RPL = 0x34,
	CPL_RDMA_CQE = 0x35,
	CPL_RDMA_CQE_READ_RSP = 0x36,
	CPL_RDMA_CQE_ERR = 0x37,
	CPL_RTE_READ_RPL = 0x38,
	CPL_RX_DATA = 0x39,

	CPL_ACT_OPEN_RPL = 0x40,
	CPL_PASS_OPEN_RPL = 0x41,
	CPL_RX_DATA_DDP = 0x42,
	CPL_SMT_READ_RPL = 0x43,

	CPL_ACT_ESTABLISH = 0x50,
	CPL_PASS_ESTABLISH = 0x51,

	CPL_PASS_ACCEPT_REQ = 0x70,

	CPL_ASYNC_NOTIF = 0x80,	/* fake opcode for async notifications */

	CPL_TX_DMA_ACK = 0xA0,
	CPL_RDMA_READ_REQ = 0xA1,
	CPL_RDMA_TERMINATE = 0xA2,
	CPL_TRACE_PKT = 0xA3,
	CPL_RDMA_EC_STATUS = 0xA5,

	NUM_CPL_CMDS		/* must be last and previous entries must be sorted */
};

enum CPL_error {
	CPL_ERR_NONE = 0,
	CPL_ERR_TCAM_PARITY = 1,
	CPL_ERR_TCAM_FULL = 3,
	CPL_ERR_CONN_RESET = 20,
	CPL_ERR_CONN_EXIST = 22,
	CPL_ERR_ARP_MISS = 23,
	CPL_ERR_BAD_SYN = 24,
	CPL_ERR_CONN_TIMEDOUT = 30,
	CPL_ERR_XMIT_TIMEDOUT = 31,
	CPL_ERR_PERSIST_TIMEDOUT = 32,
	CPL_ERR_FINWAIT2_TIMEDOUT = 33,
	CPL_ERR_KEEPALIVE_TIMEDOUT = 34,
	CPL_ERR_RTX_NEG_ADVICE = 35,
	CPL_ERR_PERSIST_NEG_ADVICE = 36,
	CPL_ERR_ABORT_FAILED = 42,
	CPL_ERR_GENERAL = 99
};

enum {
	CPL_CONN_POLICY_AUTO = 0,
	CPL_CONN_POLICY_ASK = 1,
	CPL_CONN_POLICY_DENY = 3
};

enum {
	ULP_MODE_NONE = 0,
	ULP_MODE_ISCSI = 2,
	ULP_MODE_RDMA = 4,
	ULP_MODE_TCPDDP = 5
};

enum {
	ULP_CRC_HEADER = 1 << 0,
	ULP_CRC_DATA = 1 << 1
};

enum {
	CPL_PASS_OPEN_ACCEPT,
	CPL_PASS_OPEN_REJECT
};

enum {
	CPL_ABORT_SEND_RST = 0,
	CPL_ABORT_NO_RST,
	CPL_ABORT_POST_CLOSE_REQ = 2
};

enum {				/* TX_PKT_LSO ethernet types */
	CPL_ETH_II,
	CPL_ETH_II_VLAN,
	CPL_ETH_802_3,
	CPL_ETH_802_3_VLAN
};

enum {				/* TCP congestion control algorithms */
	CONG_ALG_RENO,
	CONG_ALG_TAHOE,
	CONG_ALG_NEWRENO,
	CONG_ALG_HIGHSPEED
};

union opcode_tid {
	__be32 opcode_tid;
	__u8 opcode;
};

#define S_OPCODE 24
#define V_OPCODE(x) ((x) << S_OPCODE)
#define G_OPCODE(x) (((x) >> S_OPCODE) & 0xFF)
#define G_TID(x)    ((x) & 0xFFFFFF)

/* tid is assumed to be 24-bits */
#define MK_OPCODE_TID(opcode, tid) (V_OPCODE(opcode) | (tid))

#define OPCODE_TID(cmd) ((cmd)->ot.opcode_tid)

/* extract the TID from a CPL command */
#define GET_TID(cmd) (G_TID(ntohl(OPCODE_TID(cmd))))

struct tcp_options {
	__be16 mss;
	__u8 wsf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	 __u8:5;
	__u8 ecn:1;
	__u8 sack:1;
	__u8 tstamp:1;
#else
	__u8 tstamp:1;
	__u8 sack:1;
	__u8 ecn:1;
	 __u8:5;
#endif
};

struct rss_header {
	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 cpu_idx:6;
	__u8 hash_type:2;
#else
	__u8 hash_type:2;
	__u8 cpu_idx:6;
#endif
	__be16 cq_idx;
	__be32 rss_hash_val;
};

#ifndef CHELSIO_FW
struct work_request_hdr {
	__be32 wr_hi;
	__be32 wr_lo;
};

/* wr_hi fields */
#define S_WR_SGE_CREDITS    0
#define M_WR_SGE_CREDITS    0xFF
#define V_WR_SGE_CREDITS(x) ((x) << S_WR_SGE_CREDITS)
#define G_WR_SGE_CREDITS(x) (((x) >> S_WR_SGE_CREDITS) & M_WR_SGE_CREDITS)

#define S_WR_SGLSFLT    8
#define M_WR_SGLSFLT    0xFF
#define V_WR_SGLSFLT(x) ((x) << S_WR_SGLSFLT)
#define G_WR_SGLSFLT(x) (((x) >> S_WR_SGLSFLT) & M_WR_SGLSFLT)

#define S_WR_BCNTLFLT    16
#define M_WR_BCNTLFLT    0xF
#define V_WR_BCNTLFLT(x) ((x) << S_WR_BCNTLFLT)
#define G_WR_BCNTLFLT(x) (((x) >> S_WR_BCNTLFLT) & M_WR_BCNTLFLT)

#define S_WR_DATATYPE    20
#define V_WR_DATATYPE(x) ((x) << S_WR_DATATYPE)
#define F_WR_DATATYPE    V_WR_DATATYPE(1U)

#define S_WR_COMPL    21
#define V_WR_COMPL(x) ((x) << S_WR_COMPL)
#define F_WR_COMPL    V_WR_COMPL(1U)

#define S_WR_EOP    22
#define V_WR_EOP(x) ((x) << S_WR_EOP)
#define F_WR_EOP    V_WR_EOP(1U)

#define S_WR_SOP    23
#define V_WR_SOP(x) ((x) << S_WR_SOP)
#define F_WR_SOP    V_WR_SOP(1U)

#define S_WR_OP    24
#define M_WR_OP    0xFF
#define V_WR_OP(x) ((x) << S_WR_OP)
#define G_WR_OP(x) (((x) >> S_WR_OP) & M_WR_OP)

/* wr_lo fields */
#define S_WR_LEN    0
#define M_WR_LEN    0xFF
#define V_WR_LEN(x) ((x) << S_WR_LEN)
#define G_WR_LEN(x) (((x) >> S_WR_LEN) & M_WR_LEN)

#define S_WR_TID    8
#define M_WR_TID    0xFFFFF
#define V_WR_TID(x) ((x) << S_WR_TID)
#define G_WR_TID(x) (((x) >> S_WR_TID) & M_WR_TID)

#define S_WR_CR_FLUSH    30
#define V_WR_CR_FLUSH(x) ((x) << S_WR_CR_FLUSH)
#define F_WR_CR_FLUSH    V_WR_CR_FLUSH(1U)

#define S_WR_GEN    31
#define V_WR_GEN(x) ((x) << S_WR_GEN)
#define F_WR_GEN    V_WR_GEN(1U)

# define WR_HDR struct work_request_hdr wr
# define RSS_HDR
#else
# define WR_HDR
# define RSS_HDR struct rss_header rss_hdr;
#endif

/* option 0 lower-half fields */
#define S_CPL_STATUS    0
#define M_CPL_STATUS    0xFF
#define V_CPL_STATUS(x) ((x) << S_CPL_STATUS)
#define G_CPL_STATUS(x) (((x) >> S_CPL_STATUS) & M_CPL_STATUS)

#define S_INJECT_TIMER    6
#define V_INJECT_TIMER(x) ((x) << S_INJECT_TIMER)
#define F_INJECT_TIMER    V_INJECT_TIMER(1U)

#define S_NO_OFFLOAD    7
#define V_NO_OFFLOAD(x) ((x) << S_NO_OFFLOAD)
#define F_NO_OFFLOAD    V_NO_OFFLOAD(1U)

#define S_ULP_MODE    8
#define M_ULP_MODE    0xF
#define V_ULP_MODE(x) ((x) << S_ULP_MODE)
#define G_ULP_MODE(x) (((x) >> S_ULP_MODE) & M_ULP_MODE)

#define S_RCV_BUFSIZ    12
#define M_RCV_BUFSIZ    0x3FFF
#define V_RCV_BUFSIZ(x) ((x) << S_RCV_BUFSIZ)
#define G_RCV_BUFSIZ(x) (((x) >> S_RCV_BUFSIZ) & M_RCV_BUFSIZ)

#define S_TOS    26
#define M_TOS    0x3F
#define V_TOS(x) ((x) << S_TOS)
#define G_TOS(x) (((x) >> S_TOS) & M_TOS)

/* option 0 upper-half fields */
#define S_DELACK    0
#define V_DELACK(x) ((x) << S_DELACK)
#define F_DELACK    V_DELACK(1U)

#define S_NO_CONG    1
#define V_NO_CONG(x) ((x) << S_NO_CONG)
#define F_NO_CONG    V_NO_CONG(1U)

#define S_SRC_MAC_SEL    2
#define M_SRC_MAC_SEL    0x3
#define V_SRC_MAC_SEL(x) ((x) << S_SRC_MAC_SEL)
#define G_SRC_MAC_SEL(x) (((x) >> S_SRC_MAC_SEL) & M_SRC_MAC_SEL)

#define S_L2T_IDX    4
#define M_L2T_IDX    0x7FF
#define V_L2T_IDX(x) ((x) << S_L2T_IDX)
#define G_L2T_IDX(x) (((x) >> S_L2T_IDX) & M_L2T_IDX)

#define S_TX_CHANNEL    15
#define V_TX_CHANNEL(x) ((x) << S_TX_CHANNEL)
#define F_TX_CHANNEL    V_TX_CHANNEL(1U)

#define S_TCAM_BYPASS    16
#define V_TCAM_BYPASS(x) ((x) << S_TCAM_BYPASS)
#define F_TCAM_BYPASS    V_TCAM_BYPASS(1U)

#define S_NAGLE    17
#define V_NAGLE(x) ((x) << S_NAGLE)
#define F_NAGLE    V_NAGLE(1U)

#define S_WND_SCALE    18
#define M_WND_SCALE    0xF
#define V_WND_SCALE(x) ((x) << S_WND_SCALE)
#define G_WND_SCALE(x) (((x) >> S_WND_SCALE) & M_WND_SCALE)

#define S_KEEP_ALIVE    22
#define V_KEEP_ALIVE(x) ((x) << S_KEEP_ALIVE)
#define F_KEEP_ALIVE    V_KEEP_ALIVE(1U)

#define S_MAX_RETRANS    23
#define M_MAX_RETRANS    0xF
#define V_MAX_RETRANS(x) ((x) << S_MAX_RETRANS)
#define G_MAX_RETRANS(x) (((x) >> S_MAX_RETRANS) & M_MAX_RETRANS)

#define S_MAX_RETRANS_OVERRIDE    27
#define V_MAX_RETRANS_OVERRIDE(x) ((x) << S_MAX_RETRANS_OVERRIDE)
#define F_MAX_RETRANS_OVERRIDE    V_MAX_RETRANS_OVERRIDE(1U)

#define S_MSS_IDX    28
#define M_MSS_IDX    0xF
#define V_MSS_IDX(x) ((x) << S_MSS_IDX)
#define G_MSS_IDX(x) (((x) >> S_MSS_IDX) & M_MSS_IDX)

/* option 1 fields */
#define S_RSS_ENABLE    0
#define V_RSS_ENABLE(x) ((x) << S_RSS_ENABLE)
#define F_RSS_ENABLE    V_RSS_ENABLE(1U)

#define S_RSS_MASK_LEN    1
#define M_RSS_MASK_LEN    0x7
#define V_RSS_MASK_LEN(x) ((x) << S_RSS_MASK_LEN)
#define G_RSS_MASK_LEN(x) (((x) >> S_RSS_MASK_LEN) & M_RSS_MASK_LEN)

#define S_CPU_IDX    4
#define M_CPU_IDX    0x3F
#define V_CPU_IDX(x) ((x) << S_CPU_IDX)
#define G_CPU_IDX(x) (((x) >> S_CPU_IDX) & M_CPU_IDX)

#define S_MAC_MATCH_VALID    18
#define V_MAC_MATCH_VALID(x) ((x) << S_MAC_MATCH_VALID)
#define F_MAC_MATCH_VALID    V_MAC_MATCH_VALID(1U)

#define S_CONN_POLICY    19
#define M_CONN_POLICY    0x3
#define V_CONN_POLICY(x) ((x) << S_CONN_POLICY)
#define G_CONN_POLICY(x) (((x) >> S_CONN_POLICY) & M_CONN_POLICY)

#define S_SYN_DEFENSE    21
#define V_SYN_DEFENSE(x) ((x) << S_SYN_DEFENSE)
#define F_SYN_DEFENSE    V_SYN_DEFENSE(1U)

#define S_VLAN_PRI    22
#define M_VLAN_PRI    0x3
#define V_VLAN_PRI(x) ((x) << S_VLAN_PRI)
#define G_VLAN_PRI(x) (((x) >> S_VLAN_PRI) & M_VLAN_PRI)

#define S_VLAN_PRI_VALID    24
#define V_VLAN_PRI_VALID(x) ((x) << S_VLAN_PRI_VALID)
#define F_VLAN_PRI_VALID    V_VLAN_PRI_VALID(1U)

#define S_PKT_TYPE    25
#define M_PKT_TYPE    0x3
#define V_PKT_TYPE(x) ((x) << S_PKT_TYPE)
#define G_PKT_TYPE(x) (((x) >> S_PKT_TYPE) & M_PKT_TYPE)

#define S_MAC_MATCH    27
#define M_MAC_MATCH    0x1F
#define V_MAC_MATCH(x) ((x) << S_MAC_MATCH)
#define G_MAC_MATCH(x) (((x) >> S_MAC_MATCH) & M_MAC_MATCH)

/* option 2 fields */
#define S_CPU_INDEX    0
#define M_CPU_INDEX    0x7F
#define V_CPU_INDEX(x) ((x) << S_CPU_INDEX)
#define G_CPU_INDEX(x) (((x) >> S_CPU_INDEX) & M_CPU_INDEX)

#define S_CPU_INDEX_VALID    7
#define V_CPU_INDEX_VALID(x) ((x) << S_CPU_INDEX_VALID)
#define F_CPU_INDEX_VALID    V_CPU_INDEX_VALID(1U)

#define S_RX_COALESCE    8
#define M_RX_COALESCE    0x3
#define V_RX_COALESCE(x) ((x) << S_RX_COALESCE)
#define G_RX_COALESCE(x) (((x) >> S_RX_COALESCE) & M_RX_COALESCE)

#define S_RX_COALESCE_VALID    10
#define V_RX_COALESCE_VALID(x) ((x) << S_RX_COALESCE_VALID)
#define F_RX_COALESCE_VALID    V_RX_COALESCE_VALID(1U)

#define S_CONG_CONTROL_FLAVOR    11
#define M_CONG_CONTROL_FLAVOR    0x3
#define V_CONG_CONTROL_FLAVOR(x) ((x) << S_CONG_CONTROL_FLAVOR)
#define G_CONG_CONTROL_FLAVOR(x) (((x) >> S_CONG_CONTROL_FLAVOR) & M_CONG_CONTROL_FLAVOR)

#define S_PACING_FLAVOR    13
#define M_PACING_FLAVOR    0x3
#define V_PACING_FLAVOR(x) ((x) << S_PACING_FLAVOR)
#define G_PACING_FLAVOR(x) (((x) >> S_PACING_FLAVOR) & M_PACING_FLAVOR)

#define S_FLAVORS_VALID    15
#define V_FLAVORS_VALID(x) ((x) << S_FLAVORS_VALID)
#define F_FLAVORS_VALID    V_FLAVORS_VALID(1U)

#define S_RX_FC_DISABLE    16
#define V_RX_FC_DISABLE(x) ((x) << S_RX_FC_DISABLE)
#define F_RX_FC_DISABLE    V_RX_FC_DISABLE(1U)

#define S_RX_FC_VALID    17
#define V_RX_FC_VALID(x) ((x) << S_RX_FC_VALID)
#define F_RX_FC_VALID    V_RX_FC_VALID(1U)

struct cpl_pass_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be32 opt0h;
	__be32 opt0l;
	__be32 peer_netmask;
	__be32 opt1;
};

struct cpl_pass_open_rpl {
	RSS_HDR union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__u8 resvd[7];
	__u8 status;
};

struct cpl_pass_establish {
	RSS_HDR union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be32 tos_tid;
	__be16 l2t_idx;
	__be16 tcp_opt;
	__be32 snd_isn;
	__be32 rcv_isn;
};

/* cpl_pass_establish.tos_tid fields */
#define S_PASS_OPEN_TID    0
#define M_PASS_OPEN_TID    0xFFFFFF
#define V_PASS_OPEN_TID(x) ((x) << S_PASS_OPEN_TID)
#define G_PASS_OPEN_TID(x) (((x) >> S_PASS_OPEN_TID) & M_PASS_OPEN_TID)

#define S_PASS_OPEN_TOS    24
#define M_PASS_OPEN_TOS    0xFF
#define V_PASS_OPEN_TOS(x) ((x) << S_PASS_OPEN_TOS)
#define G_PASS_OPEN_TOS(x) (((x) >> S_PASS_OPEN_TOS) & M_PASS_OPEN_TOS)

/* cpl_pass_establish.l2t_idx fields */
#define S_L2T_IDX16    5
#define M_L2T_IDX16    0x7FF
#define V_L2T_IDX16(x) ((x) << S_L2T_IDX16)
#define G_L2T_IDX16(x) (((x) >> S_L2T_IDX16) & M_L2T_IDX16)

/* cpl_pass_establish.tcp_opt fields (also applies act_open_establish) */
#define G_TCPOPT_WSCALE_OK(x)  (((x) >> 5) & 1)
#define G_TCPOPT_SACK(x)       (((x) >> 6) & 1)
#define G_TCPOPT_TSTAMP(x)     (((x) >> 7) & 1)
#define G_TCPOPT_SND_WSCALE(x) (((x) >> 8) & 0xf)
#define G_TCPOPT_MSS(x)        (((x) >> 12) & 0xf)

struct cpl_pass_accept_req {
	RSS_HDR union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be32 tos_tid;
	struct tcp_options tcp_options;
	__u8 dst_mac[6];
	__be16 vlan_tag;
	__u8 src_mac[6];
#if defined(__LITTLE_ENDIAN_BITFIELD)
	 __u8:3;
	__u8 addr_idx:3;
	__u8 port_idx:1;
	__u8 exact_match:1;
#else
	__u8 exact_match:1;
	__u8 port_idx:1;
	__u8 addr_idx:3;
	 __u8:3;
#endif
	__u8 rsvd;
	__be32 rcv_isn;
	__be32 rsvd2;
};

struct cpl_pass_accept_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 opt2;
	__be32 rsvd;
	__be32 peer_ip;
	__be32 opt0h;
	__be32 opt0l_status;
};

struct cpl_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be32 opt0h;
	__be32 opt0l;
	__be32 params;
	__be32 opt2;
};

/* cpl_act_open_req.params fields */
#define S_AOPEN_VLAN_PRI    9
#define M_AOPEN_VLAN_PRI    0x3
#define V_AOPEN_VLAN_PRI(x) ((x) << S_AOPEN_VLAN_PRI)
#define G_AOPEN_VLAN_PRI(x) (((x) >> S_AOPEN_VLAN_PRI) & M_AOPEN_VLAN_PRI)

#define S_AOPEN_VLAN_PRI_VALID    11
#define V_AOPEN_VLAN_PRI_VALID(x) ((x) << S_AOPEN_VLAN_PRI_VALID)
#define F_AOPEN_VLAN_PRI_VALID    V_AOPEN_VLAN_PRI_VALID(1U)

#define S_AOPEN_PKT_TYPE    12
#define M_AOPEN_PKT_TYPE    0x3
#define V_AOPEN_PKT_TYPE(x) ((x) << S_AOPEN_PKT_TYPE)
#define G_AOPEN_PKT_TYPE(x) (((x) >> S_AOPEN_PKT_TYPE) & M_AOPEN_PKT_TYPE)

#define S_AOPEN_MAC_MATCH    14
#define M_AOPEN_MAC_MATCH    0x1F
#define V_AOPEN_MAC_MATCH(x) ((x) << S_AOPEN_MAC_MATCH)
#define G_AOPEN_MAC_MATCH(x) (((x) >> S_AOPEN_MAC_MATCH) & M_AOPEN_MAC_MATCH)

#define S_AOPEN_MAC_MATCH_VALID    19
#define V_AOPEN_MAC_MATCH_VALID(x) ((x) << S_AOPEN_MAC_MATCH_VALID)
#define F_AOPEN_MAC_MATCH_VALID    V_AOPEN_MAC_MATCH_VALID(1U)

#define S_AOPEN_IFF_VLAN    20
#define M_AOPEN_IFF_VLAN    0xFFF
#define V_AOPEN_IFF_VLAN(x) ((x) << S_AOPEN_IFF_VLAN)
#define G_AOPEN_IFF_VLAN(x) (((x) >> S_AOPEN_IFF_VLAN) & M_AOPEN_IFF_VLAN)

struct cpl_act_open_rpl {
	RSS_HDR union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be32 atid;
	__u8 rsvd[3];
	__u8 status;
};

struct cpl_act_establish {
	RSS_HDR union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be32 tos_tid;
	__be16 l2t_idx;
	__be16 tcp_opt;
	__be32 snd_isn;
	__be32 rcv_isn;
};

struct cpl_get_tcb {
	WR_HDR;
	union opcode_tid ot;
	__be16 cpuno;
	__be16 rsvd;
};

struct cpl_get_tcb_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 rsvd;
	__u8 status;
	__be16 len;
};

struct cpl_set_tcb {
	WR_HDR;
	union opcode_tid ot;
	__u8 reply;
	__u8 cpu_idx;
	__be16 len;
};

/* cpl_set_tcb.reply fields */
#define S_NO_REPLY    7
#define V_NO_REPLY(x) ((x) << S_NO_REPLY)
#define F_NO_REPLY    V_NO_REPLY(1U)

struct cpl_set_tcb_field {
	WR_HDR;
	union opcode_tid ot;
	__u8 reply;
	__u8 cpu_idx;
	__be16 word;
	__be64 mask;
	__be64 val;
};

struct cpl_set_tcb_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 rsvd[3];
	__u8 status;
};

struct cpl_pcmd {
	WR_HDR;
	union opcode_tid ot;
	__u8 rsvd[3];
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 src:1;
	__u8 bundle:1;
	__u8 channel:1;
	 __u8:5;
#else
	 __u8:5;
	__u8 channel:1;
	__u8 bundle:1;
	__u8 src:1;
#endif
	__be32 pcmd_parm[2];
};

struct cpl_pcmd_reply {
	RSS_HDR union opcode_tid ot;
	__u8 status;
	__u8 rsvd;
	__be16 len;
};

struct cpl_close_con_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd;
};

struct cpl_close_con_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 rsvd[3];
	__u8 status;
	__be32 snd_nxt;
	__be32 rcv_nxt;
};

struct cpl_close_listserv_req {
	WR_HDR;
	union opcode_tid ot;
	__u8 rsvd0;
	__u8 cpu_idx;
	__be16 rsvd1;
};

struct cpl_close_listserv_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 rsvd[3];
	__u8 status;
};

struct cpl_abort_req_rss {
	RSS_HDR union opcode_tid ot;
	__be32 rsvd0;
	__u8 rsvd1;
	__u8 status;
	__u8 rsvd2[6];
};

struct cpl_abort_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd0;
	__u8 rsvd1;
	__u8 cmd;
	__u8 rsvd2[6];
};

struct cpl_abort_rpl_rss {
	RSS_HDR union opcode_tid ot;
	__be32 rsvd0;
	__u8 rsvd1;
	__u8 status;
	__u8 rsvd2[6];
};

struct cpl_abort_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd0;
	__u8 rsvd1;
	__u8 cmd;
	__u8 rsvd2[6];
};

struct cpl_peer_close {
	RSS_HDR union opcode_tid ot;
	__be32 rcv_nxt;
};

struct tx_data_wr {
	__be32 wr_hi;
	__be32 wr_lo;
	__be32 len;
	__be32 flags;
	__be32 sndseq;
	__be32 param;
};

/* tx_data_wr.param fields */
#define S_TX_PORT    0
#define M_TX_PORT    0x7
#define V_TX_PORT(x) ((x) << S_TX_PORT)
#define G_TX_PORT(x) (((x) >> S_TX_PORT) & M_TX_PORT)

#define S_TX_MSS    4
#define M_TX_MSS    0xF
#define V_TX_MSS(x) ((x) << S_TX_MSS)
#define G_TX_MSS(x) (((x) >> S_TX_MSS) & M_TX_MSS)

#define S_TX_QOS    8
#define M_TX_QOS    0xFF
#define V_TX_QOS(x) ((x) << S_TX_QOS)
#define G_TX_QOS(x) (((x) >> S_TX_QOS) & M_TX_QOS)

#define S_TX_SNDBUF 16
#define M_TX_SNDBUF 0xFFFF
#define V_TX_SNDBUF(x) ((x) << S_TX_SNDBUF)
#define G_TX_SNDBUF(x) (((x) >> S_TX_SNDBUF) & M_TX_SNDBUF)

struct cpl_tx_data {
	union opcode_tid ot;
	__be32 len;
	__be32 rsvd;
	__be16 urg;
	__be16 flags;
};

/* cpl_tx_data.flags fields */
#define S_TX_ULP_SUBMODE    6
#define M_TX_ULP_SUBMODE    0xF
#define V_TX_ULP_SUBMODE(x) ((x) << S_TX_ULP_SUBMODE)
#define G_TX_ULP_SUBMODE(x) (((x) >> S_TX_ULP_SUBMODE) & M_TX_ULP_SUBMODE)

#define S_TX_ULP_MODE    10
#define M_TX_ULP_MODE    0xF
#define V_TX_ULP_MODE(x) ((x) << S_TX_ULP_MODE)
#define G_TX_ULP_MODE(x) (((x) >> S_TX_ULP_MODE) & M_TX_ULP_MODE)

#define S_TX_SHOVE    14
#define V_TX_SHOVE(x) ((x) << S_TX_SHOVE)
#define F_TX_SHOVE    V_TX_SHOVE(1U)

#define S_TX_MORE    15
#define V_TX_MORE(x) ((x) << S_TX_MORE)
#define F_TX_MORE    V_TX_MORE(1U)

/* additional tx_data_wr.flags fields */
#define S_TX_CPU_IDX    0
#define M_TX_CPU_IDX    0x3F
#define V_TX_CPU_IDX(x) ((x) << S_TX_CPU_IDX)
#define G_TX_CPU_IDX(x) (((x) >> S_TX_CPU_IDX) & M_TX_CPU_IDX)

#define S_TX_URG    16
#define V_TX_URG(x) ((x) << S_TX_URG)
#define F_TX_URG    V_TX_URG(1U)

#define S_TX_CLOSE    17
#define V_TX_CLOSE(x) ((x) << S_TX_CLOSE)
#define F_TX_CLOSE    V_TX_CLOSE(1U)

#define S_TX_INIT    18
#define V_TX_INIT(x) ((x) << S_TX_INIT)
#define F_TX_INIT    V_TX_INIT(1U)

#define S_TX_IMM_ACK    19
#define V_TX_IMM_ACK(x) ((x) << S_TX_IMM_ACK)
#define F_TX_IMM_ACK    V_TX_IMM_ACK(1U)

#define S_TX_IMM_DMA    20
#define V_TX_IMM_DMA(x) ((x) << S_TX_IMM_DMA)
#define F_TX_IMM_DMA    V_TX_IMM_DMA(1U)

struct cpl_tx_data_ack {
	RSS_HDR union opcode_tid ot;
	__be32 ack_seq;
};

struct cpl_wr_ack {
	RSS_HDR union opcode_tid ot;
	__be16 credits;
	__be16 rsvd;
	__be32 snd_nxt;
	__be32 snd_una;
};

struct cpl_rdma_ec_status {
	RSS_HDR union opcode_tid ot;
	__u8 rsvd[3];
	__u8 status;
};

struct mngt_pktsched_wr {
	__be32 wr_hi;
	__be32 wr_lo;
	__u8 mngt_opcode;
	__u8 rsvd[7];
	__u8 sched;
	__u8 idx;
	__u8 min;
	__u8 max;
	__u8 binding;
	__u8 rsvd1[3];
};

struct cpl_iscsi_hdr {
	RSS_HDR union opcode_tid ot;
	__be16 pdu_len_ddp;
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd;
	__u8 status;
};

/* cpl_iscsi_hdr.pdu_len_ddp fields */
#define S_ISCSI_PDU_LEN    0
#define M_ISCSI_PDU_LEN    0x7FFF
#define V_ISCSI_PDU_LEN(x) ((x) << S_ISCSI_PDU_LEN)
#define G_ISCSI_PDU_LEN(x) (((x) >> S_ISCSI_PDU_LEN) & M_ISCSI_PDU_LEN)

#define S_ISCSI_DDP    15
#define V_ISCSI_DDP(x) ((x) << S_ISCSI_DDP)
#define F_ISCSI_DDP    V_ISCSI_DDP(1U)

struct cpl_rx_data {
	RSS_HDR union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 seq;
	__be16 urg;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 dack_mode:2;
	__u8 psh:1;
	__u8 heartbeat:1;
	 __u8:4;
#else
	 __u8:4;
	__u8 heartbeat:1;
	__u8 psh:1;
	__u8 dack_mode:2;
#endif
	__u8 status;
};

struct cpl_rx_data_ack {
	WR_HDR;
	union opcode_tid ot;
	__be32 credit_dack;
};

/* cpl_rx_data_ack.ack_seq fields */
#define S_RX_CREDITS    0
#define M_RX_CREDITS    0x7FFFFFF
#define V_RX_CREDITS(x) ((x) << S_RX_CREDITS)
#define G_RX_CREDITS(x) (((x) >> S_RX_CREDITS) & M_RX_CREDITS)

#define S_RX_MODULATE    27
#define V_RX_MODULATE(x) ((x) << S_RX_MODULATE)
#define F_RX_MODULATE    V_RX_MODULATE(1U)

#define S_RX_FORCE_ACK    28
#define V_RX_FORCE_ACK(x) ((x) << S_RX_FORCE_ACK)
#define F_RX_FORCE_ACK    V_RX_FORCE_ACK(1U)

#define S_RX_DACK_MODE    29
#define M_RX_DACK_MODE    0x3
#define V_RX_DACK_MODE(x) ((x) << S_RX_DACK_MODE)
#define G_RX_DACK_MODE(x) (((x) >> S_RX_DACK_MODE) & M_RX_DACK_MODE)

#define S_RX_DACK_CHANGE    31
#define V_RX_DACK_CHANGE(x) ((x) << S_RX_DACK_CHANGE)
#define F_RX_DACK_CHANGE    V_RX_DACK_CHANGE(1U)

struct cpl_rx_urg_notify {
	RSS_HDR union opcode_tid ot;
	__be32 seq;
};

struct cpl_rx_ddp_complete {
	RSS_HDR union opcode_tid ot;
	__be32 ddp_report;
};

struct cpl_rx_data_ddp {
	RSS_HDR union opcode_tid ot;
	__be16 urg;
	__be16 len;
	__be32 seq;
	union {
		__be32 nxt_seq;
		__be32 ddp_report;
	};
	__be32 ulp_crc;
	__be32 ddpvld_status;
};

/* cpl_rx_data_ddp.ddpvld_status fields */
#define S_DDP_STATUS    0
#define M_DDP_STATUS    0xFF
#define V_DDP_STATUS(x) ((x) << S_DDP_STATUS)
#define G_DDP_STATUS(x) (((x) >> S_DDP_STATUS) & M_DDP_STATUS)

#define S_DDP_VALID    15
#define M_DDP_VALID    0x1FFFF
#define V_DDP_VALID(x) ((x) << S_DDP_VALID)
#define G_DDP_VALID(x) (((x) >> S_DDP_VALID) & M_DDP_VALID)

#define S_DDP_PPOD_MISMATCH    15
#define V_DDP_PPOD_MISMATCH(x) ((x) << S_DDP_PPOD_MISMATCH)
#define F_DDP_PPOD_MISMATCH    V_DDP_PPOD_MISMATCH(1U)

#define S_DDP_PDU    16
#define V_DDP_PDU(x) ((x) << S_DDP_PDU)
#define F_DDP_PDU    V_DDP_PDU(1U)

#define S_DDP_LLIMIT_ERR    17
#define V_DDP_LLIMIT_ERR(x) ((x) << S_DDP_LLIMIT_ERR)
#define F_DDP_LLIMIT_ERR    V_DDP_LLIMIT_ERR(1U)

#define S_DDP_PPOD_PARITY_ERR    18
#define V_DDP_PPOD_PARITY_ERR(x) ((x) << S_DDP_PPOD_PARITY_ERR)
#define F_DDP_PPOD_PARITY_ERR    V_DDP_PPOD_PARITY_ERR(1U)

#define S_DDP_PADDING_ERR    19
#define V_DDP_PADDING_ERR(x) ((x) << S_DDP_PADDING_ERR)
#define F_DDP_PADDING_ERR    V_DDP_PADDING_ERR(1U)

#define S_DDP_HDRCRC_ERR    20
#define V_DDP_HDRCRC_ERR(x) ((x) << S_DDP_HDRCRC_ERR)
#define F_DDP_HDRCRC_ERR    V_DDP_HDRCRC_ERR(1U)

#define S_DDP_DATACRC_ERR    21
#define V_DDP_DATACRC_ERR(x) ((x) << S_DDP_DATACRC_ERR)
#define F_DDP_DATACRC_ERR    V_DDP_DATACRC_ERR(1U)

#define S_DDP_INVALID_TAG    22
#define V_DDP_INVALID_TAG(x) ((x) << S_DDP_INVALID_TAG)
#define F_DDP_INVALID_TAG    V_DDP_INVALID_TAG(1U)

#define S_DDP_ULIMIT_ERR    23
#define V_DDP_ULIMIT_ERR(x) ((x) << S_DDP_ULIMIT_ERR)
#define F_DDP_ULIMIT_ERR    V_DDP_ULIMIT_ERR(1U)

#define S_DDP_OFFSET_ERR    24
#define V_DDP_OFFSET_ERR(x) ((x) << S_DDP_OFFSET_ERR)
#define F_DDP_OFFSET_ERR    V_DDP_OFFSET_ERR(1U)

#define S_DDP_COLOR_ERR    25
#define V_DDP_COLOR_ERR(x) ((x) << S_DDP_COLOR_ERR)
#define F_DDP_COLOR_ERR    V_DDP_COLOR_ERR(1U)

#define S_DDP_TID_MISMATCH    26
#define V_DDP_TID_MISMATCH(x) ((x) << S_DDP_TID_MISMATCH)
#define F_DDP_TID_MISMATCH    V_DDP_TID_MISMATCH(1U)

#define S_DDP_INVALID_PPOD    27
#define V_DDP_INVALID_PPOD(x) ((x) << S_DDP_INVALID_PPOD)
#define F_DDP_INVALID_PPOD    V_DDP_INVALID_PPOD(1U)

#define S_DDP_ULP_MODE    28
#define M_DDP_ULP_MODE    0xF
#define V_DDP_ULP_MODE(x) ((x) << S_DDP_ULP_MODE)
#define G_DDP_ULP_MODE(x) (((x) >> S_DDP_ULP_MODE) & M_DDP_ULP_MODE)

/* cpl_rx_data_ddp.ddp_report fields */
#define S_DDP_OFFSET    0
#define M_DDP_OFFSET    0x3FFFFF
#define V_DDP_OFFSET(x) ((x) << S_DDP_OFFSET)
#define G_DDP_OFFSET(x) (((x) >> S_DDP_OFFSET) & M_DDP_OFFSET)

#define S_DDP_URG    24
#define V_DDP_URG(x) ((x) << S_DDP_URG)
#define F_DDP_URG    V_DDP_URG(1U)

#define S_DDP_PSH    25
#define V_DDP_PSH(x) ((x) << S_DDP_PSH)
#define F_DDP_PSH    V_DDP_PSH(1U)

#define S_DDP_BUF_COMPLETE    26
#define V_DDP_BUF_COMPLETE(x) ((x) << S_DDP_BUF_COMPLETE)
#define F_DDP_BUF_COMPLETE    V_DDP_BUF_COMPLETE(1U)

#define S_DDP_BUF_TIMED_OUT    27
#define V_DDP_BUF_TIMED_OUT(x) ((x) << S_DDP_BUF_TIMED_OUT)
#define F_DDP_BUF_TIMED_OUT    V_DDP_BUF_TIMED_OUT(1U)

#define S_DDP_BUF_IDX    28
#define V_DDP_BUF_IDX(x) ((x) << S_DDP_BUF_IDX)
#define F_DDP_BUF_IDX    V_DDP_BUF_IDX(1U)

struct cpl_tx_pkt {
	WR_HDR;
	__be32 cntrl;
	__be32 len;
};

struct cpl_tx_pkt_lso {
	WR_HDR;
	__be32 cntrl;
	__be32 len;

	__be32 rsvd;
	__be32 lso_info;
};

/* cpl_tx_pkt*.cntrl fields */
#define S_TXPKT_VLAN    0
#define M_TXPKT_VLAN    0xFFFF
#define V_TXPKT_VLAN(x) ((x) << S_TXPKT_VLAN)
#define G_TXPKT_VLAN(x) (((x) >> S_TXPKT_VLAN) & M_TXPKT_VLAN)

#define S_TXPKT_INTF    16
#define M_TXPKT_INTF    0xF
#define V_TXPKT_INTF(x) ((x) << S_TXPKT_INTF)
#define G_TXPKT_INTF(x) (((x) >> S_TXPKT_INTF) & M_TXPKT_INTF)

#define S_TXPKT_IPCSUM_DIS    20
#define V_TXPKT_IPCSUM_DIS(x) ((x) << S_TXPKT_IPCSUM_DIS)
#define F_TXPKT_IPCSUM_DIS    V_TXPKT_IPCSUM_DIS(1U)

#define S_TXPKT_L4CSUM_DIS    21
#define V_TXPKT_L4CSUM_DIS(x) ((x) << S_TXPKT_L4CSUM_DIS)
#define F_TXPKT_L4CSUM_DIS    V_TXPKT_L4CSUM_DIS(1U)

#define S_TXPKT_VLAN_VLD    22
#define V_TXPKT_VLAN_VLD(x) ((x) << S_TXPKT_VLAN_VLD)
#define F_TXPKT_VLAN_VLD    V_TXPKT_VLAN_VLD(1U)

#define S_TXPKT_LOOPBACK    23
#define V_TXPKT_LOOPBACK(x) ((x) << S_TXPKT_LOOPBACK)
#define F_TXPKT_LOOPBACK    V_TXPKT_LOOPBACK(1U)

#define S_TXPKT_OPCODE    24
#define M_TXPKT_OPCODE    0xFF
#define V_TXPKT_OPCODE(x) ((x) << S_TXPKT_OPCODE)
#define G_TXPKT_OPCODE(x) (((x) >> S_TXPKT_OPCODE) & M_TXPKT_OPCODE)

/* cpl_tx_pkt_lso.lso_info fields */
#define S_LSO_MSS    0
#define M_LSO_MSS    0x3FFF
#define V_LSO_MSS(x) ((x) << S_LSO_MSS)
#define G_LSO_MSS(x) (((x) >> S_LSO_MSS) & M_LSO_MSS)

#define S_LSO_ETH_TYPE    14
#define M_LSO_ETH_TYPE    0x3
#define V_LSO_ETH_TYPE(x) ((x) << S_LSO_ETH_TYPE)
#define G_LSO_ETH_TYPE(x) (((x) >> S_LSO_ETH_TYPE) & M_LSO_ETH_TYPE)

#define S_LSO_TCPHDR_WORDS    16
#define M_LSO_TCPHDR_WORDS    0xF
#define V_LSO_TCPHDR_WORDS(x) ((x) << S_LSO_TCPHDR_WORDS)
#define G_LSO_TCPHDR_WORDS(x) (((x) >> S_LSO_TCPHDR_WORDS) & M_LSO_TCPHDR_WORDS)

#define S_LSO_IPHDR_WORDS    20
#define M_LSO_IPHDR_WORDS    0xF
#define V_LSO_IPHDR_WORDS(x) ((x) << S_LSO_IPHDR_WORDS)
#define G_LSO_IPHDR_WORDS(x) (((x) >> S_LSO_IPHDR_WORDS) & M_LSO_IPHDR_WORDS)

#define S_LSO_IPV6    24
#define V_LSO_IPV6(x) ((x) << S_LSO_IPV6)
#define F_LSO_IPV6    V_LSO_IPV6(1U)

struct cpl_trace_pkt {
#ifdef CHELSIO_FW
	__u8 rss_opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 err:1;
	 __u8:7;
#else
	 __u8:7;
	__u8 err:1;
#endif
	__u8 rsvd0;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 qid:4;
	 __u8:4;
#else
	 __u8:4;
	__u8 qid:4;
#endif
	__be32 tstamp;
#endif				/* CHELSIO_FW */

	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 iff:4;
	 __u8:4;
#else
	 __u8:4;
	__u8 iff:4;
#endif
	__u8 rsvd[4];
	__be16 len;
};

struct cpl_rx_pkt {
	RSS_HDR __u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 iff:4;
	__u8 csum_valid:1;
	__u8 ipmi_pkt:1;
	__u8 vlan_valid:1;
	__u8 fragment:1;
#else
	__u8 fragment:1;
	__u8 vlan_valid:1;
	__u8 ipmi_pkt:1;
	__u8 csum_valid:1;
	__u8 iff:4;
#endif
	__be16 csum;
	__be16 vlan;
	__be16 len;
};

struct cpl_l2t_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
	__u8 rsvd[2];
	__u8 dst_mac[6];
};

/* cpl_l2t_write_req.params fields */
#define S_L2T_W_IDX    0
#define M_L2T_W_IDX    0x7FF
#define V_L2T_W_IDX(x) ((x) << S_L2T_W_IDX)
#define G_L2T_W_IDX(x) (((x) >> S_L2T_W_IDX) & M_L2T_W_IDX)

#define S_L2T_W_VLAN    11
#define M_L2T_W_VLAN    0xFFF
#define V_L2T_W_VLAN(x) ((x) << S_L2T_W_VLAN)
#define G_L2T_W_VLAN(x) (((x) >> S_L2T_W_VLAN) & M_L2T_W_VLAN)

#define S_L2T_W_IFF    23
#define M_L2T_W_IFF    0xF
#define V_L2T_W_IFF(x) ((x) << S_L2T_W_IFF)
#define G_L2T_W_IFF(x) (((x) >> S_L2T_W_IFF) & M_L2T_W_IFF)

#define S_L2T_W_PRIO    27
#define M_L2T_W_PRIO    0x7
#define V_L2T_W_PRIO(x) ((x) << S_L2T_W_PRIO)
#define G_L2T_W_PRIO(x) (((x) >> S_L2T_W_PRIO) & M_L2T_W_PRIO)

struct cpl_l2t_write_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_l2t_read_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 rsvd;
	__be16 l2t_idx;
};

struct cpl_l2t_read_rpl {
	RSS_HDR union opcode_tid ot;
	__be32 params;
	__u8 rsvd[2];
	__u8 dst_mac[6];
};

/* cpl_l2t_read_rpl.params fields */
#define S_L2T_R_PRIO    0
#define M_L2T_R_PRIO    0x7
#define V_L2T_R_PRIO(x) ((x) << S_L2T_R_PRIO)
#define G_L2T_R_PRIO(x) (((x) >> S_L2T_R_PRIO) & M_L2T_R_PRIO)

#define S_L2T_R_VLAN    8
#define M_L2T_R_VLAN    0xFFF
#define V_L2T_R_VLAN(x) ((x) << S_L2T_R_VLAN)
#define G_L2T_R_VLAN(x) (((x) >> S_L2T_R_VLAN) & M_L2T_R_VLAN)

#define S_L2T_R_IFF    20
#define M_L2T_R_IFF    0xF
#define V_L2T_R_IFF(x) ((x) << S_L2T_R_IFF)
#define G_L2T_R_IFF(x) (((x) >> S_L2T_R_IFF) & M_L2T_R_IFF)

#define S_L2T_STATUS    24
#define M_L2T_STATUS    0xFF
#define V_L2T_STATUS(x) ((x) << S_L2T_STATUS)
#define G_L2T_STATUS(x) (((x) >> S_L2T_STATUS) & M_L2T_STATUS)

struct cpl_smt_write_req {
	WR_HDR;
	union opcode_tid ot;
	__u8 rsvd0;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 mtu_idx:4;
	__u8 iff:4;
#else
	__u8 iff:4;
	__u8 mtu_idx:4;
#endif
	__be16 rsvd2;
	__be16 rsvd3;
	__u8 src_mac1[6];
	__be16 rsvd4;
	__u8 src_mac0[6];
};

struct cpl_smt_write_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_smt_read_req {
	WR_HDR;
	union opcode_tid ot;
	__u8 rsvd0;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	 __u8:4;
	__u8 iff:4;
#else
	__u8 iff:4;
	 __u8:4;
#endif
	__be16 rsvd2;
};

struct cpl_smt_read_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 status;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 mtu_idx:4;
	 __u8:4;
#else
	 __u8:4;
	__u8 mtu_idx:4;
#endif
	__be16 rsvd2;
	__be16 rsvd3;
	__u8 src_mac1[6];
	__be16 rsvd4;
	__u8 src_mac0[6];
};

struct cpl_rte_delete_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
};

/* { cpl_rte_delete_req, cpl_rte_read_req }.params fields */
#define S_RTE_REQ_LUT_IX    8
#define M_RTE_REQ_LUT_IX    0x7FF
#define V_RTE_REQ_LUT_IX(x) ((x) << S_RTE_REQ_LUT_IX)
#define G_RTE_REQ_LUT_IX(x) (((x) >> S_RTE_REQ_LUT_IX) & M_RTE_REQ_LUT_IX)

#define S_RTE_REQ_LUT_BASE    19
#define M_RTE_REQ_LUT_BASE    0x7FF
#define V_RTE_REQ_LUT_BASE(x) ((x) << S_RTE_REQ_LUT_BASE)
#define G_RTE_REQ_LUT_BASE(x) (((x) >> S_RTE_REQ_LUT_BASE) & M_RTE_REQ_LUT_BASE)

#define S_RTE_READ_REQ_SELECT    31
#define V_RTE_READ_REQ_SELECT(x) ((x) << S_RTE_READ_REQ_SELECT)
#define F_RTE_READ_REQ_SELECT    V_RTE_READ_REQ_SELECT(1U)

struct cpl_rte_delete_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_rte_write_req {
	WR_HDR;
	union opcode_tid ot;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	 __u8:6;
	__u8 write_tcam:1;
	__u8 write_l2t_lut:1;
#else
	__u8 write_l2t_lut:1;
	__u8 write_tcam:1;
	 __u8:6;
#endif
	__u8 rsvd[3];
	__be32 lut_params;
	__be16 rsvd2;
	__be16 l2t_idx;
	__be32 netmask;
	__be32 faddr;
};

/* cpl_rte_write_req.lut_params fields */
#define S_RTE_WRITE_REQ_LUT_IX    10
#define M_RTE_WRITE_REQ_LUT_IX    0x7FF
#define V_RTE_WRITE_REQ_LUT_IX(x) ((x) << S_RTE_WRITE_REQ_LUT_IX)
#define G_RTE_WRITE_REQ_LUT_IX(x) (((x) >> S_RTE_WRITE_REQ_LUT_IX) & M_RTE_WRITE_REQ_LUT_IX)

#define S_RTE_WRITE_REQ_LUT_BASE    21
#define M_RTE_WRITE_REQ_LUT_BASE    0x7FF
#define V_RTE_WRITE_REQ_LUT_BASE(x) ((x) << S_RTE_WRITE_REQ_LUT_BASE)
#define G_RTE_WRITE_REQ_LUT_BASE(x) (((x) >> S_RTE_WRITE_REQ_LUT_BASE) & M_RTE_WRITE_REQ_LUT_BASE)

struct cpl_rte_write_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_rte_read_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
};

struct cpl_rte_read_rpl {
	RSS_HDR union opcode_tid ot;
	__u8 status;
	__u8 rsvd0;
	__be16 l2t_idx;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	 __u8:7;
	__u8 select:1;
#else
	__u8 select:1;
	 __u8:7;
#endif
	__u8 rsvd2[3];
	__be32 addr;
};

struct cpl_tid_release {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd;
};

struct cpl_barrier {
	WR_HDR;
	__u8 opcode;
	__u8 rsvd[7];
};

struct cpl_rdma_read_req {
	__u8 opcode;
	__u8 rsvd[15];
};

struct cpl_rdma_terminate {
#ifdef CHELSIO_FW
	__u8 opcode;
	__u8 rsvd[2];
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 rspq:3;
	 __u8:5;
#else
	 __u8:5;
	__u8 rspq:3;
#endif
	__be32 tid_len;
#endif
	__be32 msn;
	__be32 mo;
	__u8 data[0];
};

/* cpl_rdma_terminate.tid_len fields */
#define S_FLIT_CNT    0
#define M_FLIT_CNT    0xFF
#define V_FLIT_CNT(x) ((x) << S_FLIT_CNT)
#define G_FLIT_CNT(x) (((x) >> S_FLIT_CNT) & M_FLIT_CNT)

#define S_TERM_TID    8
#define M_TERM_TID    0xFFFFF
#define V_TERM_TID(x) ((x) << S_TERM_TID)
#define G_TERM_TID(x) (((x) >> S_TERM_TID) & M_TERM_TID)
#endif				/* T3_CPL_H */
