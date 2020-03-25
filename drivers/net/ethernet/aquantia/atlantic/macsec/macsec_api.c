// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "macsec_api.h"
#include <linux/mdio.h>
#include "MSS_Egress_registers.h"
#include "aq_phy.h"

#define AQ_API_CALL_SAFE(func, ...)                                            \
({                                                                             \
	int ret;                                                               \
	do {                                                                   \
		ret = aq_mss_mdio_sem_get(hw);                                 \
		if (unlikely(ret))                                             \
			break;                                                 \
									       \
		ret = func(__VA_ARGS__);                                       \
									       \
		aq_mss_mdio_sem_put(hw);                                       \
	} while (0);                                                           \
	ret;                                                                   \
})

/*******************************************************************************
 *                               MDIO wrappers
 ******************************************************************************/
static int aq_mss_mdio_sem_get(struct aq_hw_s *hw)
{
	u32 val;

	return readx_poll_timeout_atomic(hw_atl_sem_mdio_get, hw, val,
					 val == 1U, 10U, 100000U);
}

static void aq_mss_mdio_sem_put(struct aq_hw_s *hw)
{
	hw_atl_reg_glb_cpu_sem_set(hw, 1U, HW_ATL_FW_SM_MDIO);
}

static int aq_mss_mdio_read(struct aq_hw_s *hw, u16 mmd, u16 addr, u16 *data)
{
	*data = aq_mdio_read_word(hw, mmd, addr);
	return (*data != 0xffff) ? 0 : -ETIME;
}

static int aq_mss_mdio_write(struct aq_hw_s *hw, u16 mmd, u16 addr, u16 data)
{
	aq_mdio_write_word(hw, mmd, addr, data);
	return 0;
}

/*******************************************************************************
 *                          MACSEC config and status
 ******************************************************************************/

/*! Write packed_record to the specified Egress LUT table row. */
static int set_raw_egress_record(struct aq_hw_s *hw, u16 *packed_record,
				 u8 num_words, u8 table_id,
				 u16 table_index)
{
	struct mss_egress_lut_addr_ctl_register lut_sel_reg;
	struct mss_egress_lut_ctl_register lut_op_reg;

	unsigned int i;

	/* Write the packed record words to the data buffer registers. */
	for (i = 0; i < num_words; i += 2) {
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR + i,
				  packed_record[i]);
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR + i + 1,
				  packed_record[i + 1]);
	}

	/* Clear out the unused data buffer registers. */
	for (i = num_words; i < 28; i += 2) {
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR + i, 0);
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR + i + 1,
				  0);
	}

	/* Select the table and row index to write to */
	lut_sel_reg.bits_0.lut_select = table_id;
	lut_sel_reg.bits_0.lut_addr = table_index;

	lut_op_reg.bits_0.lut_read = 0;
	lut_op_reg.bits_0.lut_write = 1;

	aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
			  MSS_EGRESS_LUT_ADDR_CTL_REGISTER_ADDR,
			  lut_sel_reg.word_0);
	aq_mss_mdio_write(hw, MDIO_MMD_VEND1, MSS_EGRESS_LUT_CTL_REGISTER_ADDR,
			  lut_op_reg.word_0);

	return 0;
}

static int get_raw_egress_record(struct aq_hw_s *hw, u16 *packed_record,
				 u8 num_words, u8 table_id,
				 u16 table_index)
{
	struct mss_egress_lut_addr_ctl_register lut_sel_reg;
	struct mss_egress_lut_ctl_register lut_op_reg;
	int ret;

	unsigned int i;

	/* Select the table and row index to read */
	lut_sel_reg.bits_0.lut_select = table_id;
	lut_sel_reg.bits_0.lut_addr = table_index;

	lut_op_reg.bits_0.lut_read = 1;
	lut_op_reg.bits_0.lut_write = 0;

	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_LUT_ADDR_CTL_REGISTER_ADDR,
				lut_sel_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_LUT_CTL_REGISTER_ADDR,
				lut_op_reg.word_0);
	if (unlikely(ret))
		return ret;

	memset(packed_record, 0, sizeof(u16) * num_words);

	for (i = 0; i < num_words; i += 2) {
		ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
				       MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR +
					       i,
				       &packed_record[i]);
		if (unlikely(ret))
			return ret;
		ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
				       MSS_EGRESS_LUT_DATA_CTL_REGISTER_ADDR +
					       i + 1,
				       &packed_record[i + 1]);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

