/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NPC_H
#define NPC_H

enum NPC_LID_E {
	NPC_LID_LA = 0,
	NPC_LID_LB,
	NPC_LID_LC,
	NPC_LID_LD,
	NPC_LID_LE,
	NPC_LID_LF,
	NPC_LID_LG,
	NPC_LID_LH,
};

#define NPC_LT_NA 0

enum npc_kpu_la_ltype {
	NPC_LT_LA_8023 = 1,
	NPC_LT_LA_ETHER,
	NPC_LT_LA_IH_NIX_ETHER,
	NPC_LT_LA_IH_8_ETHER,
	NPC_LT_LA_IH_4_ETHER,
	NPC_LT_LA_IH_2_ETHER,
	NPC_LT_LA_HIGIG2_ETHER,
	NPC_LT_LA_IH_NIX_HIGIG2_ETHER,
	NPC_LT_LA_CUSTOM0 = 0xE,
	NPC_LT_LA_CUSTOM1 = 0xF,
};

enum npc_kpu_lb_ltype {
	NPC_LT_LB_ETAG = 1,
	NPC_LT_LB_CTAG,
	NPC_LT_LB_STAG_QINQ,
	NPC_LT_LB_BTAG,
	NPC_LT_LB_ITAG,
	NPC_LT_LB_DSA,
	NPC_LT_LB_DSA_VLAN,
	NPC_LT_LB_EDSA,
	NPC_LT_LB_EDSA_VLAN,
	NPC_LT_LB_EXDSA,
	NPC_LT_LB_EXDSA_VLAN,
	NPC_LT_LB_FDSA,
	NPC_LT_LB_CUSTOM0 = 0xE,
	NPC_LT_LB_CUSTOM1 = 0xF,
};

enum npc_kpu_lc_ltype {
	NPC_LT_LC_IP = 1,
	NPC_LT_LC_IP_OPT,
	NPC_LT_LC_IP6,
	NPC_LT_LC_IP6_EXT,
	NPC_LT_LC_ARP,
	NPC_LT_LC_RARP,
	NPC_LT_LC_MPLS,
	NPC_LT_LC_NSH,
	NPC_LT_LC_PTP,
	NPC_LT_LC_FCOE,
	NPC_LT_LC_CUSTOM0 = 0xE,
	NPC_LT_LC_CUSTOM1 = 0xF,
};

/* Don't modify Ltypes upto SCTP, otherwise it will
 * effect flow tag calculation and thus RSS.
 */
enum npc_kpu_ld_ltype {
	NPC_LT_LD_TCP = 1,
	NPC_LT_LD_UDP,
	NPC_LT_LD_ICMP,
	NPC_LT_LD_SCTP,
	NPC_LT_LD_ICMP6,
	NPC_LT_LD_CUSTOM0,
	NPC_LT_LD_CUSTOM1,
	NPC_LT_LD_IGMP = 8,
	NPC_LT_LD_AH,
	NPC_LT_LD_GRE,
	NPC_LT_LD_NVGRE,
	NPC_LT_LD_NSH,
	NPC_LT_LD_TU_MPLS_IN_NSH,
	NPC_LT_LD_TU_MPLS_IN_IP,
};

enum npc_kpu_le_ltype {
	NPC_LT_LE_VXLAN = 1,
	NPC_LT_LE_GENEVE,
	NPC_LT_LE_ESP,
	NPC_LT_LE_GTPU = 4,
	NPC_LT_LE_VXLANGPE,
	NPC_LT_LE_GTPC,
	NPC_LT_LE_NSH,
	NPC_LT_LE_TU_MPLS_IN_GRE,
	NPC_LT_LE_TU_NSH_IN_GRE,
	NPC_LT_LE_TU_MPLS_IN_UDP,
	NPC_LT_LE_CUSTOM0 = 0xE,
	NPC_LT_LE_CUSTOM1 = 0xF,
};

enum npc_kpu_lf_ltype {
	NPC_LT_LF_TU_ETHER = 1,
	NPC_LT_LF_TU_PPP,
	NPC_LT_LF_TU_MPLS_IN_VXLANGPE,
	NPC_LT_LF_TU_NSH_IN_VXLANGPE,
	NPC_LT_LF_TU_MPLS_IN_NSH,
	NPC_LT_LF_TU_3RD_NSH,
	NPC_LT_LF_CUSTOM0 = 0xE,
	NPC_LT_LF_CUSTOM1 = 0xF,
};

enum npc_kpu_lg_ltype {
	NPC_LT_LG_TU_IP = 1,
	NPC_LT_LG_TU_IP6,
	NPC_LT_LG_TU_ARP,
	NPC_LT_LG_TU_ETHER_IN_NSH,
	NPC_LT_LG_CUSTOM0 = 0xE,
	NPC_LT_LG_CUSTOM1 = 0xF,
};

/* Don't modify Ltypes upto SCTP, otherwise it will
 * effect flow tag calculation and thus RSS.
 */
