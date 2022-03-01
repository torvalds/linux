/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_FLEX_TYPE_H_
#define _ICE_FLEX_TYPE_H_

#define ICE_FV_OFFSET_INVAL	0x1FF

/* Extraction Sequence (Field Vector) Table */
struct ice_fv_word {
	u8 prot_id;
	u16 off;		/* Offset within the protocol header */
	u8 resvrd;
} __packed;

#define ICE_MAX_NUM_PROFILES 256

#define ICE_MAX_FV_WORDS 48
struct ice_fv {
	struct ice_fv_word ew[ICE_MAX_FV_WORDS];
};

/* Package and segment headers and tables */
struct ice_pkg_hdr {
	struct ice_pkg_ver pkg_format_ver;
	__le32 seg_count;
	__le32 seg_offset[];
};

/* generic segment */
struct ice_generic_seg_hdr {
#define SEGMENT_TYPE_METADATA	0x00000001
#define SEGMENT_TYPE_ICE	0x00000010
	__le32 seg_type;
	struct ice_pkg_ver seg_format_ver;
	__le32 seg_size;
	char seg_id[ICE_PKG_NAME_SIZE];
};

/* ice specific segment */

union ice_device_id {
	struct {
		__le16 device_id;
		__le16 vendor_id;
	} dev_vend_id;
	__le32 id;
};

struct ice_device_id_entry {
	union ice_device_id device;
	union ice_device_id sub_device;
};

struct ice_seg {
	struct ice_generic_seg_hdr hdr;
	__le32 device_table_count;
	struct ice_device_id_entry device_table[];
};

struct ice_nvm_table {
	__le32 table_count;
	__le32 vers[];
};

struct ice_buf {
#define ICE_PKG_BUF_SIZE	4096
	u8 buf[ICE_PKG_BUF_SIZE];
};

struct ice_buf_table {
	__le32 buf_count;
	struct ice_buf buf_array[];
};

/* global metadata specific segment */
struct ice_global_metadata_seg {
	struct ice_generic_seg_hdr hdr;
	struct ice_pkg_ver pkg_ver;
	__le32 rsvd;
	char pkg_name[ICE_PKG_NAME_SIZE];
};

#define ICE_MIN_S_OFF		12
#define ICE_MAX_S_OFF		4095
#define ICE_MIN_S_SZ		1
#define ICE_MAX_S_SZ		4084

/* section information */
struct ice_section_entry {
	__le32 type;
	__le16 offset;
	__le16 size;
};

#define ICE_MIN_S_COUNT		1
#define ICE_MAX_S_COUNT		511
#define ICE_MIN_S_DATA_END	12
#define ICE_MAX_S_DATA_END	4096

#define ICE_METADATA_BUF	0x80000000

struct ice_buf_hdr {
	__le16 section_count;
	__le16 data_end;
	struct ice_section_entry section_entry[];
};

#define ICE_MAX_ENTRIES_IN_BUF(hd_sz, ent_sz) ((ICE_PKG_BUF_SIZE - \
	struct_size((struct ice_buf_hdr *)0, section_entry, 1) - (hd_sz)) /\
	(ent_sz))

/* ice package section IDs */
#define ICE_SID_METADATA		1
#define ICE_SID_XLT0_SW			10
#define ICE_SID_XLT_KEY_BUILDER_SW	11
#define ICE_SID_XLT1_SW			12
#define ICE_SID_XLT2_SW			13
#define ICE_SID_PROFID_TCAM_SW		14
#define ICE_SID_PROFID_REDIR_SW		15
#define ICE_SID_FLD_VEC_SW		16
#define ICE_SID_CDID_KEY_BUILDER_SW	17

struct ice_meta_sect {
	struct ice_pkg_ver ver;
#define ICE_META_SECT_NAME_SIZE	28
	char name[ICE_META_SECT_NAME_SIZE];
	__le32 track_id;
};

#define ICE_SID_CDID_REDIR_SW		18

#define ICE_SID_XLT0_ACL		20
#define ICE_SID_XLT_KEY_BUILDER_ACL	21
#define ICE_SID_XLT1_ACL		22
#define ICE_SID_XLT2_ACL		23
#define ICE_SID_PROFID_TCAM_ACL		24
#define ICE_SID_PROFID_REDIR_ACL	25
#define ICE_SID_FLD_VEC_ACL		26
#define ICE_SID_CDID_KEY_BUILDER_ACL	27
#define ICE_SID_CDID_REDIR_ACL		28

#define ICE_SID_XLT0_FD			30
#define ICE_SID_XLT_KEY_BUILDER_FD	31
#define ICE_SID_XLT1_FD			32
#define ICE_SID_XLT2_FD			33
#define ICE_SID_PROFID_TCAM_FD		34
#define ICE_SID_PROFID_REDIR_FD		35
#define ICE_SID_FLD_VEC_FD		36
#define ICE_SID_CDID_KEY_BUILDER_FD	37
#define ICE_SID_CDID_REDIR_FD		38

#define ICE_SID_XLT0_RSS		40
#define ICE_SID_XLT_KEY_BUILDER_RSS	41
#define ICE_SID_XLT1_RSS		42
#define ICE_SID_XLT2_RSS		43
#define ICE_SID_PROFID_TCAM_RSS		44
#define ICE_SID_PROFID_REDIR_RSS	45
#define ICE_SID_FLD_VEC_RSS		46
#define ICE_SID_CDID_KEY_BUILDER_RSS	47
#define ICE_SID_CDID_REDIR_RSS		48

#define ICE_SID_RXPARSER_MARKER_PTYPE	55
#define ICE_SID_RXPARSER_BOOST_TCAM	56
#define ICE_SID_TXPARSER_BOOST_TCAM	66