static int set_egress_ctlf_record(struct aq_hw_s *hw,
				  const struct aq_mss_egress_ctlf_record *rec,
				  u16 table_index)
{
	u16 packed_record[6];

	if (table_index >= NUMROWS_EGRESSCTLFRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 6);

	packed_record[0] = rec->sa_da[0] & 0xFFFF;
	packed_record[1] = (rec->sa_da[0] >> 16) & 0xFFFF;
	packed_record[2] = rec->sa_da[1] & 0xFFFF;

	packed_record[3] = rec->eth_type & 0xFFFF;

	packed_record[4] = rec->match_mask & 0xFFFF;

	packed_record[5] = rec->match_type & 0xF;

	packed_record[5] |= (rec->action & 0x1) << 4;

	return set_raw_egress_record(hw, packed_record, 6, 0,
				     ROWOFFSET_EGRESSCTLFRECORD + table_index);
}

int aq_mss_set_egress_ctlf_record(struct aq_hw_s *hw,
				  const struct aq_mss_egress_ctlf_record *rec,
				  u16 table_index)
{
	return AQ_API_CALL_SAFE(set_egress_ctlf_record, hw, rec, table_index);
}

static int get_egress_ctlf_record(struct aq_hw_s *hw,
				  struct aq_mss_egress_ctlf_record *rec,
				  u16 table_index)
{
	u16 packed_record[6];
	int ret;

	if (table_index >= NUMROWS_EGRESSCTLFRECORD)
		return -EINVAL;

	/* If the row that we want to read is odd, first read the previous even
	 * row, throw that value away, and finally read the desired row.
	 */
	if ((table_index % 2) > 0) {
		ret = get_raw_egress_record(hw, packed_record, 6, 0,
					    ROWOFFSET_EGRESSCTLFRECORD +
						    table_index - 1);
		if (unlikely(ret))
			return ret;
	}

	ret = get_raw_egress_record(hw, packed_record, 6, 0,
				    ROWOFFSET_EGRESSCTLFRECORD + table_index);
	if (unlikely(ret))
		return ret;

	rec->sa_da[0] = packed_record[0];
	rec->sa_da[0] |= packed_record[1] << 16;

	rec->sa_da[1] = packed_record[2];

	rec->eth_type = packed_record[3];

	rec->match_mask = packed_record[4];

	rec->match_type = packed_record[5] & 0xF;

	rec->action = (packed_record[5] >> 4) & 0x1;

	return 0;
}

int aq_mss_get_egress_ctlf_record(struct aq_hw_s *hw,
				  struct aq_mss_egress_ctlf_record *rec,
				  u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_egress_ctlf_record, hw, rec, table_index);
}

