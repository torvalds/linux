// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024 Intel Corporation */

#include "ice_common.h"

struct ice_pkg_sect_hdr {
	__le16 count;
	__le16 offset;
};

/**
 * ice_parser_sect_item_get - parse an item from a section
 * @sect_type: section type
 * @section: section object
 * @index: index of the item to get
 * @offset: dummy as prototype of ice_pkg_enum_entry's last parameter
 *
 * Return: a pointer to the item or NULL.
 */
static void *ice_parser_sect_item_get(u32 sect_type, void *section,
				      u32 index, u32 __maybe_unused *offset)
{
	size_t data_off = ICE_SEC_DATA_OFFSET;
	struct ice_pkg_sect_hdr *hdr;
	size_t size;

	if (!section)
		return NULL;

	switch (sect_type) {
	case ICE_SID_RXPARSER_IMEM:
		size = ICE_SID_RXPARSER_IMEM_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_METADATA_INIT:
		size = ICE_SID_RXPARSER_METADATA_INIT_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_CAM:
		size = ICE_SID_RXPARSER_CAM_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_PG_SPILL:
		size = ICE_SID_RXPARSER_PG_SPILL_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_NOMATCH_CAM:
		size = ICE_SID_RXPARSER_NOMATCH_CAM_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_NOMATCH_SPILL:
		size = ICE_SID_RXPARSER_NOMATCH_SPILL_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_BOOST_TCAM:
		size = ICE_SID_RXPARSER_BOOST_TCAM_ENTRY_SIZE;
		break;
	case ICE_SID_LBL_RXPARSER_TMEM:
		data_off = ICE_SEC_LBL_DATA_OFFSET;
		size = ICE_SID_LBL_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_MARKER_PTYPE:
		size = ICE_SID_RXPARSER_MARKER_TYPE_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_MARKER_GRP:
		size = ICE_SID_RXPARSER_MARKER_GRP_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_PROTO_GRP:
		size = ICE_SID_RXPARSER_PROTO_GRP_ENTRY_SIZE;
		break;
	case ICE_SID_RXPARSER_FLAG_REDIR:
		size = ICE_SID_RXPARSER_FLAG_REDIR_ENTRY_SIZE;
		break;
	default:
		return NULL;
	}

	hdr = section;
	if (index >= le16_to_cpu(hdr->count))
		return NULL;

	return section + data_off + index * size;
}

/**
 * ice_parser_create_table - create an item table from a section
 * @hw: pointer to the hardware structure
 * @sect_type: section type
 * @item_size: item size in bytes
 * @length: number of items in the table to create
 * @parse_item: the function to parse the item
 * @no_offset: ignore header offset, calculate index from 0
 *
 * Return: a pointer to the allocated table or ERR_PTR.
 */
static void *
ice_parser_create_table(struct ice_hw *hw, u32 sect_type,
			u32 item_size, u32 length,
			void (*parse_item)(struct ice_hw *hw, u16 idx,
					   void *item, void *data,
					   int size), bool no_offset)
{
	struct ice_pkg_enum state = {};
	struct ice_seg *seg = hw->seg;
	void *table, *data, *item;
	u16 idx = 0;

	if (!seg)
		return ERR_PTR(-EINVAL);

	table = kzalloc(item_size * length, GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	do {
		data = ice_pkg_enum_entry(seg, &state, sect_type, NULL,
					  ice_parser_sect_item_get);
		seg = NULL;
		if (data) {
			struct ice_pkg_sect_hdr *hdr = state.sect;

			if (!no_offset)
				idx = le16_to_cpu(hdr->offset) +
					state.entry_idx;

			item = (void *)((uintptr_t)table + idx * item_size);
			parse_item(hw, idx, item, data, item_size);

			if (no_offset)
				idx++;
		}
	} while (data);

	return table;
}

/*** ICE_SID_RXPARSER_IMEM section ***/
static void ice_imem_bst_bm_dump(struct ice_hw *hw, struct ice_bst_main *bm)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "boost main:\n");
	dev_info(dev, "\talu0 = %d\n", bm->alu0);
	dev_info(dev, "\talu1 = %d\n", bm->alu1);
	dev_info(dev, "\talu2 = %d\n", bm->alu2);
	dev_info(dev, "\tpg = %d\n", bm->pg);
}

static void ice_imem_bst_kb_dump(struct ice_hw *hw,
				 struct ice_bst_keybuilder *kb)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "boost key builder:\n");
	dev_info(dev, "\tpriority = %d\n", kb->prio);
	dev_info(dev, "\ttsr_ctrl = %d\n", kb->tsr_ctrl);
}

static void ice_imem_np_kb_dump(struct ice_hw *hw,
				struct ice_np_keybuilder *kb)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "next proto key builder:\n");
	dev_info(dev, "\topc = %d\n", kb->opc);
	dev_info(dev, "\tstart_or_reg0 = %d\n", kb->start_reg0);
	dev_info(dev, "\tlen_or_reg1 = %d\n", kb->len_reg1);
}

static void ice_imem_pg_kb_dump(struct ice_hw *hw,
				struct ice_pg_keybuilder *kb)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "parse graph key builder:\n");
	dev_info(dev, "\tflag0_ena = %d\n", kb->flag0_ena);
	dev_info(dev, "\tflag1_ena = %d\n", kb->flag1_ena);
	dev_info(dev, "\tflag2_ena = %d\n", kb->flag2_ena);
	dev_info(dev, "\tflag3_ena = %d\n", kb->flag3_ena);
	dev_info(dev, "\tflag0_idx = %d\n", kb->flag0_idx);
	dev_info(dev, "\tflag1_idx = %d\n", kb->flag1_idx);
	dev_info(dev, "\tflag2_idx = %d\n", kb->flag2_idx);
	dev_info(dev, "\tflag3_idx = %d\n", kb->flag3_idx);
	dev_info(dev, "\talu_reg_idx = %d\n", kb->alu_reg_idx);
}

static void ice_imem_alu_dump(struct ice_hw *hw,
			      struct ice_alu *alu, int index)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "alu%d:\n", index);
	dev_info(dev, "\topc = %d\n", alu->opc);
	dev_info(dev, "\tsrc_start = %d\n", alu->src_start);
	dev_info(dev, "\tsrc_len = %d\n", alu->src_len);
	dev_info(dev, "\tshift_xlate_sel = %d\n", alu->shift_xlate_sel);
	dev_info(dev, "\tshift_xlate_key = %d\n", alu->shift_xlate_key);
	dev_info(dev, "\tsrc_reg_id = %d\n", alu->src_reg_id);
	dev_info(dev, "\tdst_reg_id = %d\n", alu->dst_reg_id);
	dev_info(dev, "\tinc0 = %d\n", alu->inc0);
	dev_info(dev, "\tinc1 = %d\n", alu->inc1);
	dev_info(dev, "\tproto_offset_opc = %d\n", alu->proto_offset_opc);
	dev_info(dev, "\tproto_offset = %d\n", alu->proto_offset);
	dev_info(dev, "\tbranch_addr = %d\n", alu->branch_addr);
	dev_info(dev, "\timm = %d\n", alu->imm);
	dev_info(dev, "\tdst_start = %d\n", alu->dst_start);
	dev_info(dev, "\tdst_len = %d\n", alu->dst_len);
	dev_info(dev, "\tflags_extr_imm = %d\n", alu->flags_extr_imm);
	dev_info(dev, "\tflags_start_imm= %d\n", alu->flags_start_imm);
}

/**
 * ice_imem_dump - dump an imem item info
 * @hw: pointer to the hardware structure
 * @item: imem item to dump
 */
static void ice_imem_dump(struct ice_hw *hw, struct ice_imem_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "index = %d\n", item->idx);
	ice_imem_bst_bm_dump(hw, &item->b_m);
	ice_imem_bst_kb_dump(hw, &item->b_kb);
	dev_info(dev, "pg priority = %d\n", item->pg_prio);
	ice_imem_np_kb_dump(hw, &item->np_kb);
	ice_imem_pg_kb_dump(hw, &item->pg_kb);
	ice_imem_alu_dump(hw, &item->alu0, 0);
	ice_imem_alu_dump(hw, &item->alu1, 1);
	ice_imem_alu_dump(hw, &item->alu2, 2);
}

#define ICE_IM_BM_ALU0		BIT(0)
#define ICE_IM_BM_ALU1		BIT(1)
#define ICE_IM_BM_ALU2		BIT(2)
#define ICE_IM_BM_PG		BIT(3)

/**
 * ice_imem_bm_init - parse 4 bits of Boost Main
 * @bm: pointer to the Boost Main structure
 * @data: Boost Main data to be parsed
 */
static void ice_imem_bm_init(struct ice_bst_main *bm, u8 data)
{
	bm->alu0	= FIELD_GET(ICE_IM_BM_ALU0, data);
	bm->alu1	= FIELD_GET(ICE_IM_BM_ALU1, data);
	bm->alu2	= FIELD_GET(ICE_IM_BM_ALU2, data);
	bm->pg		= FIELD_GET(ICE_IM_BM_PG, data);
}

#define ICE_IM_BKB_PRIO		GENMASK(7, 0)
#define ICE_IM_BKB_TSR_CTRL	BIT(8)

/**
 * ice_imem_bkb_init - parse 10 bits of Boost Main Build
 * @bkb: pointer to the Boost Main Build structure
 * @data: Boost Main Build data to be parsed
 */
static void ice_imem_bkb_init(struct ice_bst_keybuilder *bkb, u16 data)
{
	bkb->prio	= FIELD_GET(ICE_IM_BKB_PRIO, data);
	bkb->tsr_ctrl	= FIELD_GET(ICE_IM_BKB_TSR_CTRL, data);
}

#define ICE_IM_NPKB_OPC		GENMASK(1, 0)
#define ICE_IM_NPKB_S_R0	GENMASK(9, 2)
#define ICE_IM_NPKB_L_R1	GENMASK(17, 10)

/**
 * ice_imem_npkb_init - parse 18 bits of Next Protocol Key Build
 * @kb: pointer to the Next Protocol Key Build structure
 * @data: Next Protocol Key Build data to be parsed
 */
static void ice_imem_npkb_init(struct ice_np_keybuilder *kb, u32 data)
{
	kb->opc		= FIELD_GET(ICE_IM_NPKB_OPC, data);
	kb->start_reg0	= FIELD_GET(ICE_IM_NPKB_S_R0, data);
	kb->len_reg1	= FIELD_GET(ICE_IM_NPKB_L_R1, data);
}

#define ICE_IM_PGKB_F0_ENA	BIT_ULL(0)
#define ICE_IM_PGKB_F0_IDX	GENMASK_ULL(6, 1)
#define ICE_IM_PGKB_F1_ENA	BIT_ULL(7)
#define ICE_IM_PGKB_F1_IDX	GENMASK_ULL(13, 8)
#define ICE_IM_PGKB_F2_ENA	BIT_ULL(14)
#define ICE_IM_PGKB_F2_IDX	GENMASK_ULL(20, 15)
#define ICE_IM_PGKB_F3_ENA	BIT_ULL(21)
#define ICE_IM_PGKB_F3_IDX	GENMASK_ULL(27, 22)
#define ICE_IM_PGKB_AR_IDX	GENMASK_ULL(34, 28)

/**
 * ice_imem_pgkb_init - parse 35 bits of Parse Graph Key Build
 * @kb: pointer to the Parse Graph Key Build structure
 * @data: Parse Graph Key Build data to be parsed
 */
static void ice_imem_pgkb_init(struct ice_pg_keybuilder *kb, u64 data)
{
	kb->flag0_ena	= FIELD_GET(ICE_IM_PGKB_F0_ENA, data);
	kb->flag0_idx	= FIELD_GET(ICE_IM_PGKB_F0_IDX, data);
	kb->flag1_ena	= FIELD_GET(ICE_IM_PGKB_F1_ENA, data);
	kb->flag1_idx	= FIELD_GET(ICE_IM_PGKB_F1_IDX, data);
	kb->flag2_ena	= FIELD_GET(ICE_IM_PGKB_F2_ENA, data);
	kb->flag2_idx	= FIELD_GET(ICE_IM_PGKB_F2_IDX, data);
	kb->flag3_ena	= FIELD_GET(ICE_IM_PGKB_F3_ENA, data);
	kb->flag3_idx	= FIELD_GET(ICE_IM_PGKB_F3_IDX, data);
	kb->alu_reg_idx	= FIELD_GET(ICE_IM_PGKB_AR_IDX, data);
}

#define ICE_IM_ALU_OPC		GENMASK_ULL(5, 0)
#define ICE_IM_ALU_SS		GENMASK_ULL(13, 6)
#define ICE_IM_ALU_SL		GENMASK_ULL(18, 14)
#define ICE_IM_ALU_SXS		BIT_ULL(19)
#define ICE_IM_ALU_SXK		GENMASK_ULL(23, 20)
#define ICE_IM_ALU_SRID		GENMASK_ULL(30, 24)
#define ICE_IM_ALU_DRID		GENMASK_ULL(37, 31)
#define ICE_IM_ALU_INC0		BIT_ULL(38)
#define ICE_IM_ALU_INC1		BIT_ULL(39)
#define ICE_IM_ALU_POO		GENMASK_ULL(41, 40)
#define ICE_IM_ALU_PO		GENMASK_ULL(49, 42)
#define ICE_IM_ALU_BA_S		50	/* offset for the 2nd 64-bits field */
#define ICE_IM_ALU_BA		GENMASK_ULL(57 - ICE_IM_ALU_BA_S, \
					    50 - ICE_IM_ALU_BA_S)
