/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2024 Intel Corporation */

#ifndef _ICE_PARSER_H_
#define _ICE_PARSER_H_

#define ICE_SEC_DATA_OFFSET				4
#define ICE_SID_RXPARSER_IMEM_ENTRY_SIZE		48
#define ICE_SID_RXPARSER_METADATA_INIT_ENTRY_SIZE	24
#define ICE_SID_RXPARSER_CAM_ENTRY_SIZE			16
#define ICE_SID_RXPARSER_PG_SPILL_ENTRY_SIZE		17
#define ICE_SID_RXPARSER_NOMATCH_CAM_ENTRY_SIZE		12
#define ICE_SID_RXPARSER_NOMATCH_SPILL_ENTRY_SIZE	13
#define ICE_SID_RXPARSER_BOOST_TCAM_ENTRY_SIZE		88
#define ICE_SID_RXPARSER_MARKER_TYPE_ENTRY_SIZE		24
#define ICE_SID_RXPARSER_MARKER_GRP_ENTRY_SIZE		8
#define ICE_SID_RXPARSER_PROTO_GRP_ENTRY_SIZE		24
#define ICE_SID_RXPARSER_FLAG_REDIR_ENTRY_SIZE		1

#define ICE_SEC_LBL_DATA_OFFSET				2
#define ICE_SID_LBL_ENTRY_SIZE				66

/*** ICE_SID_RXPARSER_IMEM section ***/
#define ICE_IMEM_TABLE_SIZE		192

/* TCAM boost Master; if bit is set, and TCAM hit, TCAM output overrides iMEM
 * output.
 */
struct ice_bst_main {
	bool alu0;
	bool alu1;
	bool alu2;
	bool pg;
};

struct ice_bst_keybuilder {
	u8 prio;	/* 0-3: PG precedence within ALUs (3 highest) */
	bool tsr_ctrl;	/* TCAM Search Register control */
};

/* Next protocol Key builder */
struct ice_np_keybuilder {
	u8 opc;
	u8 start_reg0;
	u8 len_reg1;
};

enum ice_np_keybuilder_opcode {
	ICE_NPKB_OPC_EXTRACT	= 0,
	ICE_NPKB_OPC_BUILD	= 1,
	ICE_NPKB_OPC_BYPASS	= 2,
};

/* Parse Graph Key builder */
struct ice_pg_keybuilder {
	bool flag0_ena;
	bool flag1_ena;
	bool flag2_ena;
	bool flag3_ena;
	u8 flag0_idx;
	u8 flag1_idx;
	u8 flag2_idx;
	u8 flag3_idx;
	u8 alu_reg_idx;
};

enum ice_alu_idx {
	ICE_ALU0_IDX	= 0,
	ICE_ALU1_IDX	= 1,
	ICE_ALU2_IDX	= 2,
};

enum ice_alu_opcode {
	ICE_ALU_PARK	= 0,
	ICE_ALU_MOV_ADD	= 1,
	ICE_ALU_ADD	= 2,
	ICE_ALU_MOV_AND	= 4,
	ICE_ALU_AND	= 5,
	ICE_ALU_AND_IMM	= 6,
	ICE_ALU_MOV_OR	= 7,
	ICE_ALU_OR	= 8,
	ICE_ALU_MOV_XOR	= 9,
	ICE_ALU_XOR	= 10,
	ICE_ALU_NOP	= 11,
	ICE_ALU_BR	= 12,
	ICE_ALU_BREQ	= 13,
	ICE_ALU_BRNEQ	= 14,
	ICE_ALU_BRGT	= 15,
	ICE_ALU_BRLT	= 16,
	ICE_ALU_BRGEQ	= 17,
	ICE_ALU_BRLEG	= 18,
	ICE_ALU_SETEQ	= 19,
	ICE_ALU_ANDEQ	= 20,
	ICE_ALU_OREQ	= 21,
	ICE_ALU_SETNEQ	= 22,
	ICE_ALU_ANDNEQ	= 23,
	ICE_ALU_ORNEQ	= 24,
	ICE_ALU_SETGT	= 25,
	ICE_ALU_ANDGT	= 26,
	ICE_ALU_ORGT	= 27,
	ICE_ALU_SETLT	= 28,
	ICE_ALU_ANDLT	= 29,
	ICE_ALU_ORLT	= 30,
	ICE_ALU_MOV_SUB	= 31,
	ICE_ALU_SUB	= 32,
	ICE_ALU_INVALID	= 64,
};

enum ice_proto_off_opcode {
	ICE_PO_OFF_REMAIN	= 0,
	ICE_PO_OFF_HDR_ADD	= 1,
	ICE_PO_OFF_HDR_SUB	= 2,
};