static int set_egress_class_record(struct aq_hw_s *hw,
				   const struct aq_mss_egress_class_record *rec,
				   u16 table_index)
{
	u16 packed_record[28];

	if (table_index >= NUMROWS_EGRESSCLASSRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 28);

	packed_record[0] = rec->vlan_id & 0xFFF;

	packed_record[0] |= (rec->vlan_up & 0x7) << 12;

	packed_record[0] |= (rec->vlan_valid & 0x1) << 15;

	packed_record[1] = rec->byte3 & 0xFF;

	packed_record[1] |= (rec->byte2 & 0xFF) << 8;

	packed_record[2] = rec->byte1 & 0xFF;

	packed_record[2] |= (rec->byte0 & 0xFF) << 8;

	packed_record[3] = rec->tci & 0xFF;

	packed_record[3] |= (rec->sci[0] & 0xFF) << 8;
	packed_record[4] = (rec->sci[0] >> 8) & 0xFFFF;
	packed_record[5] = (rec->sci[0] >> 24) & 0xFF;

	packed_record[5] |= (rec->sci[1] & 0xFF) << 8;
	packed_record[6] = (rec->sci[1] >> 8) & 0xFFFF;
	packed_record[7] = (rec->sci[1] >> 24) & 0xFF;

	packed_record[7] |= (rec->eth_type & 0xFF) << 8;
	packed_record[8] = (rec->eth_type >> 8) & 0xFF;

	packed_record[8] |= (rec->snap[0] & 0xFF) << 8;
	packed_record[9] = (rec->snap[0] >> 8) & 0xFFFF;
	packed_record[10] = (rec->snap[0] >> 24) & 0xFF;

	packed_record[10] |= (rec->snap[1] & 0xFF) << 8;

	packed_record[11] = rec->llc & 0xFFFF;
	packed_record[12] = (rec->llc >> 16) & 0xFF;

	packed_record[12] |= (rec->mac_sa[0] & 0xFF) << 8;
	packed_record[13] = (rec->mac_sa[0] >> 8) & 0xFFFF;
	packed_record[14] = (rec->mac_sa[0] >> 24) & 0xFF;

	packed_record[14] |= (rec->mac_sa[1] & 0xFF) << 8;
	packed_record[15] = (rec->mac_sa[1] >> 8) & 0xFF;

	packed_record[15] |= (rec->mac_da[0] & 0xFF) << 8;
	packed_record[16] = (rec->mac_da[0] >> 8) & 0xFFFF;
	packed_record[17] = (rec->mac_da[0] >> 24) & 0xFF;

	packed_record[17] |= (rec->mac_da[1] & 0xFF) << 8;
	packed_record[18] = (rec->mac_da[1] >> 8) & 0xFF;

	packed_record[18] |= (rec->pn & 0xFF) << 8;
	packed_record[19] = (rec->pn >> 8) & 0xFFFF;
	packed_record[20] = (rec->pn >> 24) & 0xFF;

	packed_record[20] |= (rec->byte3_location & 0x3F) << 8;

	packed_record[20] |= (rec->byte3_mask & 0x1) << 14;

	packed_record[20] |= (rec->byte2_location & 0x1) << 15;
	packed_record[21] = (rec->byte2_location >> 1) & 0x1F;

	packed_record[21] |= (rec->byte2_mask & 0x1) << 5;

	packed_record[21] |= (rec->byte1_location & 0x3F) << 6;

	packed_record[21] |= (rec->byte1_mask & 0x1) << 12;

	packed_record[21] |= (rec->byte0_location & 0x7) << 13;
	packed_record[22] = (rec->byte0_location >> 3) & 0x7;

	packed_record[22] |= (rec->byte0_mask & 0x1) << 3;

	packed_record[22] |= (rec->vlan_id_mask & 0x3) << 4;

	packed_record[22] |= (rec->vlan_up_mask & 0x1) << 6;

	packed_record[22] |= (rec->vlan_valid_mask & 0x1) << 7;

	packed_record[22] |= (rec->tci_mask & 0xFF) << 8;

	packed_record[23] = rec->sci_mask & 0xFF;

	packed_record[23] |= (rec->eth_type_mask & 0x3) << 8;

	packed_record[23] |= (rec->snap_mask & 0x1F) << 10;

	packed_record[23] |= (rec->llc_mask & 0x1) << 15;
	packed_record[24] = (rec->llc_mask >> 1) & 0x3;

	packed_record[24] |= (rec->sa_mask & 0x3F) << 2;

	packed_record[24] |= (rec->da_mask & 0x3F) << 8;

	packed_record[24] |= (rec->pn_mask & 0x3) << 14;
	packed_record[25] = (rec->pn_mask >> 2) & 0x3;

	packed_record[25] |= (rec->eight02dot2 & 0x1) << 2;

	packed_record[25] |= (rec->tci_sc & 0x1) << 3;

	packed_record[25] |= (rec->tci_87543 & 0x1) << 4;

	packed_record[25] |= (rec->exp_sectag_en & 0x1) << 5;

	packed_record[25] |= (rec->sc_idx & 0x1F) << 6;

	packed_record[25] |= (rec->sc_sa & 0x3) << 11;

	packed_record[25] |= (rec->debug & 0x1) << 13;

	packed_record[25] |= (rec->action & 0x3) << 14;

	packed_record[26] = (rec->valid & 0x1) << 3;

	return set_raw_egress_record(hw, packed_record, 28, 1,
				     ROWOFFSET_EGRESSCLASSRECORD + table_index);
}

