// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024 Intel Corporation */

#include "ice_common.h"

static void ice_rt_tsr_set(struct ice_parser_rt *rt, u16 tsr)
{
	rt->gpr[ICE_GPR_TSR_IDX] = tsr;
}

static void ice_rt_ho_set(struct ice_parser_rt *rt, u16 ho)
{
	rt->gpr[ICE_GPR_HO_IDX] = ho;
	memcpy(&rt->gpr[ICE_GPR_HV_IDX], &rt->pkt_buf[ho], ICE_GPR_HV_SIZE);
}

static void ice_rt_np_set(struct ice_parser_rt *rt, u16 pc)
{
	rt->gpr[ICE_GPR_NP_IDX] = pc;
}

static void ice_rt_nn_set(struct ice_parser_rt *rt, u16 node)
{
	rt->gpr[ICE_GPR_NN_IDX] = node;
}

static void
ice_rt_flag_set(struct ice_parser_rt *rt, unsigned int idx, bool set)
{
	struct ice_hw *hw = rt->psr->hw;
	unsigned int word, id;

	word = idx / ICE_GPR_FLG_SIZE;
	id = idx % ICE_GPR_FLG_SIZE;

	if (set) {
		rt->gpr[ICE_GPR_FLG_IDX + word] |= (u16)BIT(id);
		ice_debug(hw, ICE_DBG_PARSER, "Set parser flag %u\n", idx);
	} else {
		rt->gpr[ICE_GPR_FLG_IDX + word] &= ~(u16)BIT(id);
		ice_debug(hw, ICE_DBG_PARSER, "Clear parser flag %u\n", idx);
	}
}

static void ice_rt_gpr_set(struct ice_parser_rt *rt, int idx, u16 val)
{
	struct ice_hw *hw = rt->psr->hw;

	if (idx == ICE_GPR_HO_IDX)
		ice_rt_ho_set(rt, val);
	else
		rt->gpr[idx] = val;

	ice_debug(hw, ICE_DBG_PARSER, "Set GPR %d value %d\n", idx, val);
}

static void ice_rt_err_set(struct ice_parser_rt *rt, unsigned int idx, bool set)
{
	struct ice_hw *hw = rt->psr->hw;

	if (set) {
		rt->gpr[ICE_GPR_ERR_IDX] |= (u16)BIT(idx);
		ice_debug(hw, ICE_DBG_PARSER, "Set parser error %u\n", idx);
	} else {
		rt->gpr[ICE_GPR_ERR_IDX] &= ~(u16)BIT(idx);
		ice_debug(hw, ICE_DBG_PARSER, "Reset parser error %u\n", idx);
	}
}

/**
 * ice_parser_rt_reset - reset the parser runtime
 * @rt: pointer to the parser runtime
 */
void ice_parser_rt_reset(struct ice_parser_rt *rt)
{
	struct ice_parser *psr = rt->psr;
	struct ice_metainit_item *mi;
	unsigned int i;

	mi = &psr->mi_table[0];

	memset(rt, 0, sizeof(*rt));
	rt->psr = psr;

	ice_rt_tsr_set(rt, mi->tsr);
	ice_rt_ho_set(rt, mi->ho);
	ice_rt_np_set(rt, mi->pc);
	ice_rt_nn_set(rt, mi->pg_rn);

	for (i = 0; i < ICE_PARSER_FLG_NUM; i++) {
		if (mi->flags & BIT(i))
			ice_rt_flag_set(rt, i, true);
	}
}

/**
 * ice_parser_rt_pktbuf_set - set a packet into parser runtime
 * @rt: pointer to the parser runtime
 * @pkt_buf: buffer with packet data
 * @pkt_len: packet buffer length
 */
void ice_parser_rt_pktbuf_set(struct ice_parser_rt *rt, const u8 *pkt_buf,
			      int pkt_len)
{
	int len = min(ICE_PARSER_MAX_PKT_LEN, pkt_len);
	u16 ho = rt->gpr[ICE_GPR_HO_IDX];

	memcpy(rt->pkt_buf, pkt_buf, len);
	rt->pkt_len = pkt_len;

	memcpy(&rt->gpr[ICE_GPR_HV_IDX], &rt->pkt_buf[ho], ICE_GPR_HV_SIZE);
}