struct ice_alu {
	enum ice_alu_opcode opc;
	u8 src_start;
	u8 src_len;
	bool shift_xlate_sel;
	u8 shift_xlate_key;
	u8 src_reg_id;
	u8 dst_reg_id;
	bool inc0;
	bool inc1;
	u8 proto_offset_opc;
	u8 proto_offset;
	u8 branch_addr;
	u16 imm;
	bool dedicate_flags_ena;
	u8 dst_start;
	u8 dst_len;
	bool flags_extr_imm;
	u8 flags_start_imm;
};

/* Parser program code (iMEM) */
struct ice_imem_item {
	u16 idx;
	struct ice_bst_main b_m;
	struct ice_bst_keybuilder b_kb;
	u8 pg_prio;
	struct ice_np_keybuilder np_kb;
	struct ice_pg_keybuilder pg_kb;
	struct ice_alu alu0;
	struct ice_alu alu1;
	struct ice_alu alu2;
};

/*** ICE_SID_RXPARSER_METADATA_INIT section ***/
#define ICE_METAINIT_TABLE_SIZE		16

/* Metadata Initialization item  */
struct ice_metainit_item {
	u16 idx;

	u8 tsr;		/* TCAM Search key Register */
	u16 ho;		/* Header Offset register */
	u16 pc;		/* Program Counter register */
	u16 pg_rn;	/* Parse Graph Root Node */
	u8 cd;		/* Control Domain ID */

	/* General Purpose Registers */
	bool gpr_a_ctrl;
	u8 gpr_a_data_mdid;
	u8 gpr_a_data_start;
	u8 gpr_a_data_len;
	u8 gpr_a_id;

	bool gpr_b_ctrl;
	u8 gpr_b_data_mdid;
	u8 gpr_b_data_start;
	u8 gpr_b_data_len;
	u8 gpr_b_id;

	bool gpr_c_ctrl;
	u8 gpr_c_data_mdid;
	u8 gpr_c_data_start;
	u8 gpr_c_data_len;
	u8 gpr_c_id;

	bool gpr_d_ctrl;
	u8 gpr_d_data_mdid;
	u8 gpr_d_data_start;
	u8 gpr_d_data_len;
	u8 gpr_d_id;

	u64 flags; /* Initial value for all flags */
};

/*** ICE_SID_RXPARSER_CAM, ICE_SID_RXPARSER_PG_SPILL,
 *    ICE_SID_RXPARSER_NOMATCH_CAM and ICE_SID_RXPARSER_NOMATCH_CAM
 *    sections ***/
#define ICE_PG_CAM_TABLE_SIZE		2048
#define ICE_PG_SP_CAM_TABLE_SIZE	128
#define ICE_PG_NM_CAM_TABLE_SIZE	1024
#define ICE_PG_NM_SP_CAM_TABLE_SIZE	64

struct ice_pg_cam_key {
	bool valid;
	u16 node_id;	/* Node ID of protocol in parse graph */
	bool flag0;
	bool flag1;
	bool flag2;
	bool flag3;
	u8 boost_idx;	/* Boost TCAM match index */
	u16 alu_reg;
	u32 next_proto;	/* next Protocol value */
};

struct ice_pg_nm_cam_key {
	bool valid;
	u16 node_id;
	bool flag0;
	bool flag1;
	bool flag2;
	bool flag3;
	u8 boost_idx;
	u16 alu_reg;
};

struct ice_pg_cam_action {
	u16 next_node;	/* Parser Node ID for the next round */
	u8 next_pc;	/* next Program Counter */
	bool is_pg;	/* is protocol group */
	u8 proto_id;	/* protocol ID or proto group ID */
	bool is_mg;	/* is marker group */
	u8 marker_id;	/* marker ID or marker group ID */
	bool is_last_round;
	bool ho_polarity; /* header offset polarity */
	u16 ho_inc;
};

/* Parse Graph item */
struct ice_pg_cam_item {
	u16 idx;
	struct ice_pg_cam_key key;
	struct ice_pg_cam_action action;
};

/* Parse Graph No Match item */
struct ice_pg_nm_cam_item {
	u16 idx;
	struct ice_pg_nm_cam_key key;
	struct ice_pg_cam_action action;
};

/*** ICE_SID_RXPARSER_BOOST_TCAM and ICE_SID_LBL_RXPARSER_TMEM sections ***/
#define ICE_BST_TCAM_TABLE_SIZE		256
#define ICE_BST_TCAM_KEY_SIZE		20
#define ICE_BST_KEY_TCAM_SIZE		19

