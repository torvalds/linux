/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OTX2_STRUCT_H
#define OTX2_STRUCT_H

/* NIX WQE/CQE size 128 byte or 512 byte */
enum nix_cqesz_e {
	NIX_XQESZ_W64 = 0x0,
	NIX_XQESZ_W16 = 0x1,
};

enum nix_sqes_e {
	NIX_SQESZ_W16 = 0x0,
	NIX_SQESZ_W8 = 0x1,
};

enum nix_send_ldtype {
	NIX_SEND_LDTYPE_LDD  = 0x0,
	NIX_SEND_LDTYPE_LDT  = 0x1,
	NIX_SEND_LDTYPE_LDWB = 0x2,
};

/* CSUM offload */
enum nix_sendl3type {
	NIX_SENDL3TYPE_NONE = 0x0,
	NIX_SENDL3TYPE_IP4 = 0x2,
	NIX_SENDL3TYPE_IP4_CKSUM = 0x3,
	NIX_SENDL3TYPE_IP6 = 0x4,
};

enum nix_sendl4type {
	NIX_SENDL4TYPE_NONE,
	NIX_SENDL4TYPE_TCP_CKSUM,
	NIX_SENDL4TYPE_SCTP_CKSUM,
	NIX_SENDL4TYPE_UDP_CKSUM,
};

/* NIX wqe/cqe types */
enum nix_xqe_type {
	NIX_XQE_TYPE_INVALID   = 0x0,
	NIX_XQE_TYPE_RX        = 0x1,
	NIX_XQE_TYPE_RX_IPSECS = 0x2,
	NIX_XQE_TYPE_RX_IPSECH = 0x3,
	NIX_XQE_TYPE_RX_IPSECD = 0x4,
	NIX_XQE_TYPE_SEND      = 0x8,
};

/* NIX CQE/SQE subdescriptor types */
enum nix_subdc {
	NIX_SUBDC_NOP  = 0x0,
	NIX_SUBDC_EXT  = 0x1,
	NIX_SUBDC_CRC  = 0x2,
	NIX_SUBDC_IMM  = 0x3,
	NIX_SUBDC_SG   = 0x4,
	NIX_SUBDC_MEM  = 0x5,
	NIX_SUBDC_JUMP = 0x6,
	NIX_SUBDC_WORK = 0x7,
	NIX_SUBDC_SOD  = 0xf,
};

/* Algorithm for nix_sqe_mem_s header (value of the `alg` field) */
enum nix_sendmemalg {
	NIX_SENDMEMALG_E_SET       = 0x0,
	NIX_SENDMEMALG_E_SETTSTMP  = 0x1,
	NIX_SENDMEMALG_E_SETRSLT   = 0x2,
	NIX_SENDMEMALG_E_ADD       = 0x8,
	NIX_SENDMEMALG_E_SUB       = 0x9,
	NIX_SENDMEMALG_E_ADDLEN    = 0xa,
	NIX_SENDMEMALG_E_SUBLEN    = 0xb,
	NIX_SENDMEMALG_E_ADDMBUF   = 0xc,
	NIX_SENDMEMALG_E_SUBMBUF   = 0xd,
	NIX_SENDMEMALG_E_ENUM_LAST = 0xe,
};

/* NIX CQE header structure */
struct nix_cqe_hdr_s {
	u64 flow_tag              : 32;
	u64 q                     : 20;
	u64 reserved_52_57        : 6;
	u64 node                  : 2;
	u64 cqe_type              : 4;
};

/* NIX CQE RX parse structure */
struct nix_rx_parse_s {
	u64 chan         : 12;
	u64 desc_sizem1  : 5;
	u64 rsvd_17      : 1;
	u64 express      : 1;
	u64 wqwd         : 1;
	u64 errlev       : 4;
	u64 errcode      : 8;
	u64 latype       : 4;
	u64 lbtype       : 4;
	u64 lctype       : 4;
	u64 ldtype       : 4;
	u64 letype       : 4;
	u64 lftype       : 4;
	u64 lgtype       : 4;
	u64 lhtype       : 4;
	u64 pkt_lenm1    : 16; /* W1 */
	u64 l2m          : 1;
	u64 l2b          : 1;
	u64 l3m          : 1;
	u64 l3b          : 1;
	u64 vtag0_valid  : 1;
	u64 vtag0_gone   : 1;
	u64 vtag1_valid  : 1;
	u64 vtag1_gone   : 1;
	u64 pkind        : 6;
	u64 rsvd_95_94   : 2;
	u64 vtag0_tci    : 16;
	u64 vtag1_tci    : 16;
	u64 laflags      : 8; /* W2 */
	u64 lbflags      : 8;
	u64 lcflags      : 8;
	u64 ldflags      : 8;
	u64 leflags      : 8;
	u64 lfflags      : 8;
	u64 lgflags      : 8;
	u64 lhflags      : 8;
	u64 eoh_ptr      : 8; /* W3 */
	u64 wqe_aura     : 20;
	u64 pb_aura      : 20;
	u64 match_id     : 16;
	u64 laptr        : 8; /* W4 */
	u64 lbptr        : 8;
	u64 lcptr        : 8;
	u64 ldptr        : 8;
	u64 leptr        : 8;
	u64 lfptr        : 8;
	u64 lgptr        : 8;
	u64 lhptr        : 8;
	u64 vtag0_ptr    : 8; /* W5 */
	u64 vtag1_ptr    : 8;
	u64 flow_key_alg : 5;
	u64 rsvd_383_341 : 43;
	u64 rsvd_447_384;     /* W6 */
};