int aq_mss_set_egress_class_record(struct aq_hw_s *hw,
				   const struct aq_mss_egress_class_record *rec,
				   u16 table_index)
{
	return AQ_API_CALL_SAFE(set_egress_class_record, hw, rec, table_index);
}

static int get_egress_class_record(struct aq_hw_s *hw,
				   struct aq_mss_egress_class_record *rec,
				   u16 table_index)
{
	u16 packed_record[28];
	int ret;

	if (table_index >= NUMROWS_EGRESSCLASSRECORD)
		return -EINVAL;

	/* If the row that we want to read is odd, first read the previous even
	 * row, throw that value away, and finally read the desired row.
	 */
	if ((table_index % 2) > 0) {
		ret = get_raw_egress_record(hw, packed_record, 28, 1,
					    ROWOFFSET_EGRESSCLASSRECORD +
						    table_index - 1);
		if (unlikely(ret))
			return ret;
	}

	ret = get_raw_egress_record(hw, packed_record, 28, 1,
				    ROWOFFSET_EGRESSCLASSRECORD + table_index);
	if (unlikely(ret))
		return ret;

	rec->vlan_id = packed_record[0] & 0xFFF;

	rec->vlan_up = (packed_record[0] >> 12) & 0x7;

	rec->vlan_valid = (packed_record[0] >> 15) & 0x1;

	rec->byte3 = packed_record[1] & 0xFF;

	rec->byte2 = (packed_record[1] >> 8) & 0xFF;

	rec->byte1 = packed_record[2] & 0xFF;

	rec->byte0 = (packed_record[2] >> 8) & 0xFF;

	rec->tci = packed_record[3] & 0xFF;

	rec->sci[0] = (packed_record[3] >> 8) & 0xFF;
	rec->sci[0] |= packed_record[4] << 8;
	rec->sci[0] |= (packed_record[5] & 0xFF) << 24;

	rec->sci[1] = (packed_record[5] >> 8) & 0xFF;
	rec->sci[1] |= packed_record[6] << 8;
	rec->sci[1] |= (packed_record[7] & 0xFF) << 24;

	rec->eth_type = (packed_record[7] >> 8) & 0xFF;
	rec->eth_type |= (packed_record[8] & 0xFF) << 8;

	rec->snap[0] = (packed_record[8] >> 8) & 0xFF;
	rec->snap[0] |= packed_record[9] << 8;
	rec->snap[0] |= (packed_record[10] & 0xFF) << 24;

	rec->snap[1] = (packed_record[10] >> 8) & 0xFF;

	rec->llc = packed_record[11];
	rec->llc |= (packed_record[12] & 0xFF) << 16;

	rec->mac_sa[0] = (packed_record[12] >> 8) & 0xFF;
	rec->mac_sa[0] |= packed_record[13] << 8;
	rec->mac_sa[0] |= (packed_record[14] & 0xFF) << 24;

	rec->mac_sa[1] = (packed_record[14] >> 8) & 0xFF;
	rec->mac_sa[1] |= (packed_record[15] & 0xFF) << 8;

	rec->mac_da[0] = (packed_record[15] >> 8) & 0xFF;
	rec->mac_da[0] |= packed_record[16] << 8;
	rec->mac_da[0] |= (packed_record[17] & 0xFF) << 24;

	rec->mac_da[1] = (packed_record[17] >> 8) & 0xFF;
	rec->mac_da[1] |= (packed_record[18] & 0xFF) << 8;

	rec->pn = (packed_record[18] >> 8) & 0xFF;
	rec->pn |= packed_record[19] << 8;
	rec->pn |= (packed_record[20] & 0xFF) << 24;

	rec->byte3_location = (packed_record[20] >> 8) & 0x3F;

	rec->byte3_mask = (packed_record[20] >> 14) & 0x1;

	rec->byte2_location = (packed_record[20] >> 15) & 0x1;
	rec->byte2_location |= (packed_record[21] & 0x1F) << 1;

	rec->byte2_mask = (packed_record[21] >> 5) & 0x1;

	rec->byte1_location = (packed_record[21] >> 6) & 0x3F;

	rec->byte1_mask = (packed_record[21] >> 12) & 0x1;

	rec->byte0_location = (packed_record[21] >> 13) & 0x7;
	rec->byte0_location |= (packed_record[22] & 0x7) << 3;

	rec->byte0_mask = (packed_record[22] >> 3) & 0x1;

	rec->vlan_id_mask = (packed_record[22] >> 4) & 0x3;

	rec->vlan_up_mask = (packed_record[22] >> 6) & 0x1;

	rec->vlan_valid_mask = (packed_record[22] >> 7) & 0x1;

	rec->tci_mask = (packed_record[22] >> 8) & 0xFF;

	rec->sci_mask = packed_record[23] & 0xFF;

	rec->eth_type_mask = (packed_record[23] >> 8) & 0x3;

	rec->snap_mask = (packed_record[23] >> 10) & 0x1F;

	rec->llc_mask = (packed_record[23] >> 15) & 0x1;
	rec->llc_mask |= (packed_record[24] & 0x3) << 1;

	rec->sa_mask = (packed_record[24] >> 2) & 0x3F;

	rec->da_mask = (packed_record[24] >> 8) & 0x3F;

	rec->pn_mask = (packed_record[24] >> 14) & 0x3;
	rec->pn_mask |= (packed_record[25] & 0x3) << 2;

	rec->eight02dot2 = (packed_record[25] >> 2) & 0x1;

	rec->tci_sc = (packed_record[25] >> 3) & 0x1;

	rec->tci_87543 = (packed_record[25] >> 4) & 0x1;

	rec->exp_sectag_en = (packed_record[25] >> 5) & 0x1;

	rec->sc_idx = (packed_record[25] >> 6) & 0x1F;

	rec->sc_sa = (packed_record[25] >> 11) & 0x3;

	rec->debug = (packed_record[25] >> 13) & 0x1;

	rec->action = (packed_record[25] >> 14) & 0x3;

	rec->valid = (packed_record[26] >> 3) & 0x1;

	return 0;
}