static void ice_bst_key_init(struct ice_parser_rt *rt,
			     struct ice_imem_item *imem)
{
	u8 tsr = (u8)rt->gpr[ICE_GPR_TSR_IDX];
	u16 ho = rt->gpr[ICE_GPR_HO_IDX];
	u8 *key = rt->bst_key;
	int idd, i;

	idd = ICE_BST_TCAM_KEY_SIZE - 1;
	if (imem->b_kb.tsr_ctrl)
		key[idd] = tsr;
	else
		key[idd] = imem->b_kb.prio;

	idd = ICE_BST_KEY_TCAM_SIZE - 1;
	for (i = idd; i >= 0; i--) {
		int j;

		j = ho + idd - i;
		if (j < ICE_PARSER_MAX_PKT_LEN)
			key[i] = rt->pkt_buf[ho + idd - i];
		else
			key[i] = 0;
	}

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Generated Boost TCAM Key:\n");
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
		  key[0], key[1], key[2], key[3], key[4],
		  key[5], key[6], key[7], key[8], key[9]);
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "\n");
}

static u16 ice_bit_rev_u16(u16 v, int len)
{
	return bitrev16(v) >> (BITS_PER_TYPE(v) - len);
}

static u32 ice_bit_rev_u32(u32 v, int len)
{
	return bitrev32(v) >> (BITS_PER_TYPE(v) - len);
}

static u32 ice_hv_bit_sel(struct ice_parser_rt *rt, int start, int len)
{
	int offset;
	u32 buf[2];
	u64 val;

	offset = ICE_GPR_HV_IDX + (start / BITS_PER_TYPE(u16));

	memcpy(buf, &rt->gpr[offset], sizeof(buf));

	buf[0] = bitrev8x4(buf[0]);
	buf[1] = bitrev8x4(buf[1]);

	val = *(u64 *)buf;
	val >>= start % BITS_PER_TYPE(u16);

	return ice_bit_rev_u32(val, len);
}

static u32 ice_pk_build(struct ice_parser_rt *rt,
			struct ice_np_keybuilder *kb)
{
	if (kb->opc == ICE_NPKB_OPC_EXTRACT)
		return ice_hv_bit_sel(rt, kb->start_reg0, kb->len_reg1);
	else if (kb->opc == ICE_NPKB_OPC_BUILD)
		return rt->gpr[kb->start_reg0] |
		       ((u32)rt->gpr[kb->len_reg1] << BITS_PER_TYPE(u16));
	else if (kb->opc == ICE_NPKB_OPC_BYPASS)
		return 0;

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Unsupported OP Code %u\n",
		  kb->opc);
	return U32_MAX;
}

static bool ice_flag_get(struct ice_parser_rt *rt, unsigned int index)
{
	int word = index / ICE_GPR_FLG_SIZE;
	int id = index % ICE_GPR_FLG_SIZE;

	return !!(rt->gpr[ICE_GPR_FLG_IDX + word] & (u16)BIT(id));
}

static int ice_imem_pgk_init(struct ice_parser_rt *rt,
			     struct ice_imem_item *imem)
{
	memset(&rt->pg_key, 0, sizeof(rt->pg_key));
	rt->pg_key.next_proto = ice_pk_build(rt, &imem->np_kb);
	if (rt->pg_key.next_proto == U32_MAX)
		return -EINVAL;

	if (imem->pg_kb.flag0_ena)
		rt->pg_key.flag0 = ice_flag_get(rt, imem->pg_kb.flag0_idx);
	if (imem->pg_kb.flag1_ena)
		rt->pg_key.flag1 = ice_flag_get(rt, imem->pg_kb.flag1_idx);
	if (imem->pg_kb.flag2_ena)
		rt->pg_key.flag2 = ice_flag_get(rt, imem->pg_kb.flag2_idx);
	if (imem->pg_kb.flag3_ena)
		rt->pg_key.flag3 = ice_flag_get(rt, imem->pg_kb.flag3_idx);

	rt->pg_key.alu_reg = rt->gpr[imem->pg_kb.alu_reg_idx];
	rt->pg_key.node_id = rt->gpr[ICE_GPR_NN_IDX];

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Generate Parse Graph Key: node_id(%d), flag0-3(%d,%d,%d,%d), boost_idx(%d), alu_reg(0x%04x), next_proto(0x%08x)\n",
		  rt->pg_key.node_id,
		  rt->pg_key.flag0,
		  rt->pg_key.flag1,
		  rt->pg_key.flag2,
		  rt->pg_key.flag3,
		  rt->pg_key.boost_idx,
		  rt->pg_key.alu_reg,
		  rt->pg_key.next_proto);

	return 0;
}

