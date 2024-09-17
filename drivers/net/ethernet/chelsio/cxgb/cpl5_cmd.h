/* SPDX-License-Identifier: GPL-2.0-only */
/*****************************************************************************
 *                                                                           *
 * File: cpl5_cmd.h                                                          *
 * $Revision: 1.6 $                                                          *
 * $Date: 2005/06/21 18:29:47 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CXGB_CPL5_CMD_H_
#define _CXGB_CPL5_CMD_H_

#include <asm/byteorder.h>

#if !defined(__LITTLE_ENDIAN_BITFIELD) && !defined(__BIG_ENDIAN_BITFIELD)
#error "Adjust your <asm/byteorder.h> defines"
#endif

enum CPL_opcode {
	CPL_PASS_OPEN_REQ     = 0x1,
	CPL_PASS_OPEN_RPL     = 0x2,
	CPL_PASS_ESTABLISH    = 0x3,
	CPL_PASS_ACCEPT_REQ   = 0xE,
	CPL_PASS_ACCEPT_RPL   = 0x4,
	CPL_ACT_OPEN_REQ      = 0x5,
	CPL_ACT_OPEN_RPL      = 0x6,
	CPL_CLOSE_CON_REQ     = 0x7,
	CPL_CLOSE_CON_RPL     = 0x8,
	CPL_CLOSE_LISTSRV_REQ = 0x9,
	CPL_CLOSE_LISTSRV_RPL = 0xA,
	CPL_ABORT_REQ         = 0xB,
	CPL_ABORT_RPL         = 0xC,
	CPL_PEER_CLOSE        = 0xD,
	CPL_ACT_ESTABLISH     = 0x17,

	CPL_GET_TCB           = 0x24,
	CPL_GET_TCB_RPL       = 0x25,
	CPL_SET_TCB           = 0x26,
	CPL_SET_TCB_FIELD     = 0x27,
	CPL_SET_TCB_RPL       = 0x28,
	CPL_PCMD              = 0x29,

	CPL_PCMD_READ         = 0x31,
	CPL_PCMD_READ_RPL     = 0x32,


	CPL_RX_DATA           = 0xA0,
	CPL_RX_DATA_DDP       = 0xA1,
	CPL_RX_DATA_ACK       = 0xA3,
	CPL_RX_PKT            = 0xAD,
	CPL_RX_ISCSI_HDR      = 0xAF,
	CPL_TX_DATA_ACK       = 0xB0,
	CPL_TX_DATA           = 0xB1,
	CPL_TX_PKT            = 0xB2,
	CPL_TX_PKT_LSO        = 0xB6,

	CPL_RTE_DELETE_REQ    = 0xC0,
	CPL_RTE_DELETE_RPL    = 0xC1,
	CPL_RTE_WRITE_REQ     = 0xC2,
	CPL_RTE_WRITE_RPL     = 0xD3,
	CPL_RTE_READ_REQ      = 0xC3,
	CPL_RTE_READ_RPL      = 0xC4,
	CPL_L2T_WRITE_REQ     = 0xC5,
	CPL_L2T_WRITE_RPL     = 0xD4,
	CPL_L2T_READ_REQ      = 0xC6,
	CPL_L2T_READ_RPL      = 0xC7,
	CPL_SMT_WRITE_REQ     = 0xC8,
	CPL_SMT_WRITE_RPL     = 0xD5,
	CPL_SMT_READ_REQ      = 0xC9,
	CPL_SMT_READ_RPL      = 0xCA,
	CPL_ARP_MISS_REQ      = 0xCD,
	CPL_ARP_MISS_RPL      = 0xCE,
	CPL_MIGRATE_C2T_REQ   = 0xDC,
	CPL_MIGRATE_C2T_RPL   = 0xDD,
	CPL_ERROR             = 0xD7,

	/* internal: driver -> TOM */
	CPL_MSS_CHANGE        = 0xE1
};

#define NUM_CPL_CMDS 256

enum CPL_error {
	CPL_ERR_NONE               = 0,
	CPL_ERR_TCAM_PARITY        = 1,
	CPL_ERR_TCAM_FULL          = 3,
	CPL_ERR_CONN_RESET         = 20,
	CPL_ERR_CONN_EXIST         = 22,
	CPL_ERR_ARP_MISS           = 23,
	CPL_ERR_BAD_SYN            = 24,
	CPL_ERR_CONN_TIMEDOUT      = 30,
	CPL_ERR_XMIT_TIMEDOUT      = 31,
	CPL_ERR_PERSIST_TIMEDOUT   = 32,
	CPL_ERR_FINWAIT2_TIMEDOUT  = 33,
	CPL_ERR_KEEPALIVE_TIMEDOUT = 34,
	CPL_ERR_ABORT_FAILED       = 42,
	CPL_ERR_GENERAL            = 99
};