#define ICE_IM_ALU_IMM		GENMASK_ULL(73 - ICE_IM_ALU_BA_S, \
					    58 - ICE_IM_ALU_BA_S)
#define ICE_IM_ALU_DFE		BIT_ULL(74 - ICE_IM_ALU_BA_S)
#define ICE_IM_ALU_DS		GENMASK_ULL(80 - ICE_IM_ALU_BA_S, \
					    75 - ICE_IM_ALU_BA_S)
#define ICE_IM_ALU_DL		GENMASK_ULL(86 - ICE_IM_ALU_BA_S, \
					    81 - ICE_IM_ALU_BA_S)
#define ICE_IM_ALU_FEI		BIT_ULL(87 - ICE_IM_ALU_BA_S)
#define ICE_IM_ALU_FSI		GENMASK_ULL(95 - ICE_IM_ALU_BA_S, \
					    88 - ICE_IM_ALU_BA_S)

/**
 * ice_imem_alu_init - parse 96 bits of ALU entry
 * @alu: pointer to the ALU entry structure
 * @data: ALU entry data to be parsed
 * @off: offset of the ALU entry data
 */
static void ice_imem_alu_init(struct ice_alu *alu, u8 *data, u8 off)
{
	u64 d64;
	u8 idd;

	d64 = *((u64 *)data) >> off;

	alu->opc		= FIELD_GET(ICE_IM_ALU_OPC, d64);
	alu->src_start		= FIELD_GET(ICE_IM_ALU_SS, d64);
	alu->src_len		= FIELD_GET(ICE_IM_ALU_SL, d64);
	alu->shift_xlate_sel	= FIELD_GET(ICE_IM_ALU_SXS, d64);
	alu->shift_xlate_key	= FIELD_GET(ICE_IM_ALU_SXK, d64);
	alu->src_reg_id		= FIELD_GET(ICE_IM_ALU_SRID, d64);
	alu->dst_reg_id		= FIELD_GET(ICE_IM_ALU_DRID, d64);
	alu->inc0		= FIELD_GET(ICE_IM_ALU_INC0, d64);
	alu->inc1		= FIELD_GET(ICE_IM_ALU_INC1, d64);
	alu->proto_offset_opc	= FIELD_GET(ICE_IM_ALU_POO, d64);
	alu->proto_offset	= FIELD_GET(ICE_IM_ALU_PO, d64);

	idd = (ICE_IM_ALU_BA_S + off) / BITS_PER_BYTE;
	off = (ICE_IM_ALU_BA_S + off) % BITS_PER_BYTE;
	d64 = *((u64 *)(&data[idd])) >> off;

	alu->branch_addr	= FIELD_GET(ICE_IM_ALU_BA, d64);
	alu->imm		= FIELD_GET(ICE_IM_ALU_IMM, d64);
	alu->dedicate_flags_ena	= FIELD_GET(ICE_IM_ALU_DFE, d64);
	alu->dst_start		= FIELD_GET(ICE_IM_ALU_DS, d64);
	alu->dst_len		= FIELD_GET(ICE_IM_ALU_DL, d64);
	alu->flags_extr_imm	= FIELD_GET(ICE_IM_ALU_FEI, d64);
	alu->flags_start_imm	= FIELD_GET(ICE_IM_ALU_FSI, d64);
}

#define ICE_IMEM_BM_S		0
#define ICE_IMEM_BKB_S		4
#define ICE_IMEM_BKB_IDD	(ICE_IMEM_BKB_S / BITS_PER_BYTE)
#define ICE_IMEM_BKB_OFF	(ICE_IMEM_BKB_S % BITS_PER_BYTE)
#define ICE_IMEM_PGP		GENMASK(15, 14)
#define ICE_IMEM_NPKB_S		16
#define ICE_IMEM_NPKB_IDD	(ICE_IMEM_NPKB_S / BITS_PER_BYTE)
#define ICE_IMEM_NPKB_OFF	(ICE_IMEM_NPKB_S % BITS_PER_BYTE)
#define ICE_IMEM_PGKB_S		34
#define ICE_IMEM_PGKB_IDD	(ICE_IMEM_PGKB_S / BITS_PER_BYTE)
#define ICE_IMEM_PGKB_OFF	(ICE_IMEM_PGKB_S % BITS_PER_BYTE)
#define ICE_IMEM_ALU0_S		69
#define ICE_IMEM_ALU0_IDD	(ICE_IMEM_ALU0_S / BITS_PER_BYTE)
#define ICE_IMEM_ALU0_OFF	(ICE_IMEM_ALU0_S % BITS_PER_BYTE)
#define ICE_IMEM_ALU1_S		165
#define ICE_IMEM_ALU1_IDD	(ICE_IMEM_ALU1_S / BITS_PER_BYTE)
#define ICE_IMEM_ALU1_OFF	(ICE_IMEM_ALU1_S % BITS_PER_BYTE)
#define ICE_IMEM_ALU2_S		357
#define ICE_IMEM_ALU2_IDD	(ICE_IMEM_ALU2_S / BITS_PER_BYTE)
#define ICE_IMEM_ALU2_OFF	(ICE_IMEM_ALU2_S % BITS_PER_BYTE)

/**
 * ice_imem_parse_item - parse 384 bits of IMEM entry
 * @hw: pointer to the hardware structure
 * @idx: index of IMEM entry
 * @item: item of IMEM entry
 * @data: IMEM entry data to be parsed
 * @size: size of IMEM entry
 */
static void ice_imem_parse_item(struct ice_hw *hw, u16 idx, void *item,
				void *data, int __maybe_unused size)
{
	struct ice_imem_item *ii = item;
	u8 *buf = data;

	ii->idx = idx;

	ice_imem_bm_init(&ii->b_m, *(u8 *)buf);
	ice_imem_bkb_init(&ii->b_kb,
			  *((u16 *)(&buf[ICE_IMEM_BKB_IDD])) >>
			   ICE_IMEM_BKB_OFF);

	ii->pg_prio = FIELD_GET(ICE_IMEM_PGP, *(u16 *)buf);

	ice_imem_npkb_init(&ii->np_kb,
			   *((u32 *)(&buf[ICE_IMEM_NPKB_IDD])) >>
			    ICE_IMEM_NPKB_OFF);
	ice_imem_pgkb_init(&ii->pg_kb,
			   *((u64 *)(&buf[ICE_IMEM_PGKB_IDD])) >>
			    ICE_IMEM_PGKB_OFF);

	ice_imem_alu_init(&ii->alu0,
			  &buf[ICE_IMEM_ALU0_IDD],
			  ICE_IMEM_ALU0_OFF);
	ice_imem_alu_init(&ii->alu1,
			  &buf[ICE_IMEM_ALU1_IDD],
			  ICE_IMEM_ALU1_OFF);
	ice_imem_alu_init(&ii->alu2,
			  &buf[ICE_IMEM_ALU2_IDD],
			  ICE_IMEM_ALU2_OFF);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_imem_dump(hw, ii);
}

/**
 * ice_imem_table_get - create an imem table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated IMEM table.
 */
static struct ice_imem_item *ice_imem_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_IMEM,
				       sizeof(struct ice_imem_item),
				       ICE_IMEM_TABLE_SIZE,
				       ice_imem_parse_item, false);
}

/*** ICE_SID_RXPARSER_METADATA_INIT section ***/
/**
 * ice_metainit_dump - dump an metainit item info
 * @hw: pointer to the hardware structure
 * @item: metainit item to dump
 */
static void ice_metainit_dump(struct ice_hw *hw, struct ice_metainit_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "index = %d\n", item->idx);

	dev_info(dev, "tsr = %d\n", item->tsr);
	dev_info(dev, "ho = %d\n", item->ho);
	dev_info(dev, "pc = %d\n", item->pc);
	dev_info(dev, "pg_rn = %d\n", item->pg_rn);
	dev_info(dev, "cd = %d\n", item->cd);

	dev_info(dev, "gpr_a_ctrl = %d\n", item->gpr_a_ctrl);
	dev_info(dev, "gpr_a_data_mdid = %d\n", item->gpr_a_data_mdid);
	dev_info(dev, "gpr_a_data_start = %d\n", item->gpr_a_data_start);
	dev_info(dev, "gpr_a_data_len = %d\n", item->gpr_a_data_len);
	dev_info(dev, "gpr_a_id = %d\n", item->gpr_a_id);

	dev_info(dev, "gpr_b_ctrl = %d\n", item->gpr_b_ctrl);
	dev_info(dev, "gpr_b_data_mdid = %d\n", item->gpr_b_data_mdid);
	dev_info(dev, "gpr_b_data_start = %d\n", item->gpr_b_data_start);
	dev_info(dev, "gpr_b_data_len = %d\n", item->gpr_b_data_len);
	dev_info(dev, "gpr_b_id = %d\n", item->gpr_b_id);

	dev_info(dev, "gpr_c_ctrl = %d\n", item->gpr_c_ctrl);
	dev_info(dev, "gpr_c_data_mdid = %d\n", item->gpr_c_data_mdid);
	dev_info(dev, "gpr_c_data_start = %d\n", item->gpr_c_data_start);
	dev_info(dev, "gpr_c_data_len = %d\n", item->gpr_c_data_len);
	dev_info(dev, "gpr_c_id = %d\n", item->gpr_c_id);

	dev_info(dev, "gpr_d_ctrl = %d\n", item->gpr_d_ctrl);
	dev_info(dev, "gpr_d_data_mdid = %d\n", item->gpr_d_data_mdid);
	dev_info(dev, "gpr_d_data_start = %d\n", item->gpr_d_data_start);
	dev_info(dev, "gpr_d_data_len = %d\n", item->gpr_d_data_len);
	dev_info(dev, "gpr_d_id = %d\n", item->gpr_d_id);

	dev_info(dev, "flags = 0x%llx\n", (unsigned long long)(item->flags));
}

#define ICE_MI_TSR		GENMASK_ULL(7, 0)
#define ICE_MI_HO		GENMASK_ULL(16, 8)
#define ICE_MI_PC		GENMASK_ULL(24, 17)
#define ICE_MI_PGRN		GENMASK_ULL(35, 25)
#define ICE_MI_CD		GENMASK_ULL(38, 36)
#define ICE_MI_GAC		BIT_ULL(39)
#define ICE_MI_GADM		GENMASK_ULL(44, 40)
#define ICE_MI_GADS		GENMASK_ULL(48, 45)
#define ICE_MI_GADL		GENMASK_ULL(53, 49)
#define ICE_MI_GAI		GENMASK_ULL(59, 56)
#define ICE_MI_GBC		BIT_ULL(60)
#define ICE_MI_GBDM_S		61	/* offset for the 2nd 64-bits field */
#define ICE_MI_GBDM_IDD		(ICE_MI_GBDM_S / BITS_PER_BYTE)
#define ICE_MI_GBDM_OFF		(ICE_MI_GBDM_S % BITS_PER_BYTE)

#define ICE_MI_GBDM_GENMASK_ULL(high, low) \
	GENMASK_ULL((high) - ICE_MI_GBDM_S, (low) - ICE_MI_GBDM_S)
#define ICE_MI_GBDM		ICE_MI_GBDM_GENMASK_ULL(65, 61)
#define ICE_MI_GBDS		ICE_MI_GBDM_GENMASK_ULL(69, 66)
#define ICE_MI_GBDL		ICE_MI_GBDM_GENMASK_ULL(74, 70)
#define ICE_MI_GBI		ICE_MI_GBDM_GENMASK_ULL(80, 77)
#define ICE_MI_GCC		BIT_ULL(81 - ICE_MI_GBDM_S)
#define ICE_MI_GCDM		ICE_MI_GBDM_GENMASK_ULL(86, 82)
#define ICE_MI_GCDS		ICE_MI_GBDM_GENMASK_ULL(90, 87)
#define ICE_MI_GCDL		ICE_MI_GBDM_GENMASK_ULL(95, 91)
#define ICE_MI_GCI		ICE_MI_GBDM_GENMASK_ULL(101, 98)
#define ICE_MI_GDC		BIT_ULL(102 - ICE_MI_GBDM_S)
#define ICE_MI_GDDM		ICE_MI_GBDM_GENMASK_ULL(107, 103)
#define ICE_MI_GDDS		ICE_MI_GBDM_GENMASK_ULL(111, 108)
#define ICE_MI_GDDL		ICE_MI_GBDM_GENMASK_ULL(116, 112)
#define ICE_MI_GDI		ICE_MI_GBDM_GENMASK_ULL(122, 119)
#define ICE_MI_FLAG_S		123	/* offset for the 3rd 64-bits field */
#define ICE_MI_FLAG_IDD		(ICE_MI_FLAG_S / BITS_PER_BYTE)
#define ICE_MI_FLAG_OFF		(ICE_MI_FLAG_S % BITS_PER_BYTE)
#define ICE_MI_FLAG		GENMASK_ULL(186 - ICE_MI_FLAG_S, \
					    123 - ICE_MI_FLAG_S)