int aq_mss_get_egress_class_record(struct aq_hw_s *hw,
				   struct aq_mss_egress_class_record *rec,
				   u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_egress_class_record, hw, rec, table_index);
}

static int set_egress_sc_record(struct aq_hw_s *hw,
				const struct aq_mss_egress_sc_record *rec,
				u16 table_index)
{
	u16 packed_record[8];

	if (table_index >= NUMROWS_EGRESSSCRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 8);

	packed_record[0] = rec->start_time & 0xFFFF;
	packed_record[1] = (rec->start_time >> 16) & 0xFFFF;

	packed_record[2] = rec->stop_time & 0xFFFF;
	packed_record[3] = (rec->stop_time >> 16) & 0xFFFF;

	packed_record[4] = rec->curr_an & 0x3;

	packed_record[4] |= (rec->an_roll & 0x1) << 2;

	packed_record[4] |= (rec->tci & 0x3F) << 3;

	packed_record[4] |= (rec->enc_off & 0x7F) << 9;
	packed_record[5] = (rec->enc_off >> 7) & 0x1;

	packed_record[5] |= (rec->protect & 0x1) << 1;

	packed_record[5] |= (rec->recv & 0x1) << 2;

	packed_record[5] |= (rec->fresh & 0x1) << 3;

	packed_record[5] |= (rec->sak_len & 0x3) << 4;

	packed_record[7] |= (rec->valid & 0x1) << 15;

	return set_raw_egress_record(hw, packed_record, 8, 2,
				     ROWOFFSET_EGRESSSCRECORD + table_index);
}