static void ice_imem_alu0_set(struct ice_parser_rt *rt,
			      struct ice_imem_item *imem)
{
	rt->alu0 = &imem->alu0;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load ALU0 from imem pc %d\n",
		  imem->idx);
}

static void ice_imem_alu1_set(struct ice_parser_rt *rt,
			      struct ice_imem_item *imem)
{
	rt->alu1 = &imem->alu1;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load ALU1 from imem pc %d\n",
		  imem->idx);
}

static void ice_imem_alu2_set(struct ice_parser_rt *rt,
			      struct ice_imem_item *imem)
{
	rt->alu2 = &imem->alu2;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load ALU2 from imem pc %d\n",
		  imem->idx);
}

static void ice_imem_pgp_set(struct ice_parser_rt *rt,
			     struct ice_imem_item *imem)
{
	rt->pg_prio = imem->pg_prio;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load PG priority %d from imem pc %d\n",
		  rt->pg_prio, imem->idx);
}

static int ice_bst_pgk_init(struct ice_parser_rt *rt,
			    struct ice_bst_tcam_item *bst)
{
	memset(&rt->pg_key, 0, sizeof(rt->pg_key));
	rt->pg_key.boost_idx = bst->hit_idx_grp;
	rt->pg_key.next_proto = ice_pk_build(rt, &bst->np_kb);
	if (rt->pg_key.next_proto == U32_MAX)
		return -EINVAL;

	if (bst->pg_kb.flag0_ena)
		rt->pg_key.flag0 = ice_flag_get(rt, bst->pg_kb.flag0_idx);
	if (bst->pg_kb.flag1_ena)
		rt->pg_key.flag1 = ice_flag_get(rt, bst->pg_kb.flag1_idx);
	if (bst->pg_kb.flag2_ena)
		rt->pg_key.flag2 = ice_flag_get(rt, bst->pg_kb.flag2_idx);
	if (bst->pg_kb.flag3_ena)
		rt->pg_key.flag3 = ice_flag_get(rt, bst->pg_kb.flag3_idx);

	rt->pg_key.alu_reg = rt->gpr[bst->pg_kb.alu_reg_idx];
	rt->pg_key.node_id = rt->gpr[ICE_GPR_NN_IDX];

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Generate Parse Graph Key: node_id(%d), flag0-3(%d,%d,%d,%d), boost_idx(%d), alu_reg(0x%04x), next_proto(0x%08x)\n",
		  rt->pg_key.node_id,
		  rt->pg_key.flag0,
		  rt->pg_key.flag1,
		  rt->pg_key.flag2,
		  rt->pg_key.flag3,
		  rt->pg_key.boost_idx,
		  rt->pg_key.alu_reg,
		  rt->pg_key.next_proto);

	return 0;
}

static void ice_bst_alu0_set(struct ice_parser_rt *rt,
			     struct ice_bst_tcam_item *bst)
{
	rt->alu0 = &bst->alu0;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load ALU0 from boost address %d\n",
		  bst->addr);
}

static void ice_bst_alu1_set(struct ice_parser_rt *rt,
			     struct ice_bst_tcam_item *bst)
{
	rt->alu1 = &bst->alu1;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load ALU1 from boost address %d\n",
		  bst->addr);
}

static void ice_bst_alu2_set(struct ice_parser_rt *rt,
			     struct ice_bst_tcam_item *bst)
{
	rt->alu2 = &bst->alu2;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load ALU2 from boost address %d\n",
		  bst->addr);
}

static void ice_bst_pgp_set(struct ice_parser_rt *rt,
			    struct ice_bst_tcam_item *bst)
{
	rt->pg_prio = bst->pg_prio;
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load PG priority %d from boost address %d\n",
		  rt->pg_prio, bst->addr);
}

static struct ice_pg_cam_item *ice_rt_pg_cam_match(struct ice_parser_rt *rt)
{
	struct ice_parser *psr = rt->psr;
	struct ice_pg_cam_item *item;

	item = ice_pg_cam_match(psr->pg_cam_table, ICE_PG_CAM_TABLE_SIZE,
				&rt->pg_key);
	if (!item)
		item = ice_pg_cam_match(psr->pg_sp_cam_table,
					ICE_PG_SP_CAM_TABLE_SIZE, &rt->pg_key);
	return item;
}

