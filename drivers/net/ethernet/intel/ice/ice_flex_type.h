/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_FLEX_TYPE_H_
#define _ICE_FLEX_TYPE_H_
#include "ice_ddp.h"

/* Packet Type (PTYPE) values */
#define ICE_PTYPE_MAC_PAY		1
#define ICE_PTYPE_IPV4_PAY		23
#define ICE_PTYPE_IPV4_UDP_PAY		24
#define ICE_PTYPE_IPV4_TCP_PAY		26
#define ICE_PTYPE_IPV4_SCTP_PAY		27
#define ICE_PTYPE_IPV6_PAY		89
#define ICE_PTYPE_IPV6_UDP_PAY		90
#define ICE_PTYPE_IPV6_TCP_PAY		92
#define ICE_PTYPE_IPV6_SCTP_PAY		93
#define ICE_MAC_IPV4_ESP		160
#define ICE_MAC_IPV6_ESP		161
#define ICE_MAC_IPV4_AH			162
#define ICE_MAC_IPV6_AH			163
#define ICE_MAC_IPV4_NAT_T_ESP		164
#define ICE_MAC_IPV6_NAT_T_ESP		165
#define ICE_MAC_IPV4_GTPU		329
#define ICE_MAC_IPV6_GTPU		330
#define ICE_MAC_IPV4_GTPU_IPV4_FRAG	331
#define ICE_MAC_IPV4_GTPU_IPV4_PAY	332
#define ICE_MAC_IPV4_GTPU_IPV4_UDP_PAY	333
#define ICE_MAC_IPV4_GTPU_IPV4_TCP	334
#define ICE_MAC_IPV4_GTPU_IPV4_ICMP	335
#define ICE_MAC_IPV6_GTPU_IPV4_FRAG	336
#define ICE_MAC_IPV6_GTPU_IPV4_PAY	337
#define ICE_MAC_IPV6_GTPU_IPV4_UDP_PAY	338
#define ICE_MAC_IPV6_GTPU_IPV4_TCP	339
#define ICE_MAC_IPV6_GTPU_IPV4_ICMP	340
#define ICE_MAC_IPV4_GTPU_IPV6_FRAG	341
#define ICE_MAC_IPV4_GTPU_IPV6_PAY	342
#define ICE_MAC_IPV4_GTPU_IPV6_UDP_PAY	343
#define ICE_MAC_IPV4_GTPU_IPV6_TCP	344
#define ICE_MAC_IPV4_GTPU_IPV6_ICMPV6	345
#define ICE_MAC_IPV6_GTPU_IPV6_FRAG	346
#define ICE_MAC_IPV6_GTPU_IPV6_PAY	347
#define ICE_MAC_IPV6_GTPU_IPV6_UDP_PAY	348
#define ICE_MAC_IPV6_GTPU_IPV6_TCP	349
#define ICE_MAC_IPV6_GTPU_IPV6_ICMPV6	350
#define ICE_MAC_IPV4_PFCP_SESSION	352
#define ICE_MAC_IPV6_PFCP_SESSION	354
#define ICE_MAC_IPV4_L2TPV3		360
#define ICE_MAC_IPV6_L2TPV3		361

/* Attributes that can modify PTYPE definitions.
 *
 * These values will represent special attributes for PTYPEs, which will
 * resolve into metadata packet flags definitions that can be used in the TCAM
 * for identifying a PTYPE with specific characteristics.
 */
enum ice_ptype_attrib_type {
	/* GTP PTYPEs */
	ICE_PTYPE_ATTR_GTP_PDU_EH,
	ICE_PTYPE_ATTR_GTP_SESSION,
	ICE_PTYPE_ATTR_GTP_DOWNLINK,
	ICE_PTYPE_ATTR_GTP_UPLINK,
};

struct ice_ptype_attrib_info {
	u16 flags;
	u16 mask;
};

/* TCAM flag definitions */
#define ICE_GTP_PDU			BIT(14)
#define ICE_GTP_PDU_LINK		BIT(13)

/* GTP attributes */
#define ICE_GTP_PDU_FLAG_MASK		(ICE_GTP_PDU)
#define ICE_GTP_PDU_EH			ICE_GTP_PDU

#define ICE_GTP_FLAGS_MASK		(ICE_GTP_PDU | ICE_GTP_PDU_LINK)
#define ICE_GTP_SESSION			0
#define ICE_GTP_DOWNLINK		ICE_GTP_PDU
#define ICE_GTP_UPLINK			(ICE_GTP_PDU | ICE_GTP_PDU_LINK)

struct ice_ptype_attributes {
	u16 ptype;
	enum ice_ptype_attrib_type attrib;
};