enum {
	CPL_CONN_POLICY_AUTO = 0,
	CPL_CONN_POLICY_ASK  = 1,
	CPL_CONN_POLICY_DENY = 3
};

enum {
	ULP_MODE_NONE   = 0,
	ULP_MODE_TCPDDP = 1,
	ULP_MODE_ISCSI  = 2,
	ULP_MODE_IWARP  = 3,
	ULP_MODE_SSL    = 4
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

enum {                // TX_PKT_LSO ethernet types
	CPL_ETH_II,
	CPL_ETH_II_VLAN,
	CPL_ETH_802_3,
	CPL_ETH_802_3_VLAN
};

union opcode_tid {
	u32 opcode_tid;
	u8 opcode;
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
	u16 mss;
	u8 wsf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rsvd:4;
	u8 ecn:1;
	u8 sack:1;
	u8 tstamp:1;
#else
	u8 tstamp:1;
	u8 sack:1;
	u8 ecn:1;
	u8 rsvd:4;
#endif
};

struct cpl_pass_open_req {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u32 opt0h;
	u32 opt0l;
	u32 peer_netmask;
	u32 opt1;
};

struct cpl_pass_open_rpl {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u8 resvd[7];
	u8 status;
};

struct cpl_pass_establish {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u32 tos_tid;
	u8  l2t_idx;
	u8  rsvd[3];
	u32 snd_isn;
	u32 rcv_isn;
};

struct cpl_pass_accept_req {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u32 tos_tid;
	struct tcp_options tcp_options;
	u8  dst_mac[6];
	u16 vlan_tag;
	u8  src_mac[6];
	u8  rsvd[2];
	u32 rcv_isn;
	u32 unknown_tcp_options;
};

struct cpl_pass_accept_rpl {
	union opcode_tid ot;
	u32 rsvd0;
	u32 rsvd1;
	u32 peer_ip;
	u32 opt0h;
	union {
		u32 opt0l;
		struct {
		    u8 rsvd[3];
		    u8 status;
		};
	};
};

struct cpl_act_open_req {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u32 opt0h;
	u32 opt0l;
	u32 iff_vlantag;
	u32 rsvd;
};

struct cpl_act_open_rpl {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u32 new_tid;
	u8  rsvd[3];
	u8  status;
};

struct cpl_act_establish {
	union opcode_tid ot;
	u16 local_port;
	u16 peer_port;
	u32 local_ip;
	u32 peer_ip;
	u32 tos_tid;
	u32 rsvd;
	u32 snd_isn;
	u32 rcv_isn;
};

struct cpl_get_tcb {
	union opcode_tid ot;
	u32 rsvd;
};

struct cpl_get_tcb_rpl {
	union opcode_tid ot;
	u16 len;
	u8 rsvd;
	u8 status;
};

struct cpl_set_tcb {
	union opcode_tid ot;
	u16 len;
	u16 rsvd;
};

struct cpl_set_tcb_field {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 offset;
	u32 mask;
	u32 val;
};

struct cpl_set_tcb_rpl {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
};

struct cpl_pcmd {
	union opcode_tid ot;
	u16 dlen_in;
	u16 dlen_out;
	u32 pcmd_parm[2];
};

struct cpl_pcmd_read {
	union opcode_tid ot;
	u32 rsvd1;
	u16 rsvd2;
	u32 addr;
	u16 len;
};

struct cpl_pcmd_read_rpl {
	union opcode_tid ot;
	u16 len;
};

struct cpl_close_con_req {
	union opcode_tid ot;
	u32 rsvd;
};

struct cpl_close_con_rpl {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
	u32 snd_nxt;
	u32 rcv_nxt;
};

struct cpl_close_listserv_req {
	union opcode_tid ot;
	u32 rsvd;
};

struct cpl_close_listserv_rpl {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 status;
};

struct cpl_abort_req {
	union opcode_tid ot;
	u32 rsvd0;
	u8  rsvd1;
	u8  cmd;
	u8  rsvd2[6];
};

struct cpl_abort_rpl {
	union opcode_tid ot;
	u32 rsvd0;
	u8  rsvd1;
	u8  status;
	u8  rsvd2[6];
};

struct cpl_peer_close {
	union opcode_tid ot;
	u32 rsvd;
};

struct cpl_tx_data {
	union opcode_tid ot;
	u32 len;
	u32 rsvd0;
	u16 urg;
	u16 flags;
};

struct cpl_tx_data_ack {
	union opcode_tid ot;
	u32 ack_seq;
};

struct cpl_rx_data {
	union opcode_tid ot;
	u32 len;
	u32 seq;
	u16 urg;
	u8  rsvd;
	u8  status;
};

struct cpl_rx_data_ack {
	union opcode_tid ot;
	u32 credit;
};

struct cpl_rx_data_ddp {
	union opcode_tid ot;
	u32 len;
	u32 seq;
	u32 nxt_seq;
	u32 ulp_crc;
	u16 ddp_status;
	u8  rsvd;
	u8  status;
};

/*
 * We want this header's alignment to be no more stringent than 2-byte aligned.
 * All fields are u8 or u16 except for the length.  However that field is not
 * used so we break it into 2 16-bit parts to easily meet our alignment needs.
 */
struct cpl_tx_pkt {
	u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 iff:4;
	u8 ip_csum_dis:1;
	u8 l4_csum_dis:1;
	u8 vlan_valid:1;
	u8 rsvd:1;
#else
	u8 rsvd:1;
	u8 vlan_valid:1;
	u8 l4_csum_dis:1;
	u8 ip_csum_dis:1;
	u8 iff:4;
#endif
	u16 vlan;
	u16 len_hi;
	u16 len_lo;
};

struct cpl_tx_pkt_lso {
	u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 iff:4;
	u8 ip_csum_dis:1;
	u8 l4_csum_dis:1;
	u8 vlan_valid:1;
	u8 :1;
#else
	u8 :1;
	u8 vlan_valid:1;
	u8 l4_csum_dis:1;
	u8 ip_csum_dis:1;
	u8 iff:4;
#endif
	u16 vlan;
	__be32 len;