/**
 * ice_metainit_parse_item - parse 192 bits of Metadata Init entry
 * @hw: pointer to the hardware structure
 * @idx: index of Metadata Init entry
 * @item: item of Metadata Init entry
 * @data: Metadata Init entry data to be parsed
 * @size: size of Metadata Init entry
 */
static void ice_metainit_parse_item(struct ice_hw *hw, u16 idx, void *item,
				    void *data, int __maybe_unused size)
{
	struct ice_metainit_item *mi = item;
	u8 *buf = data;
	u64 d64;

	mi->idx = idx;

	d64 = *(u64 *)buf;

	mi->tsr			= FIELD_GET(ICE_MI_TSR, d64);
	mi->ho			= FIELD_GET(ICE_MI_HO, d64);
	mi->pc			= FIELD_GET(ICE_MI_PC, d64);
	mi->pg_rn		= FIELD_GET(ICE_MI_PGRN, d64);
	mi->cd			= FIELD_GET(ICE_MI_CD, d64);

	mi->gpr_a_ctrl		= FIELD_GET(ICE_MI_GAC, d64);
	mi->gpr_a_data_mdid	= FIELD_GET(ICE_MI_GADM, d64);
	mi->gpr_a_data_start	= FIELD_GET(ICE_MI_GADS, d64);
	mi->gpr_a_data_len	= FIELD_GET(ICE_MI_GADL, d64);
	mi->gpr_a_id		= FIELD_GET(ICE_MI_GAI, d64);

	mi->gpr_b_ctrl		= FIELD_GET(ICE_MI_GBC, d64);

	d64 = *((u64 *)&buf[ICE_MI_GBDM_IDD]) >> ICE_MI_GBDM_OFF;

	mi->gpr_b_data_mdid	= FIELD_GET(ICE_MI_GBDM, d64);
	mi->gpr_b_data_start	= FIELD_GET(ICE_MI_GBDS, d64);
	mi->gpr_b_data_len	= FIELD_GET(ICE_MI_GBDL, d64);
	mi->gpr_b_id		= FIELD_GET(ICE_MI_GBI, d64);

	mi->gpr_c_ctrl		= FIELD_GET(ICE_MI_GCC, d64);
	mi->gpr_c_data_mdid	= FIELD_GET(ICE_MI_GCDM, d64);
	mi->gpr_c_data_start	= FIELD_GET(ICE_MI_GCDS, d64);
	mi->gpr_c_data_len	= FIELD_GET(ICE_MI_GCDL, d64);
	mi->gpr_c_id		= FIELD_GET(ICE_MI_GCI, d64);

	mi->gpr_d_ctrl		= FIELD_GET(ICE_MI_GDC, d64);
	mi->gpr_d_data_mdid	= FIELD_GET(ICE_MI_GDDM, d64);
	mi->gpr_d_data_start	= FIELD_GET(ICE_MI_GDDS, d64);
	mi->gpr_d_data_len	= FIELD_GET(ICE_MI_GDDL, d64);
	mi->gpr_d_id		= FIELD_GET(ICE_MI_GDI, d64);

	d64 = *((u64 *)&buf[ICE_MI_FLAG_IDD]) >> ICE_MI_FLAG_OFF;

	mi->flags		= FIELD_GET(ICE_MI_FLAG, d64);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_metainit_dump(hw, mi);
}

/**
 * ice_metainit_table_get - create a metainit table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Metadata initialization table.
 */
static struct ice_metainit_item *ice_metainit_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_METADATA_INIT,
				       sizeof(struct ice_metainit_item),
				       ICE_METAINIT_TABLE_SIZE,
				       ice_metainit_parse_item, false);
}

/**
 * ice_bst_tcam_search - find a TCAM item with specific type
 * @tcam_table: the TCAM table
 * @lbl_table: the lbl table to search
 * @type: the type we need to match against
 * @start: start searching from this index
 *
 * Return: a pointer to the matching BOOST TCAM item or NULL.
 */
struct ice_bst_tcam_item *
ice_bst_tcam_search(struct ice_bst_tcam_item *tcam_table,
		    struct ice_lbl_item *lbl_table,
		    enum ice_lbl_type type, u16 *start)
{
	u16 i = *start;

	for (; i < ICE_BST_TCAM_TABLE_SIZE; i++) {
		if (lbl_table[i].type == type) {
			*start = i;
			return &tcam_table[lbl_table[i].idx];
		}
	}

	return NULL;
}

/*** ICE_SID_RXPARSER_CAM, ICE_SID_RXPARSER_PG_SPILL,
 *    ICE_SID_RXPARSER_NOMATCH_CAM and ICE_SID_RXPARSER_NOMATCH_CAM
 *    sections ***/
static void ice_pg_cam_key_dump(struct ice_hw *hw, struct ice_pg_cam_key *key)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "key:\n");
	dev_info(dev, "\tvalid = %d\n", key->valid);
	dev_info(dev, "\tnode_id = %d\n", key->node_id);
	dev_info(dev, "\tflag0 = %d\n", key->flag0);
	dev_info(dev, "\tflag1 = %d\n", key->flag1);
	dev_info(dev, "\tflag2 = %d\n", key->flag2);
	dev_info(dev, "\tflag3 = %d\n", key->flag3);
	dev_info(dev, "\tboost_idx = %d\n", key->boost_idx);
	dev_info(dev, "\talu_reg = 0x%04x\n", key->alu_reg);
	dev_info(dev, "\tnext_proto = 0x%08x\n", key->next_proto);
}

static void ice_pg_nm_cam_key_dump(struct ice_hw *hw,
				   struct ice_pg_nm_cam_key *key)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "key:\n");
	dev_info(dev, "\tvalid = %d\n", key->valid);
	dev_info(dev, "\tnode_id = %d\n", key->node_id);
	dev_info(dev, "\tflag0 = %d\n", key->flag0);
	dev_info(dev, "\tflag1 = %d\n", key->flag1);
	dev_info(dev, "\tflag2 = %d\n", key->flag2);
	dev_info(dev, "\tflag3 = %d\n", key->flag3);
	dev_info(dev, "\tboost_idx = %d\n", key->boost_idx);
	dev_info(dev, "\talu_reg = 0x%04x\n", key->alu_reg);
}

static void ice_pg_cam_action_dump(struct ice_hw *hw,
				   struct ice_pg_cam_action *action)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "action:\n");
	dev_info(dev, "\tnext_node = %d\n", action->next_node);
	dev_info(dev, "\tnext_pc = %d\n", action->next_pc);
	dev_info(dev, "\tis_pg = %d\n", action->is_pg);
	dev_info(dev, "\tproto_id = %d\n", action->proto_id);
	dev_info(dev, "\tis_mg = %d\n", action->is_mg);
	dev_info(dev, "\tmarker_id = %d\n", action->marker_id);
	dev_info(dev, "\tis_last_round = %d\n", action->is_last_round);
	dev_info(dev, "\tho_polarity = %d\n", action->ho_polarity);
	dev_info(dev, "\tho_inc = %d\n", action->ho_inc);
}

/**
 * ice_pg_cam_dump - dump an parse graph cam info
 * @hw: pointer to the hardware structure
 * @item: parse graph cam to dump
 */
static void ice_pg_cam_dump(struct ice_hw *hw, struct ice_pg_cam_item *item)
{
	dev_info(ice_hw_to_dev(hw), "index = %d\n", item->idx);
	ice_pg_cam_key_dump(hw, &item->key);
	ice_pg_cam_action_dump(hw, &item->action);
}

/**
 * ice_pg_nm_cam_dump - dump an parse graph no match cam info
 * @hw: pointer to the hardware structure
 * @item: parse graph no match cam to dump
 */
static void ice_pg_nm_cam_dump(struct ice_hw *hw,
			       struct ice_pg_nm_cam_item *item)
{
	dev_info(ice_hw_to_dev(hw), "index = %d\n", item->idx);
	ice_pg_nm_cam_key_dump(hw, &item->key);
	ice_pg_cam_action_dump(hw, &item->action);
}

#define ICE_PGCA_NN	GENMASK_ULL(10, 0)
#define ICE_PGCA_NPC	GENMASK_ULL(18, 11)
#define ICE_PGCA_IPG	BIT_ULL(19)
#define ICE_PGCA_PID	GENMASK_ULL(30, 23)
#define ICE_PGCA_IMG	BIT_ULL(31)
#define ICE_PGCA_MID	GENMASK_ULL(39, 32)
#define ICE_PGCA_ILR	BIT_ULL(40)
#define ICE_PGCA_HOP	BIT_ULL(41)
#define ICE_PGCA_HOI	GENMASK_ULL(50, 42)

/**
 * ice_pg_cam_action_init - parse 55 bits of Parse Graph CAM Action
 * @action: pointer to the Parse Graph CAM Action structure
 * @data: Parse Graph CAM Action data to be parsed
 */
static void ice_pg_cam_action_init(struct ice_pg_cam_action *action, u64 data)
{
	action->next_node	= FIELD_GET(ICE_PGCA_NN, data);
	action->next_pc		= FIELD_GET(ICE_PGCA_NPC, data);
	action->is_pg		= FIELD_GET(ICE_PGCA_IPG, data);
	action->proto_id	= FIELD_GET(ICE_PGCA_PID, data);
	action->is_mg		= FIELD_GET(ICE_PGCA_IMG, data);
	action->marker_id	= FIELD_GET(ICE_PGCA_MID, data);
	action->is_last_round	= FIELD_GET(ICE_PGCA_ILR, data);
	action->ho_polarity	= FIELD_GET(ICE_PGCA_HOP, data);
	action->ho_inc		= FIELD_GET(ICE_PGCA_HOI, data);
}

#define ICE_PGNCK_VLD		BIT_ULL(0)
#define ICE_PGNCK_NID		GENMASK_ULL(11, 1)
#define ICE_PGNCK_F0		BIT_ULL(12)
#define ICE_PGNCK_F1		BIT_ULL(13)
#define ICE_PGNCK_F2		BIT_ULL(14)
#define ICE_PGNCK_F3		BIT_ULL(15)
#define ICE_PGNCK_BH		BIT_ULL(16)
#define ICE_PGNCK_BI		GENMASK_ULL(24, 17)
#define ICE_PGNCK_AR		GENMASK_ULL(40, 25)

/**
 * ice_pg_nm_cam_key_init - parse 41 bits of Parse Graph NoMatch CAM Key
 * @key: pointer to the Parse Graph NoMatch CAM Key structure
 * @data: Parse Graph NoMatch CAM Key data to be parsed
 */
static void ice_pg_nm_cam_key_init(struct ice_pg_nm_cam_key *key, u64 data)
{
	key->valid	= FIELD_GET(ICE_PGNCK_VLD, data);
	key->node_id	= FIELD_GET(ICE_PGNCK_NID, data);
	key->flag0	= FIELD_GET(ICE_PGNCK_F0, data);
	key->flag1	= FIELD_GET(ICE_PGNCK_F1, data);
	key->flag2	= FIELD_GET(ICE_PGNCK_F2, data);
	key->flag3	= FIELD_GET(ICE_PGNCK_F3, data);

	if (FIELD_GET(ICE_PGNCK_BH, data))
		key->boost_idx = FIELD_GET(ICE_PGNCK_BI, data);
	else
		key->boost_idx = 0;

	key->alu_reg	= FIELD_GET(ICE_PGNCK_AR, data);
}

#define ICE_PGCK_VLD		BIT_ULL(0)
#define ICE_PGCK_NID		GENMASK_ULL(11, 1)
#define ICE_PGCK_F0		BIT_ULL(12)
#define ICE_PGCK_F1		BIT_ULL(13)
#define ICE_PGCK_F2		BIT_ULL(14)
#define ICE_PGCK_F3		BIT_ULL(15)
#define ICE_PGCK_BH		BIT_ULL(16)
#define ICE_PGCK_BI		GENMASK_ULL(24, 17)
#define ICE_PGCK_AR		GENMASK_ULL(40, 25)
#define ICE_PGCK_NPK_S		41	/* offset for the 2nd 64-bits field */
#define ICE_PGCK_NPK_IDD	(ICE_PGCK_NPK_S / BITS_PER_BYTE)
#define ICE_PGCK_NPK_OFF	(ICE_PGCK_NPK_S % BITS_PER_BYTE)
#define ICE_PGCK_NPK		GENMASK_ULL(72 - ICE_PGCK_NPK_S, \
					    41 - ICE_PGCK_NPK_S)

/**
 * ice_pg_cam_key_init - parse 73 bits of Parse Graph CAM Key
 * @key: pointer to the Parse Graph CAM Key structure
 * @data: Parse Graph CAM Key data to be parsed
 */
static void ice_pg_cam_key_init(struct ice_pg_cam_key *key, u8 *data)
{
	u64 d64 = *(u64 *)data;

	key->valid	= FIELD_GET(ICE_PGCK_VLD, d64);
	key->node_id	= FIELD_GET(ICE_PGCK_NID, d64);
	key->flag0	= FIELD_GET(ICE_PGCK_F0, d64);
	key->flag1	= FIELD_GET(ICE_PGCK_F1, d64);
	key->flag2	= FIELD_GET(ICE_PGCK_F2, d64);
	key->flag3	= FIELD_GET(ICE_PGCK_F3, d64);

	if (FIELD_GET(ICE_PGCK_BH, d64))
		key->boost_idx = FIELD_GET(ICE_PGCK_BI, d64);
	else
		key->boost_idx = 0;

	key->alu_reg	= FIELD_GET(ICE_PGCK_AR, d64);

	d64 = *((u64 *)&data[ICE_PGCK_NPK_IDD]) >> ICE_PGCK_NPK_OFF;

	key->next_proto	= FIELD_GET(ICE_PGCK_NPK, d64);
}

