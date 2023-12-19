/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell.
 *
 */

#ifndef NPC_H
#define NPC_H

#define NPC_KEX_CHAN_MASK	0xFFFULL

#define SET_KEX_LD(intf, lid, ltype, ld, cfg)	\
	rvu_write64(rvu, blkaddr,	\
		    NPC_AF_INTFX_LIDX_LTX_LDX_CFG(intf, lid, ltype, ld), cfg)

#define SET_KEX_LDFLAGS(intf, ld, flags, cfg)	\
	rvu_write64(rvu, blkaddr,	\
		    NPC_AF_INTFX_LDATAX_FLAGSX_CFG(intf, ld, flags), cfg)

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
	NPC_LT_LA_HIGIG2_ETHER = 7,
	NPC_LT_LA_IH_NIX_HIGIG2_ETHER,
	NPC_LT_LA_CUSTOM_L2_90B_ETHER,
	NPC_LT_LA_CPT_HDR,
	NPC_LT_LA_CUSTOM_L2_24B_ETHER,
	NPC_LT_LA_CUSTOM_PRE_L2_ETHER,
	NPC_LT_LA_CUSTOM0 = 0xE,
	NPC_LT_LA_CUSTOM1 = 0xF,
};

enum npc_kpu_lb_ltype {
	NPC_LT_LB_ETAG = 1,
	NPC_LT_LB_CTAG,
	NPC_LT_LB_STAG_QINQ,
	NPC_LT_LB_BTAG,
	NPC_LT_LB_PPPOE,
	NPC_LT_LB_DSA,
	NPC_LT_LB_DSA_VLAN,
	NPC_LT_LB_EDSA,
	NPC_LT_LB_EDSA_VLAN,
	NPC_LT_LB_EXDSA,
	NPC_LT_LB_EXDSA_VLAN,
	NPC_LT_LB_FDSA,
	NPC_LT_LB_VLAN_EXDSA,
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
	NPC_LT_LC_NGIO,
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
#define NPC_UNRESERVED_PKIND_COUNT NPC_RX_CUSTOM_PRE_L2_PKIND

enum npc_pkind_type {
	NPC_RX_LBK_PKIND = 0ULL,
	NPC_RX_CUSTOM_PRE_L2_PKIND = 55ULL,
	NPC_RX_VLAN_EXDSA_PKIND = 56ULL,
	NPC_RX_CHLEN24B_PKIND = 57ULL,
	NPC_RX_CPT_HDR_PKIND,
	NPC_RX_CHLEN90B_PKIND,
	NPC_TX_HIGIG_PKIND,
	NPC_RX_HIGIG_PKIND,
	NPC_RX_EDSA_PKIND,
	NPC_TX_DEF_PKIND,	/* NIX-TX PKIND */
};

enum npc_interface_type {
	NPC_INTF_MODE_DEF,
};

/* list of known and supported fields in packet header and
 * fields present in key structure.
 */
enum key_fields {
	NPC_DMAC,
	NPC_SMAC,
	NPC_ETYPE,
	NPC_VLAN_ETYPE_CTAG, /* 0x8100 */
	NPC_VLAN_ETYPE_STAG, /* 0x88A8 */
	NPC_OUTER_VID,
	NPC_INNER_VID,
	NPC_TOS,
	NPC_IPFRAG_IPV4,
	NPC_SIP_IPV4,
	NPC_DIP_IPV4,
	NPC_IPFRAG_IPV6,
	NPC_SIP_IPV6,
	NPC_DIP_IPV6,
	NPC_IPPROTO_TCP,
	NPC_IPPROTO_UDP,
	NPC_IPPROTO_SCTP,
	NPC_IPPROTO_AH,
	NPC_IPPROTO_ESP,
	NPC_IPPROTO_ICMP,
	NPC_IPPROTO_ICMP6,
	NPC_SPORT_TCP,
	NPC_DPORT_TCP,
	NPC_SPORT_UDP,
	NPC_DPORT_UDP,
	NPC_SPORT_SCTP,
	NPC_DPORT_SCTP,
	NPC_IPSEC_SPI,
	NPC_MPLS1_LBTCBOS,
	NPC_MPLS1_TTL,
	NPC_MPLS2_LBTCBOS,
	NPC_MPLS2_TTL,
	NPC_MPLS3_LBTCBOS,
	NPC_MPLS3_TTL,
	NPC_MPLS4_LBTCBOS,
	NPC_MPLS4_TTL,
	NPC_HEADER_FIELDS_MAX,
	NPC_CHAN = NPC_HEADER_FIELDS_MAX, /* Valid when Rx */
	NPC_PF_FUNC, /* Valid when Tx */
	NPC_ERRLEV,
	NPC_ERRCODE,
	NPC_LXMB,
	NPC_EXACT_RESULT,
	NPC_LA,
	NPC_LB,
	NPC_LC,
	NPC_LD,
	NPC_LE,
	NPC_LF,
	NPC_LG,
	NPC_LH,
	/* Ethertype for untagged frame */
	NPC_ETYPE_ETHER,
	/* Ethertype for single tagged frame */
	NPC_ETYPE_TAG1,
	/* Ethertype for double tagged frame */
	NPC_ETYPE_TAG2,
	/* outer vlan tci for single tagged frame */
	NPC_VLAN_TAG1,
	/* outer vlan tci for double tagged frame */
	NPC_VLAN_TAG2,
	/* inner vlan tci for double tagged frame */
	NPC_VLAN_TAG3,
	/* other header fields programmed to extract but not of our interest */
	NPC_UNKNOWN,
	NPC_KEY_FIELDS_MAX,
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
} __packed;

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
} __packed;