/* Tunnel enabling */

enum ice_tunnel_type {
	TNL_VXLAN = 0,
	TNL_GENEVE,
	TNL_GRETAP,
	TNL_GTPC,
	TNL_GTPU,
	__TNL_TYPE_CNT,
	TNL_LAST = 0xFF,
	TNL_ALL = 0xFF,
};

struct ice_tunnel_type_scan {
	enum ice_tunnel_type type;
	const char *label_prefix;
};

struct ice_tunnel_entry {
	enum ice_tunnel_type type;
	u16 boost_addr;
	u16 port;
	struct ice_boost_tcam_entry *boost_entry;
	u8 valid;
};

#define ICE_TUNNEL_MAX_ENTRIES	16

struct ice_tunnel_table {
	struct ice_tunnel_entry tbl[ICE_TUNNEL_MAX_ENTRIES];
	u16 count;
	u16 valid_count[__TNL_TYPE_CNT];
};

struct ice_dvm_entry {
	u16 boost_addr;
	u16 enable;
	struct ice_boost_tcam_entry *boost_entry;
};

#define ICE_DVM_MAX_ENTRIES	48

struct ice_dvm_table {
	struct ice_dvm_entry tbl[ICE_DVM_MAX_ENTRIES];
	u16 count;
};

struct ice_pkg_es {
	__le16 count;
	__le16 offset;
	struct ice_fv_word es[];
};

struct ice_es {
	u32 sid;
	u16 count;
	u16 fvw;
	u16 *ref_count;
	u32 *mask_ena;
	struct list_head prof_map;
	struct ice_fv_word *t;
	u8 *symm;	/* symmetric setting per profile (RSS blk)*/
	struct mutex prof_map_lock;	/* protect access to profiles list */
	u8 *written;
	u8 reverse; /* set to true to reverse FV order */
};

/* PTYPE Group management */

/* Note: XLT1 table takes 13-bit as input, and results in an 8-bit packet type
 * group (PTG) ID as output.
 *
 * Note: PTG 0 is the default packet type group and it is assumed that all PTYPE
 * are a part of this group until moved to a new PTG.
 */
#define ICE_DEFAULT_PTG	0

struct ice_ptg_entry {
	struct ice_ptg_ptype *first_ptype;
	u8 in_use;
};

struct ice_ptg_ptype {
	struct ice_ptg_ptype *next_ptype;
	u8 ptg;
};

#define ICE_MAX_TCAM_PER_PROFILE	32
#define ICE_MAX_PTG_PER_PROFILE		32

struct ice_prof_map {
	struct list_head list;
	u64 profile_cookie;
	u64 context;
	u8 prof_id;
	u8 ptg_cnt;
	u8 ptg[ICE_MAX_PTG_PER_PROFILE];
	struct ice_ptype_attrib_info attr[ICE_MAX_PTG_PER_PROFILE];
};

#define ICE_INVALID_TCAM	0xFFFF

struct ice_tcam_inf {
	u16 tcam_idx;
	struct ice_ptype_attrib_info attr;
	u8 ptg;
	u8 prof_id;
	u8 in_use;
};

struct ice_vsig_prof {
	struct list_head list;
	u64 profile_cookie;
	u8 prof_id;
	u8 tcam_count;
	struct ice_tcam_inf tcam[ICE_MAX_TCAM_PER_PROFILE];
};

struct ice_vsig_entry {
	struct list_head prop_lst;
	struct ice_vsig_vsi *first_vsi;
	u8 in_use;
};

struct ice_vsig_vsi {
	struct ice_vsig_vsi *next_vsi;
	u32 prop_mask;
	u16 changed;
	u16 vsig;
};

#define ICE_XLT1_CNT	1024
#define ICE_MAX_PTGS	256

/* XLT1 Table */
struct ice_xlt1 {
	struct ice_ptg_entry *ptg_tbl;
	struct ice_ptg_ptype *ptypes;
	u8 *t;
	u32 sid;
	u16 count;
};

#define ICE_XLT2_CNT	768
#define ICE_MAX_VSIGS	768

/* VSIG bit layout:
 * [0:12]: incremental VSIG index 1 to ICE_MAX_VSIGS
 * [13:15]: PF number of device
 */
#define ICE_VSIG_IDX_M	(0x1FFF)
#define ICE_PF_NUM_S	13
#define ICE_PF_NUM_M	(0x07 << ICE_PF_NUM_S)
#define ICE_VSIG_VALUE(vsig, pf_id) \
	((u16)((((u16)(vsig)) & ICE_VSIG_IDX_M) | \
	       (((u16)(pf_id) << ICE_PF_NUM_S) & ICE_PF_NUM_M)))