#define ICE_PG_CAM_ACT_S	73
#define ICE_PG_CAM_ACT_IDD	(ICE_PG_CAM_ACT_S / BITS_PER_BYTE)
#define ICE_PG_CAM_ACT_OFF	(ICE_PG_CAM_ACT_S % BITS_PER_BYTE)

/**
 * ice_pg_cam_parse_item - parse 128 bits of Parse Graph CAM Entry
 * @hw: pointer to the hardware structure
 * @idx: index of Parse Graph CAM Entry
 * @item: item of Parse Graph CAM Entry
 * @data: Parse Graph CAM Entry data to be parsed
 * @size: size of Parse Graph CAM Entry
 */
static void ice_pg_cam_parse_item(struct ice_hw *hw, u16 idx, void *item,
				  void *data, int __maybe_unused size)
{
	struct ice_pg_cam_item *ci = item;
	u8 *buf = data;
	u64 d64;

	ci->idx = idx;

	ice_pg_cam_key_init(&ci->key, buf);

	d64 = *((u64 *)&buf[ICE_PG_CAM_ACT_IDD]) >> ICE_PG_CAM_ACT_OFF;
	ice_pg_cam_action_init(&ci->action, d64);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_pg_cam_dump(hw, ci);
}

#define ICE_PG_SP_CAM_KEY_S	56
#define ICE_PG_SP_CAM_KEY_IDD	(ICE_PG_SP_CAM_KEY_S / BITS_PER_BYTE)

/**
 * ice_pg_sp_cam_parse_item - parse 136 bits of Parse Graph Spill CAM Entry
 * @hw: pointer to the hardware structure
 * @idx: index of Parse Graph Spill CAM Entry
 * @item: item of Parse Graph Spill CAM Entry
 * @data: Parse Graph Spill CAM Entry data to be parsed
 * @size: size of Parse Graph Spill CAM Entry
 */
static void ice_pg_sp_cam_parse_item(struct ice_hw *hw, u16 idx, void *item,
				     void *data, int __maybe_unused size)
{
	struct ice_pg_cam_item *ci = item;
	u8 *buf = data;
	u64 d64;

	ci->idx = idx;

	d64 = *(u64 *)buf;
	ice_pg_cam_action_init(&ci->action, d64);

	ice_pg_cam_key_init(&ci->key, &buf[ICE_PG_SP_CAM_KEY_IDD]);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_pg_cam_dump(hw, ci);
}

#define ICE_PG_NM_CAM_ACT_S	41
#define ICE_PG_NM_CAM_ACT_IDD	(ICE_PG_NM_CAM_ACT_S / BITS_PER_BYTE)
#define ICE_PG_NM_CAM_ACT_OFF   (ICE_PG_NM_CAM_ACT_S % BITS_PER_BYTE)

/**
 * ice_pg_nm_cam_parse_item - parse 96 bits of Parse Graph NoMatch CAM Entry
 * @hw: pointer to the hardware structure
 * @idx: index of Parse Graph NoMatch CAM Entry
 * @item: item of Parse Graph NoMatch CAM Entry
 * @data: Parse Graph NoMatch CAM Entry data to be parsed
 * @size: size of Parse Graph NoMatch CAM Entry
 */
static void ice_pg_nm_cam_parse_item(struct ice_hw *hw, u16 idx, void *item,
				     void *data, int __maybe_unused size)
{
	struct ice_pg_nm_cam_item *ci = item;
	u8 *buf = data;
	u64 d64;

	ci->idx = idx;

	d64 = *(u64 *)buf;
	ice_pg_nm_cam_key_init(&ci->key, d64);

	d64 = *((u64 *)&buf[ICE_PG_NM_CAM_ACT_IDD]) >> ICE_PG_NM_CAM_ACT_OFF;
	ice_pg_cam_action_init(&ci->action, d64);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_pg_nm_cam_dump(hw, ci);
}

#define ICE_PG_NM_SP_CAM_ACT_S		56
#define ICE_PG_NM_SP_CAM_ACT_IDD	(ICE_PG_NM_SP_CAM_ACT_S / BITS_PER_BYTE)
#define ICE_PG_NM_SP_CAM_ACT_OFF	(ICE_PG_NM_SP_CAM_ACT_S % BITS_PER_BYTE)

/**
 * ice_pg_nm_sp_cam_parse_item - parse 104 bits of Parse Graph NoMatch Spill
 *  CAM Entry
 * @hw: pointer to the hardware structure
 * @idx: index of Parse Graph NoMatch Spill CAM Entry
 * @item: item of Parse Graph NoMatch Spill CAM Entry
 * @data: Parse Graph NoMatch Spill CAM Entry data to be parsed
 * @size: size of Parse Graph NoMatch Spill CAM Entry
 */
static void ice_pg_nm_sp_cam_parse_item(struct ice_hw *hw, u16 idx,
					void *item, void *data,
					int __maybe_unused size)
{
	struct ice_pg_nm_cam_item *ci = item;
	u8 *buf = data;
	u64 d64;

	ci->idx = idx;

	d64 = *(u64 *)buf;
	ice_pg_cam_action_init(&ci->action, d64);

	d64 = *((u64 *)&buf[ICE_PG_NM_SP_CAM_ACT_IDD]) >>
		ICE_PG_NM_SP_CAM_ACT_OFF;
	ice_pg_nm_cam_key_init(&ci->key, d64);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_pg_nm_cam_dump(hw, ci);
}

/**
 * ice_pg_cam_table_get - create a parse graph cam table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Parse Graph CAM table.
 */
static struct ice_pg_cam_item *ice_pg_cam_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_CAM,
				       sizeof(struct ice_pg_cam_item),
				       ICE_PG_CAM_TABLE_SIZE,
				       ice_pg_cam_parse_item, false);
}

/**
 * ice_pg_sp_cam_table_get - create a parse graph spill cam table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Parse Graph Spill CAM table.
 */
static struct ice_pg_cam_item *ice_pg_sp_cam_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_PG_SPILL,
				       sizeof(struct ice_pg_cam_item),
				       ICE_PG_SP_CAM_TABLE_SIZE,
				       ice_pg_sp_cam_parse_item, false);
}

/**
 * ice_pg_nm_cam_table_get - create a parse graph no match cam table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Parse Graph No Match CAM table.
 */
static struct ice_pg_nm_cam_item *ice_pg_nm_cam_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_NOMATCH_CAM,
				       sizeof(struct ice_pg_nm_cam_item),
				       ICE_PG_NM_CAM_TABLE_SIZE,
				       ice_pg_nm_cam_parse_item, false);
}

/**
 * ice_pg_nm_sp_cam_table_get - create a parse graph no match spill cam table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Parse Graph No Match Spill CAM table.
 */
static struct ice_pg_nm_cam_item *ice_pg_nm_sp_cam_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_NOMATCH_SPILL,
				       sizeof(struct ice_pg_nm_cam_item),
				       ICE_PG_NM_SP_CAM_TABLE_SIZE,
				       ice_pg_nm_sp_cam_parse_item, false);
}

static bool __ice_pg_cam_match(struct ice_pg_cam_item *item,
			       struct ice_pg_cam_key *key)
{
	return (item->key.valid &&
		!memcmp(&item->key.val, &key->val, sizeof(key->val)));
}

static bool __ice_pg_nm_cam_match(struct ice_pg_nm_cam_item *item,
				  struct ice_pg_cam_key *key)
{
	return (item->key.valid &&
		!memcmp(&item->key.val, &key->val, sizeof(item->key.val)));
}

/**
 * ice_pg_cam_match - search parse graph cam table by key
 * @table: parse graph cam table to search
 * @size: cam table size
 * @key: search key
 *
 * Return: a pointer to the matching PG CAM item or NULL.
 */
struct ice_pg_cam_item *ice_pg_cam_match(struct ice_pg_cam_item *table,
					 int size, struct ice_pg_cam_key *key)
{
	int i;

	for (i = 0; i < size; i++) {
		struct ice_pg_cam_item *item = &table[i];

		if (__ice_pg_cam_match(item, key))
			return item;
	}

	return NULL;
}

/**
 * ice_pg_nm_cam_match - search parse graph no match cam table by key
 * @table: parse graph no match cam table to search
 * @size: cam table size
 * @key: search key
 *
 * Return: a pointer to the matching PG No Match CAM item or NULL.
 */
struct ice_pg_nm_cam_item *
ice_pg_nm_cam_match(struct ice_pg_nm_cam_item *table, int size,
		    struct ice_pg_cam_key *key)
{
	int i;

	for (i = 0; i < size; i++) {
		struct ice_pg_nm_cam_item *item = &table[i];

		if (__ice_pg_nm_cam_match(item, key))
			return item;
	}

	return NULL;
}

/*** Ternary match ***/
/* Perform a ternary match on a 1-byte pattern (@pat) given @key and @key_inv
 * Rules (per bit):
 *     Key == 0 and Key_inv == 0 : Never match (Don't care)
 *     Key == 0 and Key_inv == 1 : Match on bit == 1
 *     Key == 1 and Key_inv == 0 : Match on bit == 0
 *     Key == 1 and Key_inv == 1 : Always match (Don't care)
 *
 * Return: true if all bits match, false otherwise.
 */
static bool ice_ternary_match_byte(u8 key, u8 key_inv, u8 pat)
{
	u8 bit_key, bit_key_inv, bit_pat;
	int i;

	for (i = 0; i < BITS_PER_BYTE; i++) {
		bit_key = key & BIT(i);
		bit_key_inv = key_inv & BIT(i);
		bit_pat = pat & BIT(i);

		if (bit_key != 0 && bit_key_inv != 0)
			continue;

		if ((bit_key == 0 && bit_key_inv == 0) || bit_key == bit_pat)
			return false;
	}

	return true;
}

static bool ice_ternary_match(const u8 *key, const u8 *key_inv,
			      const u8 *pat, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (!ice_ternary_match_byte(key[i], key_inv[i], pat[i]))
			return false;

	return true;
}

/*** ICE_SID_RXPARSER_BOOST_TCAM and ICE_SID_LBL_RXPARSER_TMEM sections ***/
static void ice_bst_np_kb_dump(struct ice_hw *hw, struct ice_np_keybuilder *kb)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "next proto key builder:\n");
	dev_info(dev, "\topc = %d\n", kb->opc);
	dev_info(dev, "\tstart_reg0 = %d\n", kb->start_reg0);
	dev_info(dev, "\tlen_reg1 = %d\n", kb->len_reg1);
}

static void ice_bst_pg_kb_dump(struct ice_hw *hw, struct ice_pg_keybuilder *kb)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "parse graph key builder:\n");
	dev_info(dev, "\tflag0_ena = %d\n", kb->flag0_ena);
	dev_info(dev, "\tflag1_ena = %d\n", kb->flag1_ena);
	dev_info(dev, "\tflag2_ena = %d\n", kb->flag2_ena);
	dev_info(dev, "\tflag3_ena = %d\n", kb->flag3_ena);
	dev_info(dev, "\tflag0_idx = %d\n", kb->flag0_idx);
	dev_info(dev, "\tflag1_idx = %d\n", kb->flag1_idx);
	dev_info(dev, "\tflag2_idx = %d\n", kb->flag2_idx);
	dev_info(dev, "\tflag3_idx = %d\n", kb->flag3_idx);
	dev_info(dev, "\talu_reg_idx = %d\n", kb->alu_reg_idx);
}

static void ice_bst_alu_dump(struct ice_hw *hw, struct ice_alu *alu, int idx)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "alu%d:\n", idx);
	dev_info(dev, "\topc = %d\n", alu->opc);
	dev_info(dev, "\tsrc_start = %d\n", alu->src_start);
	dev_info(dev, "\tsrc_len = %d\n", alu->src_len);
	dev_info(dev, "\tshift_xlate_sel = %d\n", alu->shift_xlate_sel);
	dev_info(dev, "\tshift_xlate_key = %d\n", alu->shift_xlate_key);
	dev_info(dev, "\tsrc_reg_id = %d\n", alu->src_reg_id);
	dev_info(dev, "\tdst_reg_id = %d\n", alu->dst_reg_id);
	dev_info(dev, "\tinc0 = %d\n", alu->inc0);
	dev_info(dev, "\tinc1 = %d\n", alu->inc1);
	dev_info(dev, "\tproto_offset_opc = %d\n", alu->proto_offset_opc);
	dev_info(dev, "\tproto_offset = %d\n", alu->proto_offset);
	dev_info(dev, "\tbranch_addr = %d\n", alu->branch_addr);
	dev_info(dev, "\timm = %d\n", alu->imm);
	dev_info(dev, "\tdst_start = %d\n", alu->dst_start);
	dev_info(dev, "\tdst_len = %d\n", alu->dst_len);
	dev_info(dev, "\tflags_extr_imm = %d\n", alu->flags_extr_imm);
	dev_info(dev, "\tflags_start_imm= %d\n", alu->flags_start_imm);
}