struct npc_kpu_profile {
	int cam_entries;
	int action_entries;
	struct npc_kpu_profile_cam *cam;
	struct npc_kpu_profile_action *action;
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
#define NPC_EXACT_NIBBLE_START		40
#define NPC_EXACT_NIBBLE_END		43
#define NPC_EXACT_NIBBLE		GENMASK_ULL(43, 40)

/* NPC_EXACT_KEX_S nibble definitions for each field */
#define NPC_EXACT_NIBBLE_HIT		BIT_ULL(40)
#define NPC_EXACT_NIBBLE_OPC		BIT_ULL(40)
#define NPC_EXACT_NIBBLE_WAY		BIT_ULL(40)
#define NPC_EXACT_NIBBLE_INDEX		GENMASK_ULL(43, 41)

#define NPC_EXACT_RESULT_HIT		BIT_ULL(0)
#define NPC_EXACT_RESULT_OPC		GENMASK_ULL(2, 1)
#define NPC_EXACT_RESULT_WAY		GENMASK_ULL(4, 3)
#define NPC_EXACT_RESULT_IDX		GENMASK_ULL(15, 5)

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

struct nix_tx_action {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64	rsvd_63_48	:16;
	u64	match_id	:16;
	u64	index		:20;
	u64	rsvd_11_8	:8;
	u64	op		:4;
#else
	u64	op		:4;
	u64	rsvd_11_8	:8;
	u64	index		:20;
	u64	match_id	:16;
	u64	rsvd_63_48	:16;
#endif
};

/* NIX Receive Vtag Action Structure */
#define RX_VTAG0_VALID_BIT		BIT_ULL(15)
#define RX_VTAG0_TYPE_MASK		GENMASK_ULL(14, 12)
#define RX_VTAG0_LID_MASK		GENMASK_ULL(10, 8)
#define RX_VTAG0_RELPTR_MASK		GENMASK_ULL(7, 0)
#define RX_VTAG1_VALID_BIT		BIT_ULL(47)
#define RX_VTAG1_TYPE_MASK		GENMASK_ULL(46, 44)
#define RX_VTAG1_LID_MASK		GENMASK_ULL(42, 40)
#define RX_VTAG1_RELPTR_MASK		GENMASK_ULL(39, 32)

/* NIX Transmit Vtag Action Structure */
#define TX_VTAG0_DEF_MASK		GENMASK_ULL(25, 16)
#define TX_VTAG0_OP_MASK		GENMASK_ULL(13, 12)
#define TX_VTAG0_LID_MASK		GENMASK_ULL(10, 8)
#define TX_VTAG0_RELPTR_MASK		GENMASK_ULL(7, 0)
#define TX_VTAG1_DEF_MASK		GENMASK_ULL(57, 48)
#define TX_VTAG1_OP_MASK		GENMASK_ULL(45, 44)
#define TX_VTAG1_LID_MASK		GENMASK_ULL(42, 40)
#define TX_VTAG1_RELPTR_MASK		GENMASK_ULL(39, 32)

/* NPC MCAM reserved entry index per nixlf */
#define NIXLF_UCAST_ENTRY	0
#define NIXLF_BCAST_ENTRY	1
#define NIXLF_ALLMULTI_ENTRY	2
#define NIXLF_PROMISC_ENTRY	3

struct npc_coalesced_kpu_prfl {
#define NPC_SIGN	0x00666f727063706e
#define NPC_PRFL_NAME   "npc_prfls_array"
#define NPC_NAME_LEN	32
	__le64 signature; /* "npcprof\0" (8 bytes/ASCII characters) */
	u8 name[NPC_NAME_LEN]; /* KPU Profile name */
	u64 version; /* KPU firmware/profile version */
	u8 num_prfl; /* No of NPC profiles. */
	u16 prfl_sz[];
};

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

struct npc_kpu_fwdata {
	int	entries;
	/* What follows is:
	 * struct npc_kpu_profile_cam[entries];
	 * struct npc_kpu_profile_action[entries];
	 */
	u8	data[];
} __packed;

struct npc_lt_def {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
} __packed;

struct npc_lt_def_ipsec {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
	u8	spi_offset;
	u8	spi_nz;
} __packed;

struct npc_lt_def_apad {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
	u8	valid;
} __packed;

struct npc_lt_def_color {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
	u8	noffset;
	u8	offset;
} __packed;

struct npc_lt_def_et {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
	u8	valid;
	u8	offset;
} __packed;

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
	struct npc_lt_def_apad	rx_apad0;
	struct npc_lt_def_apad	rx_apad1;
	struct npc_lt_def_color	ovlan;
	struct npc_lt_def_color	ivlan;
	struct npc_lt_def_color	rx_gen0_color;
	struct npc_lt_def_color	rx_gen1_color;
	struct npc_lt_def_et	rx_et[2];
} __packed;