int aq_mss_set_egress_sc_record(struct aq_hw_s *hw,
				const struct aq_mss_egress_sc_record *rec,
				u16 table_index)
{
	return AQ_API_CALL_SAFE(set_egress_sc_record, hw, rec, table_index);
}

static int get_egress_sc_record(struct aq_hw_s *hw,
				struct aq_mss_egress_sc_record *rec,
				u16 table_index)
{
	u16 packed_record[8];
	int ret;

	if (table_index >= NUMROWS_EGRESSSCRECORD)
		return -EINVAL;

	ret = get_raw_egress_record(hw, packed_record, 8, 2,
				    ROWOFFSET_EGRESSSCRECORD + table_index);
	if (unlikely(ret))
		return ret;

	rec->start_time = packed_record[0];
	rec->start_time |= packed_record[1] << 16;

	rec->stop_time = packed_record[2];
	rec->stop_time |= packed_record[3] << 16;

	rec->curr_an = packed_record[4] & 0x3;

	rec->an_roll = (packed_record[4] >> 2) & 0x1;

	rec->tci = (packed_record[4] >> 3) & 0x3F;

	rec->enc_off = (packed_record[4] >> 9) & 0x7F;
	rec->enc_off |= (packed_record[5] & 0x1) << 7;

	rec->protect = (packed_record[5] >> 1) & 0x1;

	rec->recv = (packed_record[5] >> 2) & 0x1;

	rec->fresh = (packed_record[5] >> 3) & 0x1;

	rec->sak_len = (packed_record[5] >> 4) & 0x3;

	rec->valid = (packed_record[7] >> 15) & 0x1;

	return 0;
}

int aq_mss_get_egress_sc_record(struct aq_hw_s *hw,
				struct aq_mss_egress_sc_record *rec,
				u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_egress_sc_record, hw, rec, table_index);
}

static int set_egress_sa_record(struct aq_hw_s *hw,
				const struct aq_mss_egress_sa_record *rec,
				u16 table_index)
{
	u16 packed_record[8];

	if (table_index >= NUMROWS_EGRESSSARECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 8);

	packed_record[0] = rec->start_time & 0xFFFF;
	packed_record[1] = (rec->start_time >> 16) & 0xFFFF;

	packed_record[2] = rec->stop_time & 0xFFFF;
	packed_record[3] = (rec->stop_time >> 16) & 0xFFFF;

	packed_record[4] = rec->next_pn & 0xFFFF;
	packed_record[5] = (rec->next_pn >> 16) & 0xFFFF;

	packed_record[6] = rec->sat_pn & 0x1;

	packed_record[6] |= (rec->fresh & 0x1) << 1;

	packed_record[7] = (rec->valid & 0x1) << 15;

	return set_raw_egress_record(hw, packed_record, 8, 2,
				     ROWOFFSET_EGRESSSARECORD + table_index);
}

int aq_mss_set_egress_sa_record(struct aq_hw_s *hw,
				const struct aq_mss_egress_sa_record *rec,
				u16 table_index)
{
	int err = AQ_API_CALL_SAFE(set_egress_sa_record, hw, rec, table_index);

	WARN_ONCE(err, "%s failed with %d\n", __func__, err);

	return err;
}