static
struct ice_pg_nm_cam_item *ice_rt_pg_nm_cam_match(struct ice_parser_rt *rt)
{
	struct ice_parser *psr = rt->psr;
	struct ice_pg_nm_cam_item *item;

	item = ice_pg_nm_cam_match(psr->pg_nm_cam_table,
				   ICE_PG_NM_CAM_TABLE_SIZE, &rt->pg_key);

	if (!item)
		item = ice_pg_nm_cam_match(psr->pg_nm_sp_cam_table,
					   ICE_PG_NM_SP_CAM_TABLE_SIZE,
					   &rt->pg_key);
	return item;
}

static void ice_gpr_add(struct ice_parser_rt *rt, int idx, u16 val)
{
	rt->pu.gpr_val_upd[idx] = true;
	rt->pu.gpr_val[idx] = val;

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Pending update for register %d value %d\n",
		  idx, val);
}

static void ice_pg_exe(struct ice_parser_rt *rt)
{
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ParseGraph action ...\n");

	ice_gpr_add(rt, ICE_GPR_NP_IDX, rt->action->next_pc);
	ice_gpr_add(rt, ICE_GPR_NN_IDX, rt->action->next_node);

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ParseGraph action done.\n");
}

static void ice_flg_add(struct ice_parser_rt *rt, int idx, bool val)
{
	rt->pu.flg_msk |= BIT_ULL(idx);
	if (val)
		rt->pu.flg_val |= BIT_ULL(idx);
	else
		rt->pu.flg_val &= ~BIT_ULL(idx);

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Pending update for flag %d value %d\n",
		  idx, val);
}

static void ice_flg_update(struct ice_parser_rt *rt, struct ice_alu *alu)
{
	u32 hv_bit_sel;
	int i;

	if (!alu->dedicate_flags_ena)
		return;

	if (alu->flags_extr_imm) {
		for (i = 0; i < alu->dst_len; i++)
			ice_flg_add(rt, alu->dst_start + i,
				    !!(alu->flags_start_imm & BIT(i)));
	} else {
		for (i = 0; i < alu->dst_len; i++) {
			hv_bit_sel = ice_hv_bit_sel(rt,
						    alu->flags_start_imm + i,
						    1);
			ice_flg_add(rt, alu->dst_start + i, !!hv_bit_sel);
		}
	}
}

static void ice_po_update(struct ice_parser_rt *rt, struct ice_alu *alu)
{
	if (alu->proto_offset_opc == ICE_PO_OFF_HDR_ADD)
		rt->po = (u16)(rt->gpr[ICE_GPR_HO_IDX] + alu->proto_offset);
	else if (alu->proto_offset_opc == ICE_PO_OFF_HDR_SUB)
		rt->po = (u16)(rt->gpr[ICE_GPR_HO_IDX] - alu->proto_offset);
	else if (alu->proto_offset_opc == ICE_PO_OFF_REMAIN)
		rt->po = rt->gpr[ICE_GPR_HO_IDX];

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Update Protocol Offset = %d\n",
		  rt->po);
}

static u16 ice_reg_bit_sel(struct ice_parser_rt *rt, int reg_idx,
			   int start, int len)
{
	int offset;
	u32 val;

	offset = ICE_GPR_HV_IDX + (start / BITS_PER_TYPE(u16));

	memcpy(&val, &rt->gpr[offset], sizeof(val));

	val = bitrev8x4(val);
	val >>= start % BITS_PER_TYPE(u16);

	return ice_bit_rev_u16(val, len);
}

static void ice_err_add(struct ice_parser_rt *rt, int idx, bool val)
{
	rt->pu.err_msk |= (u16)BIT(idx);
	if (val)
		rt->pu.flg_val |= (u64)BIT_ULL(idx);
	else
		rt->pu.flg_val &= ~(u64)BIT_ULL(idx);

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Pending update for error %d value %d\n",
		  idx, val);
}