enum npc_kpu_lh_ltype {
	NPC_LT_LH_TU_TCP = 1,
	NPC_LT_LH_TU_UDP,
	NPC_LT_LH_TU_ICMP,
	NPC_LT_LH_TU_SCTP,
	NPC_LT_LH_TU_ICMP6,
	NPC_LT_LH_TU_IGMP = 8,
	NPC_LT_LH_TU_ESP,
	NPC_LT_LH_TU_AH,
	NPC_LT_LH_CUSTOM0 = 0xE,
	NPC_LT_LH_CUSTOM1 = 0xF,
};

/* NPC port kind defines how the incoming or outgoing packets
 * are processed. NPC accepts packets from up to 64 pkinds.
 * Software assigns pkind for each incoming port such as CGX
 * Ethernet interfaces, LBK interfaces, etc.
 */
enum npc_pkind_type {
	NPC_TX_DEF_PKIND = 63ULL,	/* NIX-TX PKIND */
};

struct npc_kpu_profile_cam {
	u8 state;
	u8 state_mask;
	u16 dp0;
	u16 dp0_mask;
	u16 dp1;
	u16 dp1_mask;
	u16 dp2;
	u16 dp2_mask;
};

struct npc_kpu_profile_action {
	u8 errlev;
	u8 errcode;
	u8 dp0_offset;
	u8 dp1_offset;
	u8 dp2_offset;
	u8 bypass_count;
	u8 parse_done;
	u8 next_state;
	u8 ptr_advance;
	u8 cap_ena;
	u8 lid;
	u8 ltype;
	u8 flags;
	u8 offset;
	u8 mask;
	u8 right;
	u8 shift;
};

struct npc_kpu_profile {
	int cam_entries;
	int action_entries;
	const struct npc_kpu_profile_cam *cam;
	const struct npc_kpu_profile_action *action;
};

/* NPC KPU register formats */
struct npc_kpu_cam {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_56     : 8;
	u64 state          : 8;
	u64 dp2_data       : 16;
	u64 dp1_data       : 16;
	u64 dp0_data       : 16;
#else
	u64 dp0_data       : 16;
	u64 dp1_data       : 16;
	u64 dp2_data       : 16;
	u64 state          : 8;
	u64 rsvd_63_56     : 8;
#endif
};

struct npc_kpu_action0 {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_57     : 7;
	u64 byp_count      : 3;
	u64 capture_ena    : 1;
	u64 parse_done     : 1;
	u64 next_state     : 8;
	u64 rsvd_43        : 1;
	u64 capture_lid    : 3;
	u64 capture_ltype  : 4;
	u64 capture_flags  : 8;
	u64 ptr_advance    : 8;
	u64 var_len_offset : 8;
	u64 var_len_mask   : 8;
	u64 var_len_right  : 1;
	u64 var_len_shift  : 3;
#else
	u64 var_len_shift  : 3;
	u64 var_len_right  : 1;
	u64 var_len_mask   : 8;
	u64 var_len_offset : 8;
	u64 ptr_advance    : 8;
	u64 capture_flags  : 8;
	u64 capture_ltype  : 4;
	u64 capture_lid    : 3;
	u64 rsvd_43        : 1;
	u64 next_state     : 8;
	u64 parse_done     : 1;
	u64 capture_ena    : 1;
	u64 byp_count      : 3;
	u64 rsvd_63_57     : 7;
#endif
};

struct npc_kpu_action1 {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_36     : 28;
	u64 errlev         : 4;
	u64 errcode        : 8;
	u64 dp2_offset     : 8;
	u64 dp1_offset     : 8;
	u64 dp0_offset     : 8;
#else
	u64 dp0_offset     : 8;
	u64 dp1_offset     : 8;
	u64 dp2_offset     : 8;
	u64 errcode        : 8;
	u64 errlev         : 4;
	u64 rsvd_63_36     : 28;
#endif
};

struct npc_kpu_pkind_cpi_def {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 ena            : 1;
	u64 rsvd_62_59     : 4;
	u64 lid            : 3;
	u64 ltype_match    : 4;
	u64 ltype_mask     : 4;
	u64 flags_match    : 8;
	u64 flags_mask     : 8;
	u64 add_offset     : 8;
	u64 add_mask       : 8;
	u64 rsvd_15        : 1;
	u64 add_shift      : 3;
	u64 rsvd_11_10     : 2;
	u64 cpi_base       : 10;
#else
	u64 cpi_base       : 10;
	u64 rsvd_11_10     : 2;
	u64 add_shift      : 3;
	u64 rsvd_15        : 1;
	u64 add_mask       : 8;
	u64 add_offset     : 8;
	u64 flags_mask     : 8;
	u64 flags_match    : 8;
	u64 ltype_mask     : 4;
	u64 ltype_match    : 4;
	u64 lid            : 3;
	u64 rsvd_62_59     : 4;
	u64 ena            : 1;
#endif
};