/* Boost TCAM item */
struct ice_bst_tcam_item {
	u16 addr;
	u8 key[ICE_BST_TCAM_KEY_SIZE];
	u8 key_inv[ICE_BST_TCAM_KEY_SIZE];
	u8 hit_idx_grp;
	u8 pg_prio;
	struct ice_np_keybuilder np_kb;
	struct ice_pg_keybuilder pg_kb;
	struct ice_alu alu0;
	struct ice_alu alu1;
	struct ice_alu alu2;
};

#define ICE_LBL_LEN			64
struct ice_lbl_item {
	u16 idx;
	char label[ICE_LBL_LEN];
};

/*** ICE_SID_RXPARSER_MARKER_PTYPE section ***/
#define ICE_PTYPE_MK_TCAM_TABLE_SIZE	1024
#define ICE_PTYPE_MK_TCAM_KEY_SIZE	10

struct ice_ptype_mk_tcam_item {
	u16 address;
	u16 ptype;
	u8 key[ICE_PTYPE_MK_TCAM_KEY_SIZE];
	u8 key_inv[ICE_PTYPE_MK_TCAM_KEY_SIZE];
} __packed;

/*** ICE_SID_RXPARSER_MARKER_GRP section ***/
#define ICE_MK_GRP_TABLE_SIZE		128
#define ICE_MK_COUNT_PER_GRP		8

/*  Marker Group item */
struct ice_mk_grp_item {
	int idx;
	u8 markers[ICE_MK_COUNT_PER_GRP];
};

/*** ICE_SID_RXPARSER_PROTO_GRP section ***/
#define ICE_PROTO_COUNT_PER_GRP		8
#define ICE_PROTO_GRP_TABLE_SIZE	192
#define ICE_PROTO_GRP_ITEM_SIZE		22
struct ice_proto_off {
	bool polarity;	/* true: positive, false: negative */
	u8 proto_id;
	u16 offset;	/* 10 bit protocol offset */
};

/*  Protocol Group item */
struct ice_proto_grp_item {
	u16 idx;
	struct ice_proto_off po[ICE_PROTO_COUNT_PER_GRP];
};

/*** ICE_SID_RXPARSER_FLAG_REDIR section ***/
#define ICE_FLG_RD_TABLE_SIZE	64

/* Flags Redirection item */
struct ice_flg_rd_item {
	u16 idx;
	bool expose;
	u8 intr_flg_id;	/* Internal Flag ID */
};

/*** ICE_SID_XLT_KEY_BUILDER_SW, ICE_SID_XLT_KEY_BUILDER_ACL,
 * ICE_SID_XLT_KEY_BUILDER_FD and ICE_SID_XLT_KEY_BUILDER_RSS
 * sections ***/
#define ICE_XLT_KB_FLAG0_14_CNT		15
#define ICE_XLT_KB_TBL_CNT		8
#define ICE_XLT_KB_TBL_ENTRY_SIZE	24

struct ice_xlt_kb_entry {
	u8 xlt1_ad_sel;
	u8 xlt2_ad_sel;
	u16 flg0_14_sel[ICE_XLT_KB_FLAG0_14_CNT];
	u8 xlt1_md_sel;
	u8 xlt2_md_sel;
};

/* XLT Key Builder */
struct ice_xlt_kb {
	u8 xlt1_pm;	/* XLT1 Partition Mode */
	u8 xlt2_pm;	/* XLT2 Partition Mode */
	u8 prof_id_pm;	/* Profile ID Partition Mode */
	u64 flag15;

	struct ice_xlt_kb_entry entries[ICE_XLT_KB_TBL_CNT];
};

/*** Parser API ***/
struct ice_parser {
	struct ice_hw *hw; /* pointer to the hardware structure */

	struct ice_imem_item *imem_table;
	struct ice_metainit_item *mi_table;

	struct ice_pg_cam_item *pg_cam_table;
	struct ice_pg_cam_item *pg_sp_cam_table;
	struct ice_pg_nm_cam_item *pg_nm_cam_table;
	struct ice_pg_nm_cam_item *pg_nm_sp_cam_table;

	struct ice_bst_tcam_item *bst_tcam_table;
	struct ice_lbl_item *bst_lbl_table;
	struct ice_ptype_mk_tcam_item *ptype_mk_tcam_table;
	struct ice_mk_grp_item *mk_grp_table;
	struct ice_proto_grp_item *proto_grp_table;
	struct ice_flg_rd_item *flg_rd_table;

	struct ice_xlt_kb *xlt_kb_sw;
	struct ice_xlt_kb *xlt_kb_acl;
	struct ice_xlt_kb *xlt_kb_fd;
	struct ice_xlt_kb *xlt_kb_rss;
};

struct ice_parser *ice_parser_create(struct ice_hw *hw);
void ice_parser_destroy(struct ice_parser *psr);
#endif /* _ICE_PARSER_H_ */