static int get_egress_sa_record(struct aq_hw_s *hw,
				struct aq_mss_egress_sa_record *rec,
				u16 table_index)
{
	u16 packed_record[8];
	int ret;

	if (table_index >= NUMROWS_EGRESSSARECORD)
		return -EINVAL;

	ret = get_raw_egress_record(hw, packed_record, 8, 2,
				    ROWOFFSET_EGRESSSARECORD + table_index);
	if (unlikely(ret))
		return ret;

	rec->start_time = packed_record[0];
	rec->start_time |= packed_record[1] << 16;

	rec->stop_time = packed_record[2];
	rec->stop_time |= packed_record[3] << 16;

	rec->next_pn = packed_record[4];
	rec->next_pn |= packed_record[5] << 16;

	rec->sat_pn = packed_record[6] & 0x1;

	rec->fresh = (packed_record[6] >> 1) & 0x1;

	rec->valid = (packed_record[7] >> 15) & 0x1;

	return 0;
}

int aq_mss_get_egress_sa_record(struct aq_hw_s *hw,
				struct aq_mss_egress_sa_record *rec,
				u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_egress_sa_record, hw, rec, table_index);
}

static int set_egress_sakey_record(struct aq_hw_s *hw,
				   const struct aq_mss_egress_sakey_record *rec,
				   u16 table_index)
{
	u16 packed_record[16];
	int ret;

	if (table_index >= NUMROWS_EGRESSSAKEYRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 16);

	packed_record[0] = rec->key[0] & 0xFFFF;
	packed_record[1] = (rec->key[0] >> 16) & 0xFFFF;

	packed_record[2] = rec->key[1] & 0xFFFF;
	packed_record[3] = (rec->key[1] >> 16) & 0xFFFF;

	packed_record[4] = rec->key[2] & 0xFFFF;
	packed_record[5] = (rec->key[2] >> 16) & 0xFFFF;

	packed_record[6] = rec->key[3] & 0xFFFF;
	packed_record[7] = (rec->key[3] >> 16) & 0xFFFF;

	packed_record[8] = rec->key[4] & 0xFFFF;
	packed_record[9] = (rec->key[4] >> 16) & 0xFFFF;

	packed_record[10] = rec->key[5] & 0xFFFF;
	packed_record[11] = (rec->key[5] >> 16) & 0xFFFF;

	packed_record[12] = rec->key[6] & 0xFFFF;
	packed_record[13] = (rec->key[6] >> 16) & 0xFFFF;

	packed_record[14] = rec->key[7] & 0xFFFF;
	packed_record[15] = (rec->key[7] >> 16) & 0xFFFF;

	ret = set_raw_egress_record(hw, packed_record, 8, 2,
				    ROWOFFSET_EGRESSSAKEYRECORD + table_index);
	if (unlikely(ret))
		return ret;
	ret = set_raw_egress_record(hw, packed_record + 8, 8, 2,
				    ROWOFFSET_EGRESSSAKEYRECORD + table_index -
					    32);
	if (unlikely(ret))
		return ret;

	return 0;
}

int aq_mss_set_egress_sakey_record(struct aq_hw_s *hw,
				   const struct aq_mss_egress_sakey_record *rec,
				   u16 table_index)
{
	int err = AQ_API_CALL_SAFE(set_egress_sakey_record, hw, rec,
				   table_index);

	WARN_ONCE(err, "%s failed with %d\n", __func__, err);

	return err;
}