#define ICE_DEFAULT_VSIG	0

/* XLT2 Table */
struct ice_xlt2 {
	struct ice_vsig_entry *vsig_tbl;
	struct ice_vsig_vsi *vsis;
	u16 *t;
	u32 sid;
	u16 count;
};

/* Profile ID Management */
struct ice_prof_id_key {
	__le16 flags;
	u8 xlt1;
	__le16 xlt2_cdid;
} __packed;

/* Keys are made up of two values, each one-half the size of the key.
 * For TCAM, the entire key is 80 bits wide (or 2, 40-bit wide values)
 */
#define ICE_TCAM_KEY_VAL_SZ	5
#define ICE_TCAM_KEY_SZ		(2 * ICE_TCAM_KEY_VAL_SZ)

struct ice_prof_tcam_entry {
	__le16 addr;
	u8 key[ICE_TCAM_KEY_SZ];
	u8 prof_id;
} __packed;

struct ice_prof_id_section {
	__le16 count;
	struct ice_prof_tcam_entry entry[];
};

struct ice_prof_tcam {
	u32 sid;
	u16 count;
	u16 max_prof_id;
	struct ice_prof_tcam_entry *t;
	u8 cdid_bits; /* # CDID bits to use in key, 0, 2, 4, or 8 */
};

struct ice_prof_redir {
	u8 *t;
	u32 sid;
	u16 count;
};

struct ice_mask {
	u16 mask;	/* 16-bit mask */
	u16 idx;	/* index */
	u16 ref;	/* reference count */
	u8 in_use;	/* non-zero if used */
};

struct ice_masks {
	struct mutex lock; /* lock to protect this structure */
	u16 first;	/* first mask owned by the PF */
	u16 count;	/* number of masks owned by the PF */
#define ICE_PROF_MASK_COUNT 32
	struct ice_mask masks[ICE_PROF_MASK_COUNT];
};

struct ice_prof_id {
	unsigned long *id;
	int count;
};

/* Tables per block */
struct ice_blk_info {
	struct ice_xlt1 xlt1;
	struct ice_xlt2 xlt2;
	struct ice_prof_id prof_id;
	struct ice_prof_tcam prof;
	struct ice_prof_redir prof_redir;
	struct ice_es es;
	struct ice_masks masks;
	u8 overwrite; /* set to true to allow overwrite of table entries */
	u8 is_list_init;
};

enum ice_chg_type {
	ICE_TCAM_NONE = 0,
	ICE_PTG_ES_ADD,
	ICE_TCAM_ADD,
	ICE_VSIG_ADD,
	ICE_VSIG_REM,
	ICE_VSI_MOVE,
};

struct ice_chs_chg {
	struct list_head list_entry;
	enum ice_chg_type type;

	u8 add_ptg;
	u8 add_vsig;
	u8 add_tcam_idx;
	u8 add_prof;
	u16 ptype;
	u8 ptg;
	u8 prof_id;
	u16 vsi;
	u16 vsig;
	u16 orig_vsig;
	u16 tcam_idx;
	struct ice_ptype_attrib_info attr;
};

#define ICE_FLOW_PTYPE_MAX		ICE_XLT1_CNT

enum ice_prof_type {
	ICE_PROF_NON_TUN = 0x1,
	ICE_PROF_TUN_UDP = 0x2,
	ICE_PROF_TUN_GRE = 0x4,
	ICE_PROF_TUN_GTPU = 0x8,
	ICE_PROF_TUN_GTPC = 0x10,
	ICE_PROF_TUN_ALL = 0x1E,
	ICE_PROF_ALL = 0xFF,
};

/* Number of bits/bytes contained in meta init entry. Note, this should be a
 * multiple of 32 bits.
 */
#define ICE_META_INIT_BITS	192
#define ICE_META_INIT_DW_CNT	(ICE_META_INIT_BITS / (sizeof(__le32) * \
				 BITS_PER_BYTE))

/* The meta init Flag field starts at this bit */
#define ICE_META_FLAGS_ST		123

/* The entry and bit to check for Double VLAN Mode (DVM) support */
#define ICE_META_VLAN_MODE_ENTRY	0
#define ICE_META_FLAG_VLAN_MODE		60
#define ICE_META_VLAN_MODE_BIT		(ICE_META_FLAGS_ST + \
					 ICE_META_FLAG_VLAN_MODE)

struct ice_meta_init_entry {
	__le32 bm[ICE_META_INIT_DW_CNT];
};

struct ice_meta_init_section {
	__le16 count;
	__le16 offset;
	struct ice_meta_init_entry entry;
};
#endif /* _ICE_FLEX_TYPE_H_ */