	u8 rsvd[5];
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 tcp_hdr_words:4;
	u8 ip_hdr_words:4;
#else
	u8 ip_hdr_words:4;
	u8 tcp_hdr_words:4;
#endif
	__be16 eth_type_mss;
};

struct cpl_rx_pkt {
	u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 iff:4;
	u8 csum_valid:1;
	u8 bad_pkt:1;
	u8 vlan_valid:1;
	u8 rsvd:1;
#else
	u8 rsvd:1;
	u8 vlan_valid:1;
	u8 bad_pkt:1;
	u8 csum_valid:1;
	u8 iff:4;
#endif
	u16 csum;
	u16 vlan;
	u16 len;
};

struct cpl_l2t_write_req {
	union opcode_tid ot;
	u32 params;
	u8 rsvd1[2];
	u8 dst_mac[6];
};

struct cpl_l2t_write_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd[3];
};

struct cpl_l2t_read_req {
	union opcode_tid ot;
	u8 rsvd[3];
	u8 l2t_idx;
};

struct cpl_l2t_read_rpl {
	union opcode_tid ot;
	u32 params;
	u8 rsvd1[2];
	u8 dst_mac[6];
};

struct cpl_smt_write_req {
	union opcode_tid ot;
	u8 rsvd0;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rsvd1:1;
	u8 mtu_idx:3;
	u8 iff:4;
#else
	u8 iff:4;
	u8 mtu_idx:3;
	u8 rsvd1:1;
#endif
	u16 rsvd2;
	u16 rsvd3;
	u8  src_mac1[6];
	u16 rsvd4;
	u8  src_mac0[6];
};

struct cpl_smt_write_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd[3];
};

struct cpl_smt_read_req {
	union opcode_tid ot;
	u8 rsvd0;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rsvd1:4;
	u8 iff:4;
#else
	u8 iff:4;
	u8 rsvd1:4;
#endif
	u16 rsvd2;
};

struct cpl_smt_read_rpl {
	union opcode_tid ot;
	u8 status;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rsvd1:1;
	u8 mtu_idx:3;
	u8 rsvd0:4;
#else
	u8 rsvd0:4;
	u8 mtu_idx:3;
	u8 rsvd1:1;
#endif
	u16 rsvd2;
	u16 rsvd3;
	u8  src_mac1[6];
	u16 rsvd4;
	u8  src_mac0[6];
};

struct cpl_rte_delete_req {
	union opcode_tid ot;
	u32 params;
};

struct cpl_rte_delete_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd[3];
};

struct cpl_rte_write_req {
	union opcode_tid ot;
	u32 params;
	u32 netmask;
	u32 faddr;
};

struct cpl_rte_write_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd[3];
};

struct cpl_rte_read_req {
	union opcode_tid ot;
	u32 params;
};

struct cpl_rte_read_rpl {
	union opcode_tid ot;
	u8 status;
	u8 rsvd0[2];
	u8 l2t_idx;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rsvd1:7;
	u8 select:1;
#else
	u8 select:1;
	u8 rsvd1:7;
#endif
	u8 rsvd2[3];
	u32 addr;
};

struct cpl_mss_change {
	union opcode_tid ot;
	u32 mss;
};

#endif /* _CXGB_CPL5_CMD_H_ */