/**
 * ice_bst_tcam_dump - dump a boost tcam info
 * @hw: pointer to the hardware structure
 * @item: boost tcam to dump
 */
static void ice_bst_tcam_dump(struct ice_hw *hw, struct ice_bst_tcam_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);
	int i;

	dev_info(dev, "addr = %d\n", item->addr);

	dev_info(dev, "key    : ");
	for (i = 0; i < ICE_BST_TCAM_KEY_SIZE; i++)
		dev_info(dev, "%02x ", item->key[i]);

	dev_info(dev, "\n");

	dev_info(dev, "key_inv: ");
	for (i = 0; i < ICE_BST_TCAM_KEY_SIZE; i++)
		dev_info(dev, "%02x ", item->key_inv[i]);

	dev_info(dev, "\n");

	dev_info(dev, "hit_idx_grp = %d\n", item->hit_idx_grp);
	dev_info(dev, "pg_prio = %d\n", item->pg_prio);

	ice_bst_np_kb_dump(hw, &item->np_kb);
	ice_bst_pg_kb_dump(hw, &item->pg_kb);

	ice_bst_alu_dump(hw, &item->alu0, ICE_ALU0_IDX);
	ice_bst_alu_dump(hw, &item->alu1, ICE_ALU1_IDX);
	ice_bst_alu_dump(hw, &item->alu2, ICE_ALU2_IDX);
}

static void ice_lbl_dump(struct ice_hw *hw, struct ice_lbl_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "index = %u\n", item->idx);
	dev_info(dev, "type = %u\n", item->type);
	dev_info(dev, "label = %s\n", item->label);
}

#define ICE_BST_ALU_OPC		GENMASK_ULL(5, 0)
#define ICE_BST_ALU_SS		GENMASK_ULL(13, 6)
#define ICE_BST_ALU_SL		GENMASK_ULL(18, 14)
#define ICE_BST_ALU_SXS		BIT_ULL(19)
#define ICE_BST_ALU_SXK		GENMASK_ULL(23, 20)
#define ICE_BST_ALU_SRID	GENMASK_ULL(30, 24)
#define ICE_BST_ALU_DRID	GENMASK_ULL(37, 31)
#define ICE_BST_ALU_INC0	BIT_ULL(38)
#define ICE_BST_ALU_INC1	BIT_ULL(39)
#define ICE_BST_ALU_POO		GENMASK_ULL(41, 40)
#define ICE_BST_ALU_PO		GENMASK_ULL(49, 42)
#define ICE_BST_ALU_BA_S	50	/* offset for the 2nd 64-bits field */
#define ICE_BST_ALU_BA		GENMASK_ULL(57 - ICE_BST_ALU_BA_S, \
					    50 - ICE_BST_ALU_BA_S)
#define ICE_BST_ALU_IMM		GENMASK_ULL(73 - ICE_BST_ALU_BA_S, \
					    58 - ICE_BST_ALU_BA_S)
#define ICE_BST_ALU_DFE		BIT_ULL(74 - ICE_BST_ALU_BA_S)
#define ICE_BST_ALU_DS		GENMASK_ULL(80 - ICE_BST_ALU_BA_S, \
					    75 - ICE_BST_ALU_BA_S)
#define ICE_BST_ALU_DL		GENMASK_ULL(86 - ICE_BST_ALU_BA_S, \
					    81 - ICE_BST_ALU_BA_S)
#define ICE_BST_ALU_FEI		BIT_ULL(87 - ICE_BST_ALU_BA_S)
#define ICE_BST_ALU_FSI		GENMASK_ULL(95 - ICE_BST_ALU_BA_S, \
					    88 - ICE_BST_ALU_BA_S)

/**
 * ice_bst_alu_init - parse 96 bits of ALU entry
 * @alu: pointer to the ALU entry structure
 * @data: ALU entry data to be parsed
 * @off: offset of the ALU entry data
 */
static void ice_bst_alu_init(struct ice_alu *alu, u8 *data, u8 off)
{
	u64 d64;
	u8 idd;

	d64 = *((u64 *)data) >> off;

	alu->opc		= FIELD_GET(ICE_BST_ALU_OPC, d64);
	alu->src_start		= FIELD_GET(ICE_BST_ALU_SS, d64);
	alu->src_len		= FIELD_GET(ICE_BST_ALU_SL, d64);
	alu->shift_xlate_sel	= FIELD_GET(ICE_BST_ALU_SXS, d64);
	alu->shift_xlate_key	= FIELD_GET(ICE_BST_ALU_SXK, d64);
	alu->src_reg_id		= FIELD_GET(ICE_BST_ALU_SRID, d64);
	alu->dst_reg_id		= FIELD_GET(ICE_BST_ALU_DRID, d64);
	alu->inc0		= FIELD_GET(ICE_BST_ALU_INC0, d64);
	alu->inc1		= FIELD_GET(ICE_BST_ALU_INC1, d64);
	alu->proto_offset_opc	= FIELD_GET(ICE_BST_ALU_POO, d64);
	alu->proto_offset	= FIELD_GET(ICE_BST_ALU_PO, d64);

	idd = (ICE_BST_ALU_BA_S + off) / BITS_PER_BYTE;
	off = (ICE_BST_ALU_BA_S + off) % BITS_PER_BYTE;
	d64 = *((u64 *)(&data[idd])) >> off;

	alu->branch_addr	= FIELD_GET(ICE_BST_ALU_BA, d64);
	alu->imm		= FIELD_GET(ICE_BST_ALU_IMM, d64);
	alu->dedicate_flags_ena	= FIELD_GET(ICE_BST_ALU_DFE, d64);
	alu->dst_start		= FIELD_GET(ICE_BST_ALU_DS, d64);
	alu->dst_len		= FIELD_GET(ICE_BST_ALU_DL, d64);
	alu->flags_extr_imm	= FIELD_GET(ICE_BST_ALU_FEI, d64);
	alu->flags_start_imm	= FIELD_GET(ICE_BST_ALU_FSI, d64);
}

#define ICE_BST_PGKB_F0_ENA		BIT_ULL(0)
#define ICE_BST_PGKB_F0_IDX		GENMASK_ULL(6, 1)
#define ICE_BST_PGKB_F1_ENA		BIT_ULL(7)
#define ICE_BST_PGKB_F1_IDX		GENMASK_ULL(13, 8)
#define ICE_BST_PGKB_F2_ENA		BIT_ULL(14)
#define ICE_BST_PGKB_F2_IDX		GENMASK_ULL(20, 15)
#define ICE_BST_PGKB_F3_ENA		BIT_ULL(21)
#define ICE_BST_PGKB_F3_IDX		GENMASK_ULL(27, 22)
#define ICE_BST_PGKB_AR_IDX		GENMASK_ULL(34, 28)

/**
 * ice_bst_pgkb_init - parse 35 bits of Parse Graph Key Build
 * @kb: pointer to the Parse Graph Key Build structure
 * @data: Parse Graph Key Build data to be parsed
 */
static void ice_bst_pgkb_init(struct ice_pg_keybuilder *kb, u64 data)
{
	kb->flag0_ena	= FIELD_GET(ICE_BST_PGKB_F0_ENA, data);
	kb->flag0_idx	= FIELD_GET(ICE_BST_PGKB_F0_IDX, data);
	kb->flag1_ena	= FIELD_GET(ICE_BST_PGKB_F1_ENA, data);
	kb->flag1_idx	= FIELD_GET(ICE_BST_PGKB_F1_IDX, data);
	kb->flag2_ena	= FIELD_GET(ICE_BST_PGKB_F2_ENA, data);
	kb->flag2_idx	= FIELD_GET(ICE_BST_PGKB_F2_IDX, data);
	kb->flag3_ena	= FIELD_GET(ICE_BST_PGKB_F3_ENA, data);
	kb->flag3_idx	= FIELD_GET(ICE_BST_PGKB_F3_IDX, data);
	kb->alu_reg_idx	= FIELD_GET(ICE_BST_PGKB_AR_IDX, data);
}

#define ICE_BST_NPKB_OPC	GENMASK(1, 0)
#define ICE_BST_NPKB_S_R0	GENMASK(9, 2)
#define ICE_BST_NPKB_L_R1	GENMASK(17, 10)

/**
 * ice_bst_npkb_init - parse 18 bits of Next Protocol Key Build
 * @kb: pointer to the Next Protocol Key Build structure
 * @data: Next Protocol Key Build data to be parsed
 */
static void ice_bst_npkb_init(struct ice_np_keybuilder *kb, u32 data)
{
	kb->opc		= FIELD_GET(ICE_BST_NPKB_OPC, data);
	kb->start_reg0	= FIELD_GET(ICE_BST_NPKB_S_R0, data);
	kb->len_reg1	= FIELD_GET(ICE_BST_NPKB_L_R1, data);
}

#define ICE_BT_KEY_S		32
#define ICE_BT_KEY_IDD		(ICE_BT_KEY_S / BITS_PER_BYTE)
#define ICE_BT_KIV_S		192
#define ICE_BT_KIV_IDD		(ICE_BT_KIV_S / BITS_PER_BYTE)
#define ICE_BT_HIG_S		352
#define ICE_BT_HIG_IDD		(ICE_BT_HIG_S / BITS_PER_BYTE)
#define ICE_BT_PGP_S		360
#define ICE_BT_PGP_IDD		(ICE_BT_PGP_S / BITS_PER_BYTE)
#define ICE_BT_PGP_M		GENMASK(361 - ICE_BT_PGP_S, 360 - ICE_BT_PGP_S)
#define ICE_BT_NPKB_S		362
#define ICE_BT_NPKB_IDD		(ICE_BT_NPKB_S / BITS_PER_BYTE)
#define ICE_BT_NPKB_OFF		(ICE_BT_NPKB_S % BITS_PER_BYTE)
#define ICE_BT_PGKB_S		380
#define ICE_BT_PGKB_IDD		(ICE_BT_PGKB_S / BITS_PER_BYTE)
#define ICE_BT_PGKB_OFF		(ICE_BT_PGKB_S % BITS_PER_BYTE)
#define ICE_BT_ALU0_S		415
#define ICE_BT_ALU0_IDD		(ICE_BT_ALU0_S / BITS_PER_BYTE)
#define ICE_BT_ALU0_OFF		(ICE_BT_ALU0_S % BITS_PER_BYTE)
#define ICE_BT_ALU1_S		511
#define ICE_BT_ALU1_IDD		(ICE_BT_ALU1_S / BITS_PER_BYTE)
#define ICE_BT_ALU1_OFF		(ICE_BT_ALU1_S % BITS_PER_BYTE)
#define ICE_BT_ALU2_S		607
#define ICE_BT_ALU2_IDD		(ICE_BT_ALU2_S / BITS_PER_BYTE)
#define ICE_BT_ALU2_OFF		(ICE_BT_ALU2_S % BITS_PER_BYTE)

/**
 * ice_bst_parse_item - parse 704 bits of Boost TCAM entry
 * @hw: pointer to the hardware structure
 * @idx: index of Boost TCAM entry
 * @item: item of Boost TCAM entry
 * @data: Boost TCAM entry data to be parsed
 * @size: size of Boost TCAM entry
 */
static void ice_bst_parse_item(struct ice_hw *hw, u16 idx, void *item,
			       void *data, int __maybe_unused size)
{
	struct ice_bst_tcam_item *ti = item;
	u8 *buf = (u8 *)data;
	int i;

	ti->addr = *(u16 *)buf;

	for (i = 0; i < ICE_BST_TCAM_KEY_SIZE; i++) {
		ti->key[i] = buf[ICE_BT_KEY_IDD + i];
		ti->key_inv[i] = buf[ICE_BT_KIV_IDD + i];
	}
	ti->hit_idx_grp	= buf[ICE_BT_HIG_IDD];
	ti->pg_prio	= buf[ICE_BT_PGP_IDD] & ICE_BT_PGP_M;

	ice_bst_npkb_init(&ti->np_kb,
			  *((u32 *)(&buf[ICE_BT_NPKB_IDD])) >>
			   ICE_BT_NPKB_OFF);
	ice_bst_pgkb_init(&ti->pg_kb,
			  *((u64 *)(&buf[ICE_BT_PGKB_IDD])) >>
			   ICE_BT_PGKB_OFF);

	ice_bst_alu_init(&ti->alu0, &buf[ICE_BT_ALU0_IDD], ICE_BT_ALU0_OFF);
	ice_bst_alu_init(&ti->alu1, &buf[ICE_BT_ALU1_IDD], ICE_BT_ALU1_OFF);
	ice_bst_alu_init(&ti->alu2, &buf[ICE_BT_ALU2_IDD], ICE_BT_ALU2_OFF);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_bst_tcam_dump(hw, ti);
}

/**
 * ice_bst_tcam_table_get - create a boost tcam table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Boost TCAM table.
 */
static struct ice_bst_tcam_item *ice_bst_tcam_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_BOOST_TCAM,
				       sizeof(struct ice_bst_tcam_item),
				       ICE_BST_TCAM_TABLE_SIZE,
				       ice_bst_parse_item, true);
}