static int get_egress_sakey_record(struct aq_hw_s *hw,
				   struct aq_mss_egress_sakey_record *rec,
				   u16 table_index)
{
	u16 packed_record[16];
	int ret;

	if (table_index >= NUMROWS_EGRESSSAKEYRECORD)
		return -EINVAL;

	ret = get_raw_egress_record(hw, packed_record, 8, 2,
				    ROWOFFSET_EGRESSSAKEYRECORD + table_index);
	if (unlikely(ret))
		return ret;
	ret = get_raw_egress_record(hw, packed_record + 8, 8, 2,
				    ROWOFFSET_EGRESSSAKEYRECORD + table_index -
					    32);
	if (unlikely(ret))
		return ret;

	rec->key[0] = packed_record[0];
	rec->key[0] |= packed_record[1] << 16;

	rec->key[1] = packed_record[2];
	rec->key[1] |= packed_record[3] << 16;

	rec->key[2] = packed_record[4];
	rec->key[2] |= packed_record[5] << 16;

	rec->key[3] = packed_record[6];
	rec->key[3] |= packed_record[7] << 16;

	rec->key[4] = packed_record[8];
	rec->key[4] |= packed_record[9] << 16;

	rec->key[5] = packed_record[10];
	rec->key[5] |= packed_record[11] << 16;

	rec->key[6] = packed_record[12];
	rec->key[6] |= packed_record[13] << 16;

	rec->key[7] = packed_record[14];
	rec->key[7] |= packed_record[15] << 16;

	return 0;
}

int aq_mss_get_egress_sakey_record(struct aq_hw_s *hw,
				   struct aq_mss_egress_sakey_record *rec,
				   u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_egress_sakey_record, hw, rec, table_index);
}

static int get_egress_sa_expired(struct aq_hw_s *hw, u32 *expired)
{
	u16 val;
	int ret;

	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
			       MSS_EGRESS_SA_EXPIRED_STATUS_REGISTER_ADDR,
			       &val);
	if (unlikely(ret))
		return ret;

	*expired = val;

	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
			       MSS_EGRESS_SA_EXPIRED_STATUS_REGISTER_ADDR + 1,
			       &val);
	if (unlikely(ret))
		return ret;

	*expired |= val << 16;

	return 0;
}

int aq_mss_get_egress_sa_expired(struct aq_hw_s *hw, u32 *expired)
{
	*expired = 0;

	return AQ_API_CALL_SAFE(get_egress_sa_expired, hw, expired);
}

static int get_egress_sa_threshold_expired(struct aq_hw_s *hw,
					   u32 *expired)
{
	u16 val;
	int ret;

	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
		MSS_EGRESS_SA_THRESHOLD_EXPIRED_STATUS_REGISTER_ADDR, &val);
	if (unlikely(ret))
		return ret;

	*expired = val;

	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
		MSS_EGRESS_SA_THRESHOLD_EXPIRED_STATUS_REGISTER_ADDR + 1, &val);
	if (unlikely(ret))
		return ret;

	*expired |= val << 16;

	return 0;
}

int aq_mss_get_egress_sa_threshold_expired(struct aq_hw_s *hw,
					   u32 *expired)
{
	*expired = 0;

	return AQ_API_CALL_SAFE(get_egress_sa_threshold_expired, hw, expired);
}

static int set_egress_sa_expired(struct aq_hw_s *hw, u32 expired)
{
	int ret;

	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_SA_EXPIRED_STATUS_REGISTER_ADDR,
				expired & 0xFFFF);
	if (unlikely(ret))
		return ret;

	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_SA_EXPIRED_STATUS_REGISTER_ADDR + 1,
				expired >> 16);
	if (unlikely(ret))
		return ret;

	return 0;
}

int aq_mss_set_egress_sa_expired(struct aq_hw_s *hw, u32 expired)
{
	return AQ_API_CALL_SAFE(set_egress_sa_expired, hw, expired);
}

static int set_egress_sa_threshold_expired(struct aq_hw_s *hw, u32 expired)
{
	int ret;

	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
		MSS_EGRESS_SA_THRESHOLD_EXPIRED_STATUS_REGISTER_ADDR,
		expired & 0xFFFF);
	if (unlikely(ret))
		return ret;

	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
		MSS_EGRESS_SA_THRESHOLD_EXPIRED_STATUS_REGISTER_ADDR + 1,
		expired >> 16);
	if (unlikely(ret))
		return ret;

	return 0;
}

int aq_mss_set_egress_sa_threshold_expired(struct aq_hw_s *hw, u32 expired)
{
	return AQ_API_CALL_SAFE(set_egress_sa_threshold_expired, hw, expired);
}