/* NIX CQE RX scatter/gather subdescriptor structure */
struct nix_rx_sg_s {
	u64 seg_size   : 16; /* W0 */
	u64 seg2_size  : 16;
	u64 seg3_size  : 16;
	u64 segs       : 2;
	u64 rsvd_59_50 : 10;
	u64 subdc      : 4;
	u64 seg_addr;
	u64 seg2_addr;
	u64 seg3_addr;
};

struct nix_send_comp_s {
	u64 status	: 8;
	u64 sqe_id	: 16;
	u64 rsvd_24_63	: 40;
};

struct nix_cqe_rx_s {
	struct nix_cqe_hdr_s  hdr;
	struct nix_rx_parse_s parse;
	struct nix_rx_sg_s sg;
};

struct nix_cqe_tx_s {
	struct nix_cqe_hdr_s  hdr;
	struct nix_send_comp_s comp;
};

/* NIX SQE header structure */
struct nix_sqe_hdr_s {
	u64 total		: 18; /* W0 */
	u64 reserved_18		: 1;
	u64 df			: 1;
	u64 aura		: 20;
	u64 sizem1		: 3;
	u64 pnc			: 1;
	u64 sq			: 20;
	u64 ol3ptr		: 8; /* W1 */
	u64 ol4ptr		: 8;
	u64 il3ptr		: 8;
	u64 il4ptr		: 8;
	u64 ol3type		: 4;
	u64 ol4type		: 4;
	u64 il3type		: 4;
	u64 il4type		: 4;
	u64 sqe_id		: 16;

};

/* NIX send extended header subdescriptor structure */
struct nix_sqe_ext_s {
	u64 lso_mps       : 14; /* W0 */
	u64 lso           : 1;
	u64 tstmp         : 1;
	u64 lso_sb        : 8;
	u64 lso_format    : 5;
	u64 rsvd_31_29    : 3;
	u64 shp_chg       : 9;
	u64 shp_dis       : 1;
	u64 shp_ra        : 2;
	u64 markptr       : 8;
	u64 markform      : 7;
	u64 mark_en       : 1;
	u64 subdc         : 4;
	u64 vlan0_ins_ptr : 8; /* W1 */
	u64 vlan0_ins_tci : 16;
	u64 vlan1_ins_ptr : 8;
	u64 vlan1_ins_tci : 16;
	u64 vlan0_ins_ena : 1;
	u64 vlan1_ins_ena : 1;
	u64 rsvd_127_114  : 14;
};

struct nix_sqe_sg_s {
	u64 seg1_size	: 16;
	u64 seg2_size	: 16;
	u64 seg3_size	: 16;
	u64 segs	: 2;
	u64 rsvd_54_50	: 5;
	u64 i1		: 1;
	u64 i2		: 1;
	u64 i3		: 1;
	u64 ld_type	: 2;
	u64 subdc	: 4;
};

/* NIX send memory subdescriptor structure */
struct nix_sqe_mem_s {
	u64 offset        : 16; /* W0 */
	u64 rsvd_52_16    : 37;
	u64 wmem          : 1;
	u64 dsz           : 2;
	u64 alg           : 4;
	u64 subdc         : 4;
	u64 addr; /* W1 */
};

enum nix_cqerrint_e {
	NIX_CQERRINT_DOOR_ERR = 0,
	NIX_CQERRINT_WR_FULL = 1,
	NIX_CQERRINT_CQE_FAULT = 2,
};

#define NIX_CQERRINT_BITS (BIT_ULL(NIX_CQERRINT_DOOR_ERR) | \
			   BIT_ULL(NIX_CQERRINT_CQE_FAULT))

enum nix_rqint_e {
	NIX_RQINT_DROP = 0,
	NIX_RQINT_RED = 1,
};

#define NIX_RQINT_BITS (BIT_ULL(NIX_RQINT_DROP) | BIT_ULL(NIX_RQINT_RED))

enum nix_sqint_e {
	NIX_SQINT_LMT_ERR = 0,
	NIX_SQINT_MNQ_ERR = 1,
	NIX_SQINT_SEND_ERR = 2,
	NIX_SQINT_SQB_ALLOC_FAIL = 3,
};

#define NIX_SQINT_BITS (BIT_ULL(NIX_SQINT_LMT_ERR) | \
			BIT_ULL(NIX_SQINT_MNQ_ERR) | \
			BIT_ULL(NIX_SQINT_SEND_ERR) | \
			BIT_ULL(NIX_SQINT_SQB_ALLOC_FAIL))

#endif /* OTX2_STRUCT_H */