struct nix_rx_action {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64	rsvd_63_61	:3;
	u64	flow_key_alg	:5;
	u64	match_id	:16;
	u64	index		:20;
	u64	pf_func		:16;
	u64	op		:4;
#else
	u64	op		:4;
	u64	pf_func		:16;
	u64	index		:20;
	u64	match_id	:16;
	u64	flow_key_alg	:5;
	u64	rsvd_63_61	:3;
#endif
};

/* NPC_AF_INTFX_KEX_CFG field masks */
#define NPC_PARSE_NIBBLE		GENMASK_ULL(30, 0)

/* NPC_PARSE_KEX_S nibble definitions for each field */
#define NPC_PARSE_NIBBLE_CHAN		GENMASK_ULL(2, 0)
#define NPC_PARSE_NIBBLE_ERRLEV		BIT_ULL(3)
#define NPC_PARSE_NIBBLE_ERRCODE	GENMASK_ULL(5, 4)
#define NPC_PARSE_NIBBLE_L2L3_BCAST	BIT_ULL(6)
#define NPC_PARSE_NIBBLE_LA_FLAGS	GENMASK_ULL(8, 7)
#define NPC_PARSE_NIBBLE_LA_LTYPE	BIT_ULL(9)
#define NPC_PARSE_NIBBLE_LB_FLAGS	GENMASK_ULL(11, 10)
#define NPC_PARSE_NIBBLE_LB_LTYPE	BIT_ULL(12)
#define NPC_PARSE_NIBBLE_LC_FLAGS	GENMASK_ULL(14, 13)
#define NPC_PARSE_NIBBLE_LC_LTYPE	BIT_ULL(15)
#define NPC_PARSE_NIBBLE_LD_FLAGS	GENMASK_ULL(17, 16)
#define NPC_PARSE_NIBBLE_LD_LTYPE	BIT_ULL(18)
#define NPC_PARSE_NIBBLE_LE_FLAGS	GENMASK_ULL(20, 19)
#define NPC_PARSE_NIBBLE_LE_LTYPE	BIT_ULL(21)
#define NPC_PARSE_NIBBLE_LF_FLAGS	GENMASK_ULL(23, 22)
#define NPC_PARSE_NIBBLE_LF_LTYPE	BIT_ULL(24)
#define NPC_PARSE_NIBBLE_LG_FLAGS	GENMASK_ULL(26, 25)
#define NPC_PARSE_NIBBLE_LG_LTYPE	BIT_ULL(27)
#define NPC_PARSE_NIBBLE_LH_FLAGS	GENMASK_ULL(29, 28)
#define NPC_PARSE_NIBBLE_LH_LTYPE	BIT_ULL(30)

/* NIX Receive Vtag Action Structure */
#define VTAG0_VALID_BIT		BIT_ULL(15)
#define VTAG0_TYPE_MASK		GENMASK_ULL(14, 12)
#define VTAG0_LID_MASK		GENMASK_ULL(10, 8)
#define VTAG0_RELPTR_MASK	GENMASK_ULL(7, 0)

struct npc_mcam_kex {
	/* MKEX Profle Header */
	u64 mkex_sign; /* "mcam-kex-profile" (8 bytes/ASCII characters) */
	u8 name[MKEX_NAME_LEN];   /* MKEX Profile name */
	u64 cpu_model;   /* Format as profiled by CPU hardware */
	u64 kpu_version; /* KPU firmware/profile version */
	u64 reserved; /* Reserved for extension */

	/* MKEX Profle Data */
	u64 keyx_cfg[NPC_MAX_INTF]; /* NPC_AF_INTF(0..1)_KEX_CFG */
	/* NPC_AF_KEX_LDATA(0..1)_FLAGS_CFG */
	u64 kex_ld_flags[NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_LID(0..7)_LT(0..15)_LD(0..1)_CFG */
	u64 intf_lid_lt_ld[NPC_MAX_INTF][NPC_MAX_LID][NPC_MAX_LT][NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_LDATA(0..1)_FLAGS(0..15)_CFG */
	u64 intf_ld_flags[NPC_MAX_INTF][NPC_MAX_LD][NPC_MAX_LFL];
} __packed;

struct npc_lt_def {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
};

struct npc_lt_def_ipsec {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
	u8	spi_offset;
	u8	spi_nz;
};

struct npc_lt_def_cfg {
	struct npc_lt_def	rx_ol2;
	struct npc_lt_def	rx_oip4;
	struct npc_lt_def	rx_iip4;
	struct npc_lt_def	rx_oip6;
	struct npc_lt_def	rx_iip6;
	struct npc_lt_def	rx_otcp;
	struct npc_lt_def	rx_itcp;
	struct npc_lt_def	rx_oudp;
	struct npc_lt_def	rx_iudp;
	struct npc_lt_def	rx_osctp;
	struct npc_lt_def	rx_isctp;
	struct npc_lt_def_ipsec	rx_ipsec[2];
	struct npc_lt_def	pck_ol2;
	struct npc_lt_def	pck_oip4;
	struct npc_lt_def	pck_oip6;
	struct npc_lt_def	pck_iip4;
};

#endif /* NPC_H */