static void ice_parse_lbl_item(struct ice_hw *hw, u16 idx, void *item,
			       void *data, int __maybe_unused size)
{
	struct ice_lbl_item *lbl_item = item;
	struct ice_lbl_item *lbl_data = data;

	lbl_item->idx = lbl_data->idx;
	memcpy(lbl_item->label, lbl_data->label, sizeof(lbl_item->label));

	if (strstarts(lbl_item->label, ICE_LBL_BST_DVM))
		lbl_item->type = ICE_LBL_BST_TYPE_DVM;
	else if (strstarts(lbl_item->label, ICE_LBL_BST_SVM))
		lbl_item->type = ICE_LBL_BST_TYPE_SVM;
	else if (strstarts(lbl_item->label, ICE_LBL_TNL_VXLAN))
		lbl_item->type = ICE_LBL_BST_TYPE_VXLAN;
	else if (strstarts(lbl_item->label, ICE_LBL_TNL_GENEVE))
		lbl_item->type = ICE_LBL_BST_TYPE_GENEVE;
	else if (strstarts(lbl_item->label, ICE_LBL_TNL_UDP_ECPRI))
		lbl_item->type = ICE_LBL_BST_TYPE_UDP_ECPRI;

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_lbl_dump(hw, lbl_item);
}

/**
 * ice_bst_lbl_table_get - create a boost label table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Boost label table.
 */
static struct ice_lbl_item *ice_bst_lbl_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_LBL_RXPARSER_TMEM,
				       sizeof(struct ice_lbl_item),
				       ICE_BST_TCAM_TABLE_SIZE,
				       ice_parse_lbl_item, true);
}

/**
 * ice_bst_tcam_match - match a pattern on the boost tcam table
 * @tcam_table: boost tcam table to search
 * @pat: pattern to match
 *
 * Return: a pointer to the matching Boost TCAM item or NULL.
 */
struct ice_bst_tcam_item *
ice_bst_tcam_match(struct ice_bst_tcam_item *tcam_table, u8 *pat)
{
	int i;

	for (i = 0; i < ICE_BST_TCAM_TABLE_SIZE; i++) {
		struct ice_bst_tcam_item *item = &tcam_table[i];

		if (item->hit_idx_grp == 0)
			continue;
		if (ice_ternary_match(item->key, item->key_inv, pat,
				      ICE_BST_TCAM_KEY_SIZE))
			return item;
	}

	return NULL;
}

/*** ICE_SID_RXPARSER_MARKER_PTYPE section ***/
/**
 * ice_ptype_mk_tcam_dump - dump an ptype marker tcam info
 * @hw: pointer to the hardware structure
 * @item: ptype marker tcam to dump
 */
static void ice_ptype_mk_tcam_dump(struct ice_hw *hw,
				   struct ice_ptype_mk_tcam_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);
	int i;

	dev_info(dev, "address = %d\n", item->address);
	dev_info(dev, "ptype = %d\n", item->ptype);

	dev_info(dev, "key    :");
	for (i = 0; i < ICE_PTYPE_MK_TCAM_KEY_SIZE; i++)
		dev_info(dev, "%02x ", item->key[i]);

	dev_info(dev, "\n");

	dev_info(dev, "key_inv:");
	for (i = 0; i < ICE_PTYPE_MK_TCAM_KEY_SIZE; i++)
		dev_info(dev, "%02x ", item->key_inv[i]);

	dev_info(dev, "\n");
}

static void ice_parse_ptype_mk_tcam_item(struct ice_hw *hw, u16 idx,
					 void *item, void *data, int size)
{
	memcpy(item, data, size);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_ptype_mk_tcam_dump(hw,
				       (struct ice_ptype_mk_tcam_item *)item);
}

/**
 * ice_ptype_mk_tcam_table_get - create a ptype marker tcam table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Marker PType TCAM table.
 */
static
struct ice_ptype_mk_tcam_item *ice_ptype_mk_tcam_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_MARKER_PTYPE,
				       sizeof(struct ice_ptype_mk_tcam_item),
				       ICE_PTYPE_MK_TCAM_TABLE_SIZE,
				       ice_parse_ptype_mk_tcam_item, true);
}

/**
 * ice_ptype_mk_tcam_match - match a pattern on a ptype marker tcam table
 * @table: ptype marker tcam table to search
 * @pat: pattern to match
 * @len: length of the pattern
 *
 * Return: a pointer to the matching Marker PType item or NULL.
 */
struct ice_ptype_mk_tcam_item *
ice_ptype_mk_tcam_match(struct ice_ptype_mk_tcam_item *table,
			u8 *pat, int len)
{
	int i;

	for (i = 0; i < ICE_PTYPE_MK_TCAM_TABLE_SIZE; i++) {
		struct ice_ptype_mk_tcam_item *item = &table[i];

		if (ice_ternary_match(item->key, item->key_inv, pat, len))
			return item;
	}

	return NULL;
}

/*** ICE_SID_RXPARSER_MARKER_GRP section ***/
/**
 * ice_mk_grp_dump - dump an marker group item info
 * @hw: pointer to the hardware structure
 * @item: marker group item to dump
 */
static void ice_mk_grp_dump(struct ice_hw *hw, struct ice_mk_grp_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);
	int i;

	dev_info(dev, "index = %d\n", item->idx);

	dev_info(dev, "markers: ");
	for (i = 0; i < ICE_MK_COUNT_PER_GRP; i++)
		dev_info(dev, "%d ", item->markers[i]);

	dev_info(dev, "\n");
}

static void ice_mk_grp_parse_item(struct ice_hw *hw, u16 idx, void *item,
				  void *data, int __maybe_unused size)
{
	struct ice_mk_grp_item *grp = item;
	u8 *buf = data;
	int i;

	grp->idx = idx;

	for (i = 0; i < ICE_MK_COUNT_PER_GRP; i++)
		grp->markers[i] = buf[i];

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_mk_grp_dump(hw, grp);
}

/**
 * ice_mk_grp_table_get - create a marker group table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Marker Group ID table.
 */
static struct ice_mk_grp_item *ice_mk_grp_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_MARKER_GRP,
				       sizeof(struct ice_mk_grp_item),
				       ICE_MK_GRP_TABLE_SIZE,
				       ice_mk_grp_parse_item, false);
}

/*** ICE_SID_RXPARSER_PROTO_GRP section ***/
static void ice_proto_off_dump(struct ice_hw *hw,
			       struct ice_proto_off *po, int idx)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "proto %d\n", idx);
	dev_info(dev, "\tpolarity = %d\n", po->polarity);
	dev_info(dev, "\tproto_id = %d\n", po->proto_id);
	dev_info(dev, "\toffset = %d\n", po->offset);
}

/**
 * ice_proto_grp_dump - dump a proto group item info
 * @hw: pointer to the hardware structure
 * @item: proto group item to dump
 */
static void ice_proto_grp_dump(struct ice_hw *hw,
			       struct ice_proto_grp_item *item)
{
	int i;

	dev_info(ice_hw_to_dev(hw), "index = %d\n", item->idx);

	for (i = 0; i < ICE_PROTO_COUNT_PER_GRP; i++)
		ice_proto_off_dump(hw, &item->po[i], i);
}

#define ICE_PO_POL	BIT(0)
#define ICE_PO_PID	GENMASK(8, 1)
#define ICE_PO_OFF	GENMASK(21, 12)

/**
 * ice_proto_off_parse - parse 22 bits of Protocol entry
 * @po: pointer to the Protocol entry structure
 * @data: Protocol entry data to be parsed
 */
static void ice_proto_off_parse(struct ice_proto_off *po, u32 data)
{
	po->polarity = FIELD_GET(ICE_PO_POL, data);
	po->proto_id = FIELD_GET(ICE_PO_PID, data);
	po->offset = FIELD_GET(ICE_PO_OFF, data);
}

/**
 * ice_proto_grp_parse_item - parse 192 bits of Protocol Group Table entry
 * @hw: pointer to the hardware structure
 * @idx: index of Protocol Group Table entry
 * @item: item of Protocol Group Table entry
 * @data: Protocol Group Table entry data to be parsed
 * @size: size of Protocol Group Table entry
 */
static void ice_proto_grp_parse_item(struct ice_hw *hw, u16 idx, void *item,
				     void *data, int __maybe_unused size)
{
	struct ice_proto_grp_item *grp = item;
	u8 *buf = (u8 *)data;
	u8 idd, off;
	u32 d32;
	int i;

	grp->idx = idx;

	for (i = 0; i < ICE_PROTO_COUNT_PER_GRP; i++) {
		idd = (ICE_PROTO_GRP_ITEM_SIZE * i) / BITS_PER_BYTE;
		off = (ICE_PROTO_GRP_ITEM_SIZE * i) % BITS_PER_BYTE;
		d32 = *((u32 *)&buf[idd]) >> off;
		ice_proto_off_parse(&grp->po[i], d32);
	}

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_proto_grp_dump(hw, grp);
}

/**
 * ice_proto_grp_table_get - create a proto group table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Protocol Group table.
 */
static struct ice_proto_grp_item *ice_proto_grp_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_PROTO_GRP,
				       sizeof(struct ice_proto_grp_item),
				       ICE_PROTO_GRP_TABLE_SIZE,
				       ice_proto_grp_parse_item, false);
}

/*** ICE_SID_RXPARSER_FLAG_REDIR section ***/
/**
 * ice_flg_rd_dump - dump a flag redirect item info
 * @hw: pointer to the hardware structure
 * @item: flag redirect item to dump
 */
static void ice_flg_rd_dump(struct ice_hw *hw, struct ice_flg_rd_item *item)
{
	struct device *dev = ice_hw_to_dev(hw);

	dev_info(dev, "index = %d\n", item->idx);
	dev_info(dev, "expose = %d\n", item->expose);
	dev_info(dev, "intr_flg_id = %d\n", item->intr_flg_id);
}

#define ICE_FRT_EXPO	BIT(0)
#define ICE_FRT_IFID	GENMASK(6, 1)

/**
 * ice_flg_rd_parse_item - parse 8 bits of Flag Redirect Table entry
 * @hw: pointer to the hardware structure
 * @idx: index of Flag Redirect Table entry
 * @item: item of Flag Redirect Table entry
 * @data: Flag Redirect Table entry data to be parsed
 * @size: size of Flag Redirect Table entry
 */
static void ice_flg_rd_parse_item(struct ice_hw *hw, u16 idx, void *item,
				  void *data, int __maybe_unused size)
{
	struct ice_flg_rd_item *rdi = item;
	u8 d8 = *(u8 *)data;

	rdi->idx = idx;
	rdi->expose = FIELD_GET(ICE_FRT_EXPO, d8);
	rdi->intr_flg_id = FIELD_GET(ICE_FRT_IFID, d8);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_flg_rd_dump(hw, rdi);
}

/**
 * ice_flg_rd_table_get - create a flag redirect table
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Flags Redirection table.
 */
static struct ice_flg_rd_item *ice_flg_rd_table_get(struct ice_hw *hw)
{
	return ice_parser_create_table(hw, ICE_SID_RXPARSER_FLAG_REDIR,
				       sizeof(struct ice_flg_rd_item),
				       ICE_FLG_RD_TABLE_SIZE,
				       ice_flg_rd_parse_item, false);
}

/**
 * ice_flg_redirect - redirect a parser flag to packet flag
 * @table: flag redirect table
 * @psr_flg: parser flag to redirect
 *
 * Return: flag or 0 if @psr_flag = 0.
 */
u64 ice_flg_redirect(struct ice_flg_rd_item *table, u64 psr_flg)
{
	u64 flg = 0;
	int i;

	for (i = 0; i < ICE_FLG_RDT_SIZE; i++) {
		struct ice_flg_rd_item *item = &table[i];

		if (!item->expose)
			continue;

		if (psr_flg & BIT(item->intr_flg_id))
			flg |= BIT(i);
	}

	return flg;
}

/*** ICE_SID_XLT_KEY_BUILDER_SW, ICE_SID_XLT_KEY_BUILDER_ACL,
 * ICE_SID_XLT_KEY_BUILDER_FD and ICE_SID_XLT_KEY_BUILDER_RSS
 * sections ***/
static void ice_xlt_kb_entry_dump(struct ice_hw *hw,
				  struct ice_xlt_kb_entry *entry, int idx)
{
	struct device *dev = ice_hw_to_dev(hw);
	int i;

	dev_info(dev, "key builder entry %d\n", idx);
	dev_info(dev, "\txlt1_ad_sel = %d\n", entry->xlt1_ad_sel);
	dev_info(dev, "\txlt2_ad_sel = %d\n", entry->xlt2_ad_sel);

	for (i = 0; i < ICE_XLT_KB_FLAG0_14_CNT; i++)
		dev_info(dev, "\tflg%d_sel = %d\n", i, entry->flg0_14_sel[i]);

	dev_info(dev, "\txlt1_md_sel = %d\n", entry->xlt1_md_sel);
	dev_info(dev, "\txlt2_md_sel = %d\n", entry->xlt2_md_sel);
}

/**
 * ice_xlt_kb_dump - dump a xlt key build info
 * @hw: pointer to the hardware structure
 * @kb: key build to dump
 */