#define ICE_SID_XLT0_PE			80
#define ICE_SID_XLT_KEY_BUILDER_PE	81
#define ICE_SID_XLT1_PE			82
#define ICE_SID_XLT2_PE			83
#define ICE_SID_PROFID_TCAM_PE		84
#define ICE_SID_PROFID_REDIR_PE		85
#define ICE_SID_FLD_VEC_PE		86
#define ICE_SID_CDID_KEY_BUILDER_PE	87
#define ICE_SID_CDID_REDIR_PE		88

/* Label Metadata section IDs */
#define ICE_SID_LBL_FIRST		0x80000010
#define ICE_SID_LBL_RXPARSER_TMEM	0x80000018
/* The following define MUST be updated to reflect the last label section ID */
#define ICE_SID_LBL_LAST		0x80000038

enum ice_block {
	ICE_BLK_SW = 0,
	ICE_BLK_ACL,
	ICE_BLK_FD,
	ICE_BLK_RSS,
	ICE_BLK_PE,
	ICE_BLK_COUNT
};

enum ice_sect {
	ICE_XLT0 = 0,
	ICE_XLT_KB,
	ICE_XLT1,
	ICE_XLT2,
	ICE_PROF_TCAM,
	ICE_PROF_REDIR,
	ICE_VEC_TBL,
	ICE_CDID_KB,
	ICE_CDID_REDIR,
	ICE_SECT_COUNT
};

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

/* package labels */
struct ice_label {
	__le16 value;
#define ICE_PKG_LABEL_SIZE	64
	char name[ICE_PKG_LABEL_SIZE];
};

struct ice_label_section {
	__le16 count;
	struct ice_label label[];
};

#define ICE_MAX_LABELS_IN_BUF ICE_MAX_ENTRIES_IN_BUF( \
	struct_size((struct ice_label_section *)0, label, 1) - \
	sizeof(struct ice_label), sizeof(struct ice_label))

struct ice_sw_fv_section {
	__le16 count;
	__le16 base_offset;
	struct ice_fv fv[];
};

struct ice_sw_fv_list_entry {
	struct list_head list_entry;
	u32 profile_id;
	struct ice_fv *fv_ptr;
};

/* The BOOST TCAM stores the match packet header in reverse order, meaning
 * the fields are reversed; in addition, this means that the normally big endian
 * fields of the packet are now little endian.
 */
struct ice_boost_key_value {
#define ICE_BOOST_REMAINING_HV_KEY	15
	u8 remaining_hv_key[ICE_BOOST_REMAINING_HV_KEY];
	__le16 hv_dst_port_key;
	__le16 hv_src_port_key;
	u8 tcam_search_key;
} __packed;

struct ice_boost_key {
	struct ice_boost_key_value key;
	struct ice_boost_key_value key2;
};

/* package Boost TCAM entry */
struct ice_boost_tcam_entry {
	__le16 addr;
	__le16 reserved;
	/* break up the 40 bytes of key into different fields */
	struct ice_boost_key key;
	u8 boost_hit_index_group;
	/* The following contains bitfields which are not on byte boundaries.
	 * These fields are currently unused by driver software.
	 */
#define ICE_BOOST_BIT_FIELDS		43
	u8 bit_fields[ICE_BOOST_BIT_FIELDS];
};

struct ice_boost_tcam_section {
	__le16 count;
	__le16 reserved;
	struct ice_boost_tcam_entry tcam[];
};

#define ICE_MAX_BST_TCAMS_IN_BUF ICE_MAX_ENTRIES_IN_BUF( \
	struct_size((struct ice_boost_tcam_section *)0, tcam, 1) - \
	sizeof(struct ice_boost_tcam_entry), \
	sizeof(struct ice_boost_tcam_entry))

/* package Marker Ptype TCAM entry */
struct ice_marker_ptype_tcam_entry {
#define ICE_MARKER_PTYPE_TCAM_ADDR_MAX	1024
	__le16 addr;
	__le16 ptype;
	u8 keys[20];
};

struct ice_marker_ptype_tcam_section {
	__le16 count;
	__le16 reserved;
	struct ice_marker_ptype_tcam_entry tcam[];
};

#define ICE_MAX_MARKER_PTYPE_TCAMS_IN_BUF	\
	ICE_MAX_ENTRIES_IN_BUF(struct_size((struct ice_marker_ptype_tcam_section *)0, tcam, 1) - \
	sizeof(struct ice_marker_ptype_tcam_entry), \
	sizeof(struct ice_marker_ptype_tcam_entry))

struct ice_xlt1_section {
	__le16 count;
	__le16 offset;
	u8 value[];
};

struct ice_xlt2_section {
	__le16 count;
	__le16 offset;
	__le16 value[];
};

struct ice_prof_redir_section {
	__le16 count;
	__le16 offset;
	u8 redir_value[];
};

/* package buffer building */

struct ice_buf_build {
	struct ice_buf buf;
	u16 reserved_section_table_entries;
};

struct ice_pkg_enum {
	struct ice_buf_table *buf_table;
	u32 buf_idx;

	u32 type;
	struct ice_buf_hdr *buf;
	u32 sect_idx;
	void *sect;
	u32 sect_type;

	u32 entry_idx;
	void *(*handler)(u32 sect_type, void *section, u32 index, u32 *offset);
};

/* Tunnel enabling */

enum ice_tunnel_type {
	TNL_VXLAN = 0,
	TNL_GENEVE,
	TNL_GRETAP,
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

/* Tables per block */
struct ice_blk_info {
	struct ice_xlt1 xlt1;
	struct ice_xlt2 xlt2;
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
	ICE_PROF_TUN_ALL = 0x6,
	ICE_PROF_ALL = 0xFF,
};
#endif /* _ICE_FLEX_TYPE_H_ */