/* Loadable KPU profile firmware data */
struct npc_kpu_profile_fwdata {
#define KPU_SIGN	0x00666f727075706b
#define KPU_NAME_LEN	32
/** Maximum number of custom KPU entries supported by the built-in profile. */
#define KPU_MAX_CST_ENT	6
	/* KPU Profle Header */
	__le64	signature; /* "kpuprof\0" (8 bytes/ASCII characters) */
	u8	name[KPU_NAME_LEN]; /* KPU Profile name */
	__le64	version; /* KPU profile version */
	u8	kpus;
	u8	reserved[7];

	/* Default MKEX profile to be used with this KPU profile. May be
	 * overridden with mkex_profile module parameter. Format is same as for
	 * the MKEX profile to streamline processing.
	 */
	struct npc_mcam_kex	mkex;
	/* LTYPE values for specific HW offloaded protocols. */
	struct npc_lt_def_cfg	lt_def;
	/* Dynamically sized data:
	 *  Custom KPU CAM and ACTION configuration entries.
	 * struct npc_kpu_fwdata kpu[kpus];
	 */
	u8	data[];
} __packed;

struct rvu_npc_mcam_rule {
	struct flow_msg packet;
	struct flow_msg mask;
	u8 intf;
	union {
		struct nix_tx_action tx_action;
		struct nix_rx_action rx_action;
	};
	u64 vtag_action;
	struct list_head list;
	u64 features;
	u16 owner;
	u16 entry;
	u16 cntr;
	bool has_cntr;
	u8 default_rule;
	bool enable;
	bool vfvlan_cfg;
	u16 chan;
	u16 chan_mask;
	u8 lxmb;
};

#endif /* NPC_H */