static void ice_xlt_kb_dump(struct ice_hw *hw, struct ice_xlt_kb *kb)
{
	struct device *dev = ice_hw_to_dev(hw);
	int i;

	dev_info(dev, "xlt1_pm = %d\n", kb->xlt1_pm);
	dev_info(dev, "xlt2_pm = %d\n", kb->xlt2_pm);
	dev_info(dev, "prof_id_pm = %d\n", kb->prof_id_pm);
	dev_info(dev, "flag15 lo = 0x%08x\n", (u32)kb->flag15);
	dev_info(dev, "flag15 hi = 0x%08x\n",
		 (u32)(kb->flag15 >> (sizeof(u32) * BITS_PER_BYTE)));

	for (i = 0; i < ICE_XLT_KB_TBL_CNT; i++)
		ice_xlt_kb_entry_dump(hw, &kb->entries[i], i);
}

#define ICE_XLT_KB_X1AS_S	32	/* offset for the 1st 64-bits field */
#define ICE_XLT_KB_X1AS_IDD	(ICE_XLT_KB_X1AS_S / BITS_PER_BYTE)
#define ICE_XLT_KB_X1AS_OFF	(ICE_XLT_KB_X1AS_S % BITS_PER_BYTE)
#define ICE_XLT_KB_X1AS		GENMASK_ULL(34 - ICE_XLT_KB_X1AS_S, \
					    32 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_X2AS		GENMASK_ULL(37 - ICE_XLT_KB_X1AS_S, \
					    35 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL00		GENMASK_ULL(46 - ICE_XLT_KB_X1AS_S, \
					    38 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL01		GENMASK_ULL(55 - ICE_XLT_KB_X1AS_S, \
					    47 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL02		GENMASK_ULL(64 - ICE_XLT_KB_X1AS_S, \
					    56 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL03		GENMASK_ULL(73 - ICE_XLT_KB_X1AS_S, \
					    65 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL04		GENMASK_ULL(82 - ICE_XLT_KB_X1AS_S, \
					    74 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL05		GENMASK_ULL(91 - ICE_XLT_KB_X1AS_S, \
					    83 - ICE_XLT_KB_X1AS_S)
#define ICE_XLT_KB_FL06_S	92	/* offset for the 2nd 64-bits field */
#define ICE_XLT_KB_FL06_IDD	(ICE_XLT_KB_FL06_S / BITS_PER_BYTE)
#define ICE_XLT_KB_FL06_OFF	(ICE_XLT_KB_FL06_S % BITS_PER_BYTE)
#define ICE_XLT_KB_FL06		GENMASK_ULL(100 - ICE_XLT_KB_FL06_S, \
					    92 - ICE_XLT_KB_FL06_S)
#define ICE_XLT_KB_FL07		GENMASK_ULL(109 - ICE_XLT_KB_FL06_S, \
					    101 - ICE_XLT_KB_FL06_S)
#define ICE_XLT_KB_FL08		GENMASK_ULL(118 - ICE_XLT_KB_FL06_S, \
					    110 - ICE_XLT_KB_FL06_S)
#define ICE_XLT_KB_FL09		GENMASK_ULL(127 - ICE_XLT_KB_FL06_S, \
					    119 - ICE_XLT_KB_FL06_S)
#define ICE_XLT_KB_FL10		GENMASK_ULL(136 - ICE_XLT_KB_FL06_S, \
					    128 - ICE_XLT_KB_FL06_S)
#define ICE_XLT_KB_FL11		GENMASK_ULL(145 - ICE_XLT_KB_FL06_S, \
					    137 - ICE_XLT_KB_FL06_S)
#define ICE_XLT_KB_FL12_S	146	/* offset for the 3rd 64-bits field */
#define ICE_XLT_KB_FL12_IDD	(ICE_XLT_KB_FL12_S / BITS_PER_BYTE)
#define ICE_XLT_KB_FL12_OFF	(ICE_XLT_KB_FL12_S % BITS_PER_BYTE)
#define ICE_XLT_KB_FL12		GENMASK_ULL(154 - ICE_XLT_KB_FL12_S, \
					    146 - ICE_XLT_KB_FL12_S)
#define ICE_XLT_KB_FL13		GENMASK_ULL(163 - ICE_XLT_KB_FL12_S, \
					    155 - ICE_XLT_KB_FL12_S)
#define ICE_XLT_KB_FL14		GENMASK_ULL(181 - ICE_XLT_KB_FL12_S, \
					    164 - ICE_XLT_KB_FL12_S)
#define ICE_XLT_KB_X1MS		GENMASK_ULL(186 - ICE_XLT_KB_FL12_S, \
					    182 - ICE_XLT_KB_FL12_S)
#define ICE_XLT_KB_X2MS		GENMASK_ULL(191 - ICE_XLT_KB_FL12_S, \
					    187 - ICE_XLT_KB_FL12_S)

/**
 * ice_kb_entry_init - parse 192 bits of XLT Key Builder entry
 * @entry: pointer to the XLT Key Builder entry structure
 * @data: XLT Key Builder entry data to be parsed
 */
static void ice_kb_entry_init(struct ice_xlt_kb_entry *entry, u8 *data)
{
	u8 i = 0;
	u64 d64;

	d64 = *((u64 *)&data[ICE_XLT_KB_X1AS_IDD]) >> ICE_XLT_KB_X1AS_OFF;

	entry->xlt1_ad_sel	= FIELD_GET(ICE_XLT_KB_X1AS, d64);
	entry->xlt2_ad_sel	= FIELD_GET(ICE_XLT_KB_X2AS, d64);

	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL00, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL01, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL02, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL03, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL04, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL05, d64);

	d64 = *((u64 *)&data[ICE_XLT_KB_FL06_IDD]) >> ICE_XLT_KB_FL06_OFF;

	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL06, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL07, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL08, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL09, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL10, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL11, d64);

	d64 = *((u64 *)&data[ICE_XLT_KB_FL12_IDD]) >> ICE_XLT_KB_FL12_OFF;

	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL12, d64);
	entry->flg0_14_sel[i++]	= FIELD_GET(ICE_XLT_KB_FL13, d64);
	entry->flg0_14_sel[i]	= FIELD_GET(ICE_XLT_KB_FL14, d64);

	entry->xlt1_md_sel	= FIELD_GET(ICE_XLT_KB_X1MS, d64);
	entry->xlt2_md_sel	= FIELD_GET(ICE_XLT_KB_X2MS, d64);
}

#define ICE_XLT_KB_X1PM_OFF	0
#define ICE_XLT_KB_X2PM_OFF	1
#define ICE_XLT_KB_PIPM_OFF	2
#define ICE_XLT_KB_FL15_OFF	4
#define ICE_XLT_KB_TBL_OFF	12

/**
 * ice_parse_kb_data - parse 204 bits of XLT Key Build Table
 * @hw: pointer to the hardware structure
 * @kb: pointer to the XLT Key Build Table structure
 * @data: XLT Key Build Table data to be parsed
 */
static void ice_parse_kb_data(struct ice_hw *hw, struct ice_xlt_kb *kb,
			      void *data)
{
	u8 *buf = data;
	int i;

	kb->xlt1_pm	= buf[ICE_XLT_KB_X1PM_OFF];
	kb->xlt2_pm	= buf[ICE_XLT_KB_X2PM_OFF];
	kb->prof_id_pm	= buf[ICE_XLT_KB_PIPM_OFF];

	kb->flag15 = *(u64 *)&buf[ICE_XLT_KB_FL15_OFF];
	for (i = 0; i < ICE_XLT_KB_TBL_CNT; i++)
		ice_kb_entry_init(&kb->entries[i],
				  &buf[ICE_XLT_KB_TBL_OFF +
				       i * ICE_XLT_KB_TBL_ENTRY_SIZE]);

	if (hw->debug_mask & ICE_DBG_PARSER)
		ice_xlt_kb_dump(hw, kb);
}

static struct ice_xlt_kb *ice_xlt_kb_get(struct ice_hw *hw, u32 sect_type)
{
	struct ice_pkg_enum state = {};
	struct ice_seg *seg = hw->seg;
	struct ice_xlt_kb *kb;
	void *data;

	if (!seg)
		return ERR_PTR(-EINVAL);

	kb = kzalloc(sizeof(*kb), GFP_KERNEL);
	if (!kb)
		return ERR_PTR(-ENOMEM);

	data = ice_pkg_enum_section(seg, &state, sect_type);
	if (!data) {
		ice_debug(hw, ICE_DBG_PARSER, "failed to find section type %d.\n",
			  sect_type);
		kfree(kb);
		return ERR_PTR(-EINVAL);
	}

	ice_parse_kb_data(hw, kb, data);

	return kb;
}

/**
 * ice_xlt_kb_get_sw - create switch xlt key build
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Key Builder table for Switch.
 */
static struct ice_xlt_kb *ice_xlt_kb_get_sw(struct ice_hw *hw)
{
	return ice_xlt_kb_get(hw, ICE_SID_XLT_KEY_BUILDER_SW);
}

/**
 * ice_xlt_kb_get_acl - create acl xlt key build
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Key Builder table for ACL.
 */
static struct ice_xlt_kb *ice_xlt_kb_get_acl(struct ice_hw *hw)
{
	return ice_xlt_kb_get(hw, ICE_SID_XLT_KEY_BUILDER_ACL);
}

/**
 * ice_xlt_kb_get_fd - create fdir xlt key build
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Key Builder table for Flow Director.
 */
static struct ice_xlt_kb *ice_xlt_kb_get_fd(struct ice_hw *hw)
{
	return ice_xlt_kb_get(hw, ICE_SID_XLT_KEY_BUILDER_FD);
}

/**
 * ice_xlt_kb_get_rss - create rss xlt key build
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated Key Builder table for RSS.
 */
static struct ice_xlt_kb *ice_xlt_kb_get_rss(struct ice_hw *hw)
{
	return ice_xlt_kb_get(hw, ICE_SID_XLT_KEY_BUILDER_RSS);
}

#define ICE_XLT_KB_MASK		GENMASK_ULL(5, 0)

/**
 * ice_xlt_kb_flag_get - aggregate 64 bits packet flag into 16 bits xlt flag
 * @kb: xlt key build
 * @pkt_flag: 64 bits packet flag
 *
 * Return: XLT flag or 0 if @pkt_flag = 0.
 */
u16 ice_xlt_kb_flag_get(struct ice_xlt_kb *kb, u64 pkt_flag)
{
	struct ice_xlt_kb_entry *entry = &kb->entries[0];
	u16 flag = 0;
	int i;

	/* check flag 15 */
	if (kb->flag15 & pkt_flag)
		flag = BIT(ICE_XLT_KB_FLAG0_14_CNT);

	/* check flag 0 - 14 */
	for (i = 0; i < ICE_XLT_KB_FLAG0_14_CNT; i++) {
		/* only check first entry */
		u16 idx = entry->flg0_14_sel[i] & ICE_XLT_KB_MASK;

		if (pkt_flag & BIT(idx))
			flag |= (u16)BIT(i);
	}

	return flag;
}

/*** Parser API ***/
/**
 * ice_parser_create - create a parser instance
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated parser instance or ERR_PTR
 * in case of error.
 */
struct ice_parser *ice_parser_create(struct ice_hw *hw)
{
	struct ice_parser *p;
	void *err;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	p->hw = hw;
	p->rt.psr = p;

	p->imem_table = ice_imem_table_get(hw);
	if (IS_ERR(p->imem_table)) {
		err = p->imem_table;
		goto err;
	}

	p->mi_table = ice_metainit_table_get(hw);
	if (IS_ERR(p->mi_table)) {
		err = p->mi_table;
		goto err;
	}

	p->pg_cam_table = ice_pg_cam_table_get(hw);
	if (IS_ERR(p->pg_cam_table)) {
		err = p->pg_cam_table;
		goto err;
	}

	p->pg_sp_cam_table = ice_pg_sp_cam_table_get(hw);
	if (IS_ERR(p->pg_sp_cam_table)) {
		err = p->pg_sp_cam_table;
		goto err;
	}

	p->pg_nm_cam_table = ice_pg_nm_cam_table_get(hw);
	if (IS_ERR(p->pg_nm_cam_table)) {
		err = p->pg_nm_cam_table;
		goto err;
	}

	p->pg_nm_sp_cam_table = ice_pg_nm_sp_cam_table_get(hw);
	if (IS_ERR(p->pg_nm_sp_cam_table)) {
		err = p->pg_nm_sp_cam_table;
		goto err;
	}

	p->bst_tcam_table = ice_bst_tcam_table_get(hw);
	if (IS_ERR(p->bst_tcam_table)) {
		err = p->bst_tcam_table;
		goto err;
	}

	p->bst_lbl_table = ice_bst_lbl_table_get(hw);
	if (IS_ERR(p->bst_lbl_table)) {
		err = p->bst_lbl_table;
		goto err;
	}

	p->ptype_mk_tcam_table = ice_ptype_mk_tcam_table_get(hw);
	if (IS_ERR(p->ptype_mk_tcam_table)) {
		err = p->ptype_mk_tcam_table;
		goto err;
	}

	p->mk_grp_table = ice_mk_grp_table_get(hw);
	if (IS_ERR(p->mk_grp_table)) {
		err = p->mk_grp_table;
		goto err;
	}

	p->proto_grp_table = ice_proto_grp_table_get(hw);
	if (IS_ERR(p->proto_grp_table)) {
		err = p->proto_grp_table;
		goto err;
	}

	p->flg_rd_table = ice_flg_rd_table_get(hw);
	if (IS_ERR(p->flg_rd_table)) {
		err = p->flg_rd_table;
		goto err;
	}