static void ice_dst_reg_bit_set(struct ice_parser_rt *rt, struct ice_alu *alu,
				bool val)
{
	u16 flg_idx;

	if (alu->dedicate_flags_ena) {
		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "DedicatedFlagsEnable should not be enabled in opcode %d\n",
			  alu->opc);
		return;
	}

	if (alu->dst_reg_id == ICE_GPR_ERR_IDX) {
		if (alu->dst_start >= ICE_PARSER_ERR_NUM) {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Invalid error %d\n",
				  alu->dst_start);
			return;
		}
		ice_err_add(rt, alu->dst_start, val);
	} else if (alu->dst_reg_id >= ICE_GPR_FLG_IDX) {
		flg_idx = (u16)(((alu->dst_reg_id - ICE_GPR_FLG_IDX) << 4) +
				alu->dst_start);

		if (flg_idx >= ICE_PARSER_FLG_NUM) {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Invalid flag %d\n",
				  flg_idx);
			return;
		}
		ice_flg_add(rt, flg_idx, val);
	} else {
		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Unexpected Dest Register Bit set, RegisterID %d Start %d\n",
			  alu->dst_reg_id, alu->dst_start);
	}
}

static void ice_alu_exe(struct ice_parser_rt *rt, struct ice_alu *alu)
{
	u16 dst, src, shift, imm;

	if (alu->shift_xlate_sel) {
		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "shift_xlate_sel != 0 is not expected\n");
		return;
	}

	ice_po_update(rt, alu);
	ice_flg_update(rt, alu);

	dst = rt->gpr[alu->dst_reg_id];
	src = ice_reg_bit_sel(rt, alu->src_reg_id,
			      alu->src_start, alu->src_len);
	shift = alu->shift_xlate_key;
	imm = alu->imm;

	switch (alu->opc) {
	case ICE_ALU_PARK:
		break;
	case ICE_ALU_MOV_ADD:
		dst = (src << shift) + imm;
		ice_gpr_add(rt, alu->dst_reg_id, dst);
		break;
	case ICE_ALU_ADD:
		dst += (src << shift) + imm;
		ice_gpr_add(rt, alu->dst_reg_id, dst);
		break;
	case ICE_ALU_ORLT:
		if (src < imm)
			ice_dst_reg_bit_set(rt, alu, true);
		ice_gpr_add(rt, ICE_GPR_NP_IDX, alu->branch_addr);
		break;
	case ICE_ALU_OREQ:
		if (src == imm)
			ice_dst_reg_bit_set(rt, alu, true);
		ice_gpr_add(rt, ICE_GPR_NP_IDX, alu->branch_addr);
		break;
	case ICE_ALU_SETEQ:
		ice_dst_reg_bit_set(rt, alu, src == imm);
		ice_gpr_add(rt, ICE_GPR_NP_IDX, alu->branch_addr);
		break;
	case ICE_ALU_MOV_XOR:
		dst = (src << shift) ^ imm;
		ice_gpr_add(rt, alu->dst_reg_id, dst);
		break;
	default:
		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Unsupported ALU instruction %d\n",
			  alu->opc);
		break;
	}
}

static void ice_alu0_exe(struct ice_parser_rt *rt)
{
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ALU0 ...\n");
	ice_alu_exe(rt, rt->alu0);
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ALU0 done.\n");
}

static void ice_alu1_exe(struct ice_parser_rt *rt)
{
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ALU1 ...\n");
	ice_alu_exe(rt, rt->alu1);
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ALU1 done.\n");
}

static void ice_alu2_exe(struct ice_parser_rt *rt)
{
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ALU2 ...\n");
	ice_alu_exe(rt, rt->alu2);
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Executing ALU2 done.\n");
}

static void ice_pu_exe(struct ice_parser_rt *rt)
{
	struct ice_gpr_pu *pu = &rt->pu;
	unsigned int i;

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Updating Registers ...\n");

	for (i = 0; i < ICE_PARSER_GPR_NUM; i++) {
		if (pu->gpr_val_upd[i])
			ice_rt_gpr_set(rt, i, pu->gpr_val[i]);
	}

	for (i = 0; i < ICE_PARSER_FLG_NUM; i++) {
		if (pu->flg_msk & BIT(i))
			ice_rt_flag_set(rt, i, pu->flg_val & BIT(i));
	}

	for (i = 0; i < ICE_PARSER_ERR_NUM; i++) {
		if (pu->err_msk & BIT(i))
			ice_rt_err_set(rt, i, pu->err_val & BIT(i));
	}

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Updating Registers done.\n");
}

