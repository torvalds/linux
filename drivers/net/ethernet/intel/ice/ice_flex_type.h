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

#define ICE_MAX_FV_WORDS 48
struct ice_fv {
	struct ice_fv_word ew[ICE_MAX_FV_WORDS];
};

/* Package and segment headers and tables */
struct ice_pkg_hdr {
	struct ice_pkg_ver format_ver;
	__le32 seg_count;
	__le32 seg_offset[1];
};

/* generic segment */
struct ice_generic_seg_hdr {
#define SEGMENT_TYPE_METADATA	0x00000001
#define SEGMENT_TYPE_ICE	0x00000010
	__le32 seg_type;
	struct ice_pkg_ver seg_ver;
	__le32 seg_size;
	char seg_name[ICE_PKG_NAME_SIZE];
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
	struct ice_device_id_entry device_table[1];
};

struct ice_nvm_table {
	__le32 table_count;
	__le32 vers[1];
};

struct ice_buf {
#define ICE_PKG_BUF_SIZE	4096
	u8 buf[ICE_PKG_BUF_SIZE];
};

struct ice_buf_table {
	__le32 buf_count;
	struct ice_buf buf_array[1];
};

/* global metadata specific segment */
struct ice_global_metadata_seg {
	struct ice_generic_seg_hdr hdr;
	struct ice_pkg_ver pkg_ver;
	__le32 track_id;
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
	struct ice_section_entry section_entry[1];
};

#define ICE_MAX_ENTRIES_IN_BUF(hd_sz, ent_sz) ((ICE_PKG_BUF_SIZE - \
	sizeof(struct ice_buf_hdr) - (hd_sz)) / (ent_sz))

/* ice package section IDs */
#define ICE_SID_XLT1_SW			12
#define ICE_SID_XLT2_SW			13
#define ICE_SID_PROFID_TCAM_SW		14
#define ICE_SID_PROFID_REDIR_SW		15
#define ICE_SID_FLD_VEC_SW		16

#define ICE_SID_XLT1_ACL		22
#define ICE_SID_XLT2_ACL		23
#define ICE_SID_PROFID_TCAM_ACL		24
#define ICE_SID_PROFID_REDIR_ACL	25
#define ICE_SID_FLD_VEC_ACL		26

#define ICE_SID_XLT1_FD			32
#define ICE_SID_XLT2_FD			33
#define ICE_SID_PROFID_TCAM_FD		34
#define ICE_SID_PROFID_REDIR_FD		35
#define ICE_SID_FLD_VEC_FD		36

#define ICE_SID_XLT1_RSS		42
#define ICE_SID_XLT2_RSS		43
#define ICE_SID_PROFID_TCAM_RSS		44
#define ICE_SID_PROFID_REDIR_RSS	45
#define ICE_SID_FLD_VEC_RSS		46

#define ICE_SID_RXPARSER_BOOST_TCAM	56

#define ICE_SID_XLT1_PE			82
#define ICE_SID_XLT2_PE			83
#define ICE_SID_PROFID_TCAM_PE		84
#define ICE_SID_PROFID_REDIR_PE		85
#define ICE_SID_FLD_VEC_PE		86

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

/* package labels */
struct ice_label {
	__le16 value;
#define ICE_PKG_LABEL_SIZE	64
	char name[ICE_PKG_LABEL_SIZE];
};

struct ice_label_section {
	__le16 count;
	struct ice_label label[1];
};

#define ICE_MAX_LABELS_IN_BUF ICE_MAX_ENTRIES_IN_BUF( \
	sizeof(struct ice_label_section) - sizeof(struct ice_label), \
	sizeof(struct ice_label))

struct ice_sw_fv_section {
	__le16 count;
	__le16 base_offset;
	struct ice_fv fv[1];
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
	struct ice_boost_tcam_entry tcam[1];
};

#define ICE_MAX_BST_TCAMS_IN_BUF ICE_MAX_ENTRIES_IN_BUF( \
	sizeof(struct ice_boost_tcam_section) - \
	sizeof(struct ice_boost_tcam_entry), \
	sizeof(struct ice_boost_tcam_entry))

struct ice_xlt1_section {
	__le16 count;
	__le16 offset;
	u8 value[1];
} __packed;

struct ice_xlt2_section {
	__le16 count;
	__le16 offset;
	__le16 value[1];
};

struct ice_prof_redir_section {
	__le16 count;
	__le16 offset;
	u8 redir_value[1];
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

struct ice_es {
	u32 sid;
	u16 count;
	u16 fvw;
	u16 *ref_count;
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

#define ICE_MAX_PTG_PER_PROFILE		32

struct ice_prof_map {
	struct list_head list;
	u64 profile_cookie;
	u64 context;
	u8 prof_id;
	u8 ptg_cnt;
	u8 ptg[ICE_MAX_PTG_PER_PROFILE];
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
	(u16)((((u16)(vsig)) & ICE_VSIG_IDX_M) | \
	      (((u16)(pf_id) << ICE_PF_NUM_S) & ICE_PF_NUM_M))
#define ICE_DEFAULT_VSIG	0

/* XLT2 Table */
struct ice_xlt2 {
	struct ice_vsig_entry *vsig_tbl;
	struct ice_vsig_vsi *vsis;
	u16 *t;
	u32 sid;
	u16 count;
};

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
	struct ice_prof_tcam_entry entry[1];
} __packed;

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

/* Tables per block */
struct ice_blk_info {
	struct ice_xlt1 xlt1;
	struct ice_xlt2 xlt2;
	struct ice_prof_tcam prof;
	struct ice_prof_redir prof_redir;
	struct ice_es es;
	u8 overwrite; /* set to true to allow overwrite of table entries */
	u8 is_list_init;
};

#define ICE_FLOW_PTYPE_MAX		ICE_XLT1_CNT
#endif /* _ICE_FLEX_TYPE_H_ */