	p->xlt_kb_sw = ice_xlt_kb_get_sw(hw);
	if (IS_ERR(p->xlt_kb_sw)) {
		err = p->xlt_kb_sw;
		goto err;
	}

	p->xlt_kb_acl = ice_xlt_kb_get_acl(hw);
	if (IS_ERR(p->xlt_kb_acl)) {
		err = p->xlt_kb_acl;
		goto err;
	}

	p->xlt_kb_fd = ice_xlt_kb_get_fd(hw);
	if (IS_ERR(p->xlt_kb_fd)) {
		err = p->xlt_kb_fd;
		goto err;
	}

	p->xlt_kb_rss = ice_xlt_kb_get_rss(hw);
	if (IS_ERR(p->xlt_kb_rss)) {
		err = p->xlt_kb_rss;
		goto err;
	}

	return p;
err:
	ice_parser_destroy(p);
	return err;
}

/**
 * ice_parser_destroy - destroy a parser instance
 * @psr: pointer to a parser instance
 */
void ice_parser_destroy(struct ice_parser *psr)
{
	kfree(psr->imem_table);
	kfree(psr->mi_table);
	kfree(psr->pg_cam_table);
	kfree(psr->pg_sp_cam_table);
	kfree(psr->pg_nm_cam_table);
	kfree(psr->pg_nm_sp_cam_table);
	kfree(psr->bst_tcam_table);
	kfree(psr->bst_lbl_table);
	kfree(psr->ptype_mk_tcam_table);
	kfree(psr->mk_grp_table);
	kfree(psr->proto_grp_table);
	kfree(psr->flg_rd_table);
	kfree(psr->xlt_kb_sw);
	kfree(psr->xlt_kb_acl);
	kfree(psr->xlt_kb_fd);
	kfree(psr->xlt_kb_rss);

	kfree(psr);
}

/**
 * ice_parser_run - parse on a packet in binary and return the result
 * @psr: pointer to a parser instance
 * @pkt_buf: packet data
 * @pkt_len: packet length
 * @rslt: input/output parameter to save parser result.
 *
 * Return: 0 on success or errno.
 */
int ice_parser_run(struct ice_parser *psr, const u8 *pkt_buf,
		   int pkt_len, struct ice_parser_result *rslt)
{
	ice_parser_rt_reset(&psr->rt);
	ice_parser_rt_pktbuf_set(&psr->rt, pkt_buf, pkt_len);

	return ice_parser_rt_execute(&psr->rt, rslt);
}

/**
 * ice_parser_result_dump - dump a parser result info
 * @hw: pointer to the hardware structure
 * @rslt: parser result info to dump
 */
void ice_parser_result_dump(struct ice_hw *hw, struct ice_parser_result *rslt)
{
	struct device *dev = ice_hw_to_dev(hw);
	int i;

	dev_info(dev, "ptype = %d\n", rslt->ptype);
	for (i = 0; i < rslt->po_num; i++)
		dev_info(dev, "proto = %d, offset = %d\n",
			 rslt->po[i].proto_id, rslt->po[i].offset);

	dev_info(dev, "flags_psr = 0x%016llx\n", rslt->flags_psr);
	dev_info(dev, "flags_pkt = 0x%016llx\n", rslt->flags_pkt);
	dev_info(dev, "flags_sw = 0x%04x\n", rslt->flags_sw);
	dev_info(dev, "flags_fd = 0x%04x\n", rslt->flags_fd);
	dev_info(dev, "flags_rss = 0x%04x\n", rslt->flags_rss);
}

#define ICE_BT_VLD_KEY	0xFF
#define ICE_BT_INV_KEY	0xFE

static void ice_bst_dvm_set(struct ice_parser *psr, enum ice_lbl_type type,
			    bool on)
{
	u16 i = 0;

	while (true) {
		struct ice_bst_tcam_item *item;
		u8 key;

		item = ice_bst_tcam_search(psr->bst_tcam_table,
					   psr->bst_lbl_table,
					   type, &i);
		if (!item)
			break;

		key = on ? ICE_BT_VLD_KEY : ICE_BT_INV_KEY;
		item->key[ICE_BT_VM_OFF] = key;
		item->key_inv[ICE_BT_VM_OFF] = key;
		i++;
	}
}

/**
 * ice_parser_dvm_set - configure double vlan mode for parser
 * @psr: pointer to a parser instance
 * @on: true to turn on; false to turn off
 */
void ice_parser_dvm_set(struct ice_parser *psr, bool on)
{
	ice_bst_dvm_set(psr, ICE_LBL_BST_TYPE_DVM, on);
	ice_bst_dvm_set(psr, ICE_LBL_BST_TYPE_SVM, !on);
}

static int ice_tunnel_port_set(struct ice_parser *psr, enum ice_lbl_type type,
			       u16 udp_port, bool on)
{
	u8 *buf = (u8 *)&udp_port;
	u16 i = 0;

	while (true) {
		struct ice_bst_tcam_item *item;

		item = ice_bst_tcam_search(psr->bst_tcam_table,
					   psr->bst_lbl_table,
					   type, &i);
		if (!item)
			break;

		/* found empty slot to add */
		if (on && item->key[ICE_BT_TUN_PORT_OFF_H] == ICE_BT_INV_KEY &&
		    item->key_inv[ICE_BT_TUN_PORT_OFF_H] == ICE_BT_INV_KEY) {
			item->key_inv[ICE_BT_TUN_PORT_OFF_L] =
						buf[ICE_UDP_PORT_OFF_L];
			item->key_inv[ICE_BT_TUN_PORT_OFF_H] =
						buf[ICE_UDP_PORT_OFF_H];

			item->key[ICE_BT_TUN_PORT_OFF_L] =
				ICE_BT_VLD_KEY - buf[ICE_UDP_PORT_OFF_L];
			item->key[ICE_BT_TUN_PORT_OFF_H] =
				ICE_BT_VLD_KEY - buf[ICE_UDP_PORT_OFF_H];

			return 0;
		/* found a matched slot to delete */
		} else if (!on &&
			   (item->key_inv[ICE_BT_TUN_PORT_OFF_L] ==
				buf[ICE_UDP_PORT_OFF_L] ||
			    item->key_inv[ICE_BT_TUN_PORT_OFF_H] ==
				buf[ICE_UDP_PORT_OFF_H])) {
			item->key_inv[ICE_BT_TUN_PORT_OFF_L] = ICE_BT_VLD_KEY;
			item->key_inv[ICE_BT_TUN_PORT_OFF_H] = ICE_BT_INV_KEY;

			item->key[ICE_BT_TUN_PORT_OFF_L] = ICE_BT_VLD_KEY;
			item->key[ICE_BT_TUN_PORT_OFF_H] = ICE_BT_INV_KEY;

			return 0;
		}
		i++;
	}

	return -EINVAL;
}

/**
 * ice_parser_vxlan_tunnel_set - configure vxlan tunnel for parser
 * @psr: pointer to a parser instance
 * @udp_port: vxlan tunnel port in UDP header
 * @on: true to turn on; false to turn off
 *
 * Return: 0 on success or errno on failure.
 */
int ice_parser_vxlan_tunnel_set(struct ice_parser *psr,
				u16 udp_port, bool on)
{
	return ice_tunnel_port_set(psr, ICE_LBL_BST_TYPE_VXLAN, udp_port, on);
}

/**
 * ice_parser_geneve_tunnel_set - configure geneve tunnel for parser
 * @psr: pointer to a parser instance
 * @udp_port: geneve tunnel port in UDP header
 * @on: true to turn on; false to turn off
 *
 * Return: 0 on success or errno on failure.
 */
int ice_parser_geneve_tunnel_set(struct ice_parser *psr,
				 u16 udp_port, bool on)
{
	return ice_tunnel_port_set(psr, ICE_LBL_BST_TYPE_GENEVE, udp_port, on);
}

/**
 * ice_parser_ecpri_tunnel_set - configure ecpri tunnel for parser
 * @psr: pointer to a parser instance
 * @udp_port: ecpri tunnel port in UDP header
 * @on: true to turn on; false to turn off
 *
 * Return: 0 on success or errno on failure.
 */
int ice_parser_ecpri_tunnel_set(struct ice_parser *psr,
				u16 udp_port, bool on)
{
	return ice_tunnel_port_set(psr, ICE_LBL_BST_TYPE_UDP_ECPRI,
				   udp_port, on);
}

/**
 * ice_nearest_proto_id - find nearest protocol ID
 * @rslt: pointer to a parser result instance
 * @offset: a min value for the protocol offset
 * @proto_id: the protocol ID (output)
 * @proto_off: the protocol offset (output)
 *
 * From the protocols in @rslt, find the nearest protocol that has offset
 * larger than @offset.
 *
 * Return: if true, the protocol's ID and offset
 */
static bool ice_nearest_proto_id(struct ice_parser_result *rslt, u16 offset,
				 u8 *proto_id, u16 *proto_off)
{
	u16 dist = U16_MAX;
	u8 proto = 0;
	int i;

	for (i = 0; i < rslt->po_num; i++) {
		if (offset < rslt->po[i].offset)
			continue;
		if (offset - rslt->po[i].offset < dist) {
			proto = rslt->po[i].proto_id;
			dist = offset - rslt->po[i].offset;
		}
	}

	if (dist % 2)
		return false;

	*proto_id = proto;
	*proto_off = dist;

	return true;
}

/* default flag mask to cover GTP_EH_PDU, GTP_EH_PDU_LINK and TUN2
 * In future, the flag masks should learn from DDP
 */
#define ICE_KEYBUILD_FLAG_MASK_DEFAULT_SW	0x4002
#define ICE_KEYBUILD_FLAG_MASK_DEFAULT_ACL	0x0000
#define ICE_KEYBUILD_FLAG_MASK_DEFAULT_FD	0x6080
#define ICE_KEYBUILD_FLAG_MASK_DEFAULT_RSS	0x6010

/**
 * ice_parser_profile_init - initialize a FXP profile based on parser result
 * @rslt: a instance of a parser result
 * @pkt_buf: packet data buffer
 * @msk_buf: packet mask buffer
 * @buf_len: packet length
 * @blk: FXP pipeline stage
 * @prof: input/output parameter to save the profile
 *
 * Return: 0 on success or errno on failure.
 */
int ice_parser_profile_init(struct ice_parser_result *rslt,
			    const u8 *pkt_buf, const u8 *msk_buf,
			    int buf_len, enum ice_block blk,
			    struct ice_parser_profile *prof)
{
	u8 proto_id = U8_MAX;
	u16 proto_off = 0;
	u16 off;

	memset(prof, 0, sizeof(*prof));
	set_bit(rslt->ptype, prof->ptypes);
	if (blk == ICE_BLK_SW) {
		prof->flags	= rslt->flags_sw;
		prof->flags_msk	= ICE_KEYBUILD_FLAG_MASK_DEFAULT_SW;
	} else if (blk == ICE_BLK_ACL) {
		prof->flags	= rslt->flags_acl;
		prof->flags_msk	= ICE_KEYBUILD_FLAG_MASK_DEFAULT_ACL;
	} else if (blk == ICE_BLK_FD) {
		prof->flags	= rslt->flags_fd;
		prof->flags_msk	= ICE_KEYBUILD_FLAG_MASK_DEFAULT_FD;
	} else if (blk == ICE_BLK_RSS) {
		prof->flags	= rslt->flags_rss;
		prof->flags_msk	= ICE_KEYBUILD_FLAG_MASK_DEFAULT_RSS;
	} else {
		return -EINVAL;
	}

	for (off = 0; off < buf_len - 1; off++) {
		if (msk_buf[off] == 0 && msk_buf[off + 1] == 0)
			continue;
		if (!ice_nearest_proto_id(rslt, off, &proto_id, &proto_off))
			continue;
		if (prof->fv_num >= ICE_PARSER_FV_MAX)
			return -EINVAL;

		prof->fv[prof->fv_num].proto_id	= proto_id;
		prof->fv[prof->fv_num].offset	= proto_off;
		prof->fv[prof->fv_num].spec	= *(const u16 *)&pkt_buf[off];
		prof->fv[prof->fv_num].msk	= *(const u16 *)&msk_buf[off];
		prof->fv_num++;
	}

	return 0;
}

/**
 * ice_parser_profile_dump - dump an FXP profile info
 * @hw: pointer to the hardware structure
 * @prof: profile info to dump
 */
void ice_parser_profile_dump(struct ice_hw *hw,
			     struct ice_parser_profile *prof)
{
	struct device *dev = ice_hw_to_dev(hw);
	u16 i;

	dev_info(dev, "ptypes:\n");
	for (i = 0; i < ICE_FLOW_PTYPE_MAX; i++)
		if (test_bit(i, prof->ptypes))
			dev_info(dev, "\t%u\n", i);

	for (i = 0; i < prof->fv_num; i++)
		dev_info(dev, "proto = %u, offset = %2u, spec = 0x%04x, mask = 0x%04x\n",
			 prof->fv[i].proto_id, prof->fv[i].offset,
			 prof->fv[i].spec, prof->fv[i].msk);

	dev_info(dev, "flags = 0x%04x\n", prof->flags);
	dev_info(dev, "flags_msk = 0x%04x\n", prof->flags_msk);
}