static void ice_alu_pg_exe(struct ice_parser_rt *rt)
{
	memset(&rt->pu, 0, sizeof(rt->pu));

	switch (rt->pg_prio) {
	case (ICE_PG_P0):
		ice_pg_exe(rt);
		ice_alu0_exe(rt);
		ice_alu1_exe(rt);
		ice_alu2_exe(rt);
		break;
	case (ICE_PG_P1):
		ice_alu0_exe(rt);
		ice_pg_exe(rt);
		ice_alu1_exe(rt);
		ice_alu2_exe(rt);
		break;
	case (ICE_PG_P2):
		ice_alu0_exe(rt);
		ice_alu1_exe(rt);
		ice_pg_exe(rt);
		ice_alu2_exe(rt);
		break;
	case (ICE_PG_P3):
		ice_alu0_exe(rt);
		ice_alu1_exe(rt);
		ice_alu2_exe(rt);
		ice_pg_exe(rt);
		break;
	}

	ice_pu_exe(rt);

	if (rt->action->ho_inc == 0)
		return;

	if (rt->action->ho_polarity)
		ice_rt_ho_set(rt, rt->gpr[ICE_GPR_HO_IDX] + rt->action->ho_inc);
	else
		ice_rt_ho_set(rt, rt->gpr[ICE_GPR_HO_IDX] - rt->action->ho_inc);
}

static void ice_proto_off_update(struct ice_parser_rt *rt)
{
	struct ice_parser *psr = rt->psr;

	if (rt->action->is_pg) {
		struct ice_proto_grp_item *proto_grp =
			&psr->proto_grp_table[rt->action->proto_id];
		u16 po;
		int i;

		for (i = 0; i < ICE_PROTO_COUNT_PER_GRP; i++) {
			struct ice_proto_off *entry = &proto_grp->po[i];

			if (entry->proto_id == U8_MAX)
				break;

			if (!entry->polarity)
				po = rt->po + entry->offset;
			else
				po = rt->po - entry->offset;

			rt->protocols[entry->proto_id] = true;
			rt->offsets[entry->proto_id] = po;

			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Set Protocol %d at offset %d\n",
				  entry->proto_id, po);
		}
	} else {
		rt->protocols[rt->action->proto_id] = true;
		rt->offsets[rt->action->proto_id] = rt->po;

		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Set Protocol %d at offset %d\n",
			  rt->action->proto_id, rt->po);
	}
}

static void ice_marker_set(struct ice_parser_rt *rt, int idx)
{
	unsigned int byte = idx / BITS_PER_BYTE;
	unsigned int bit = idx % BITS_PER_BYTE;

	rt->markers[byte] |= (u8)BIT(bit);
}

static void ice_marker_update(struct ice_parser_rt *rt)
{
	struct ice_parser *psr = rt->psr;

	if (rt->action->is_mg) {
		struct ice_mk_grp_item *mk_grp =
			&psr->mk_grp_table[rt->action->marker_id];
		int i;

		for (i = 0; i < ICE_MARKER_ID_NUM; i++) {
			u8 marker = mk_grp->markers[i];

			if (marker == ICE_MARKER_MAX_SIZE)
				break;

			ice_marker_set(rt, marker);
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Set Marker %d\n",
				  marker);
		}
	} else {
		if (rt->action->marker_id != ICE_MARKER_MAX_SIZE)
			ice_marker_set(rt, rt->action->marker_id);

		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Set Marker %d\n",
			  rt->action->marker_id);
	}
}

static u16 ice_ptype_resolve(struct ice_parser_rt *rt)
{
	struct ice_ptype_mk_tcam_item *item;
	struct ice_parser *psr = rt->psr;

	item = ice_ptype_mk_tcam_match(psr->ptype_mk_tcam_table,
				       rt->markers, ICE_MARKER_ID_SIZE);
	if (item)
		return item->ptype;

	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Could not resolve PTYPE\n");
	return U16_MAX;
}

static void ice_proto_off_resolve(struct ice_parser_rt *rt,
				  struct ice_parser_result *rslt)
{
	int i;

	for (i = 0; i < ICE_PO_PAIR_SIZE - 1; i++) {
		if (rt->protocols[i]) {
			rslt->po[rslt->po_num].proto_id = (u8)i;
			rslt->po[rslt->po_num].offset = rt->offsets[i];
			rslt->po_num++;
		}
	}
}

static void ice_result_resolve(struct ice_parser_rt *rt,
			       struct ice_parser_result *rslt)
{
	struct ice_parser *psr = rt->psr;

	memset(rslt, 0, sizeof(*rslt));

	memcpy(&rslt->flags_psr, &rt->gpr[ICE_GPR_FLG_IDX],
	       ICE_PARSER_FLAG_PSR_SIZE);
	rslt->flags_pkt = ice_flg_redirect(psr->flg_rd_table, rslt->flags_psr);
	rslt->flags_sw = ice_xlt_kb_flag_get(psr->xlt_kb_sw, rslt->flags_pkt);
	rslt->flags_fd = ice_xlt_kb_flag_get(psr->xlt_kb_fd, rslt->flags_pkt);
	rslt->flags_rss = ice_xlt_kb_flag_get(psr->xlt_kb_rss, rslt->flags_pkt);

	ice_proto_off_resolve(rt, rslt);
	rslt->ptype = ice_ptype_resolve(rt);
}

/**
 * ice_parser_rt_execute - parser execution routine
 * @rt: pointer to the parser runtime
 * @rslt: input/output parameter to save parser result
 *
 * Return: 0 on success or errno.
 */
int ice_parser_rt_execute(struct ice_parser_rt *rt,
			  struct ice_parser_result *rslt)
{
	struct ice_pg_nm_cam_item *pg_nm_cam;
	struct ice_parser *psr = rt->psr;
	struct ice_pg_cam_item *pg_cam;
	int status = 0;
	u16 node;
	u16 pc;

	node = rt->gpr[ICE_GPR_NN_IDX];
	ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Start with Node: %u\n", node);

	while (true) {
		struct ice_bst_tcam_item *bst;
		struct ice_imem_item *imem;

		pc = rt->gpr[ICE_GPR_NP_IDX];
		imem = &psr->imem_table[pc];
		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Load imem at pc: %u\n",
			  pc);

		ice_bst_key_init(rt, imem);
		bst = ice_bst_tcam_match(psr->bst_tcam_table, rt->bst_key);
		if (!bst) {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "No Boost TCAM Match\n");
			status = ice_imem_pgk_init(rt, imem);
			if (status)
				break;
			ice_imem_alu0_set(rt, imem);
			ice_imem_alu1_set(rt, imem);
			ice_imem_alu2_set(rt, imem);
			ice_imem_pgp_set(rt, imem);
		} else {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Boost TCAM Match address: %u\n",
				  bst->addr);
			if (imem->b_m.pg) {
				status = ice_bst_pgk_init(rt, bst);
				if (status)
					break;
				ice_bst_pgp_set(rt, bst);
			} else {
				status = ice_imem_pgk_init(rt, imem);
				if (status)
					break;
				ice_imem_pgp_set(rt, imem);
			}

			if (imem->b_m.alu0)
				ice_bst_alu0_set(rt, bst);
			else
				ice_imem_alu0_set(rt, imem);

			if (imem->b_m.alu1)
				ice_bst_alu1_set(rt, bst);
			else
				ice_imem_alu1_set(rt, imem);

			if (imem->b_m.alu2)
				ice_bst_alu2_set(rt, bst);
			else
				ice_imem_alu2_set(rt, imem);
		}

		rt->action = NULL;
		pg_cam = ice_rt_pg_cam_match(rt);
		if (!pg_cam) {
			pg_nm_cam = ice_rt_pg_nm_cam_match(rt);
			if (pg_nm_cam) {
				ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Match ParseGraph Nomatch CAM Address %u\n",
					  pg_nm_cam->idx);
				rt->action = &pg_nm_cam->action;
			}
		} else {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Match ParseGraph CAM Address %u\n",
				  pg_cam->idx);
			rt->action = &pg_cam->action;
		}

		if (!rt->action) {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Failed to match ParseGraph CAM, stop parsing.\n");
			status = -EINVAL;
			break;
		}

		ice_alu_pg_exe(rt);
		ice_marker_update(rt);
		ice_proto_off_update(rt);

		ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Go to node %u\n",
			  rt->action->next_node);

		if (rt->action->is_last_round) {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Last Round in ParseGraph Action, stop parsing.\n");
			break;
		}

		if (rt->gpr[ICE_GPR_HO_IDX] >= rt->pkt_len) {
			ice_debug(rt->psr->hw, ICE_DBG_PARSER, "Header Offset (%u) is larger than packet len (%u), stop parsing\n",
				  rt->gpr[ICE_GPR_HO_IDX], rt->pkt_len);
			break;
		}
	}

	ice_result_resolve(rt, rslt);

	return status;
}
