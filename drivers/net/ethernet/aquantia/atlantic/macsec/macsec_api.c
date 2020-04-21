// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "macsec_api.h"
#include <linux/mdio.h>
#include "MSS_Ingress_registers.h"
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

static int set_raw_ingress_record(struct aq_hw_s *hw, u16 *packed_record,
				  u8 num_words, u8 table_id,
				  u16 table_index)
{
	struct mss_ingress_lut_addr_ctl_register lut_sel_reg;
	struct mss_ingress_lut_ctl_register lut_op_reg;

	unsigned int i;

	/* NOTE: MSS registers must always be read/written as adjacent pairs.
	 * For instance, to write either or both 1E.80A0 and 80A1, we have to:
	 * 1. Write 1E.80A0 first
	 * 2. Then write 1E.80A1
	 *
	 * For HHD devices: These writes need to be performed consecutively, and
	 * to ensure this we use the PIF mailbox to delegate the reads/writes to
	 * the FW.
	 *
	 * For EUR devices: Not need to use the PIF mailbox; it is safe to
	 * write to the registers directly.
	 */

	/* Write the packed record words to the data buffer registers. */
	for (i = 0; i < num_words; i += 2) {
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR + i,
				  packed_record[i]);
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR + i +
					  1,
				  packed_record[i + 1]);
	}

	/* Clear out the unused data buffer registers. */
	for (i = num_words; i < 24; i += 2) {
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				  MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR + i,
				  0);
		aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
			MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR + i + 1, 0);
	}

	/* Select the table and row index to write to */
	lut_sel_reg.bits_0.lut_select = table_id;
	lut_sel_reg.bits_0.lut_addr = table_index;

	lut_op_reg.bits_0.lut_read = 0;
	lut_op_reg.bits_0.lut_write = 1;

	aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
			  MSS_INGRESS_LUT_ADDR_CTL_REGISTER_ADDR,
			  lut_sel_reg.word_0);
	aq_mss_mdio_write(hw, MDIO_MMD_VEND1, MSS_INGRESS_LUT_CTL_REGISTER_ADDR,
			  lut_op_reg.word_0);

	return 0;
}

/*! Read the specified Ingress LUT table row.
 *  packed_record - [OUT] The table row data (raw).
 */
static int get_raw_ingress_record(struct aq_hw_s *hw, u16 *packed_record,
				  u8 num_words, u8 table_id,
				  u16 table_index)
{
	struct mss_ingress_lut_addr_ctl_register lut_sel_reg;
	struct mss_ingress_lut_ctl_register lut_op_reg;
	int ret;

	unsigned int i;

	/* Select the table and row index to read */
	lut_sel_reg.bits_0.lut_select = table_id;
	lut_sel_reg.bits_0.lut_addr = table_index;

	lut_op_reg.bits_0.lut_read = 1;
	lut_op_reg.bits_0.lut_write = 0;

	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_LUT_ADDR_CTL_REGISTER_ADDR,
				lut_sel_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_LUT_CTL_REGISTER_ADDR,
				lut_op_reg.word_0);
	if (unlikely(ret))
		return ret;

	memset(packed_record, 0, sizeof(u16) * num_words);

	for (i = 0; i < num_words; i += 2) {
		ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
				       MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR +
					       i,
				       &packed_record[i]);
		if (unlikely(ret))
			return ret;
		ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
				       MSS_INGRESS_LUT_DATA_CTL_REGISTER_ADDR +
					       i + 1,
				       &packed_record[i + 1]);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

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

static int
set_ingress_prectlf_record(struct aq_hw_s *hw,
			   const struct aq_mss_ingress_prectlf_record *rec,
			   u16 table_index)
{
	u16 packed_record[6];

	if (table_index >= NUMROWS_INGRESSPRECTLFRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 6);

	packed_record[0] = rec->sa_da[0] & 0xFFFF;
	packed_record[1] = (rec->sa_da[0] >> 16) & 0xFFFF;
	packed_record[2] = rec->sa_da[1] & 0xFFFF;
	packed_record[3] = rec->eth_type & 0xFFFF;
	packed_record[4] = rec->match_mask & 0xFFFF;
	packed_record[5] = rec->match_type & 0xF;
	packed_record[5] |= (rec->action & 0x1) << 4;

	return set_raw_ingress_record(hw, packed_record, 6, 0,
				      ROWOFFSET_INGRESSPRECTLFRECORD +
					      table_index);
}

int aq_mss_set_ingress_prectlf_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_prectlf_record *rec,
	u16 table_index)
{
	return AQ_API_CALL_SAFE(set_ingress_prectlf_record, hw, rec,
				table_index);
}

static int get_ingress_prectlf_record(struct aq_hw_s *hw,
				      struct aq_mss_ingress_prectlf_record *rec,
				      u16 table_index)
{
	u16 packed_record[6];
	int ret;

	if (table_index >= NUMROWS_INGRESSPRECTLFRECORD)
		return -EINVAL;

	/* If the row that we want to read is odd, first read the previous even
	 * row, throw that value away, and finally read the desired row.
	 * This is a workaround for EUR devices that allows us to read
	 * odd-numbered rows.  For HHD devices: this workaround will not work,
	 * so don't bother; odd-numbered rows are not readable.
	 */
	if ((table_index % 2) > 0) {
		ret = get_raw_ingress_record(hw, packed_record, 6, 0,
					     ROWOFFSET_INGRESSPRECTLFRECORD +
						     table_index - 1);
		if (unlikely(ret))
			return ret;
	}

	ret = get_raw_ingress_record(hw, packed_record, 6, 0,
				     ROWOFFSET_INGRESSPRECTLFRECORD +
					     table_index);
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

int aq_mss_get_ingress_prectlf_record(struct aq_hw_s *hw,
				      struct aq_mss_ingress_prectlf_record *rec,
				      u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_prectlf_record, hw, rec,
				table_index);
}

static int
set_ingress_preclass_record(struct aq_hw_s *hw,
			    const struct aq_mss_ingress_preclass_record *rec,
			    u16 table_index)
{
	u16 packed_record[20];

	if (table_index >= NUMROWS_INGRESSPRECLASSRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 20);

	packed_record[0] = rec->sci[0] & 0xFFFF;
	packed_record[1] = (rec->sci[0] >> 16) & 0xFFFF;

	packed_record[2] = rec->sci[1] & 0xFFFF;
	packed_record[3] = (rec->sci[1] >> 16) & 0xFFFF;

	packed_record[4] = rec->tci & 0xFF;

	packed_record[4] |= (rec->encr_offset & 0xFF) << 8;

	packed_record[5] = rec->eth_type & 0xFFFF;

	packed_record[6] = rec->snap[0] & 0xFFFF;
	packed_record[7] = (rec->snap[0] >> 16) & 0xFFFF;

	packed_record[8] = rec->snap[1] & 0xFF;

	packed_record[8] |= (rec->llc & 0xFF) << 8;
	packed_record[9] = (rec->llc >> 8) & 0xFFFF;

	packed_record[10] = rec->mac_sa[0] & 0xFFFF;
	packed_record[11] = (rec->mac_sa[0] >> 16) & 0xFFFF;

	packed_record[12] = rec->mac_sa[1] & 0xFFFF;

	packed_record[13] = rec->mac_da[0] & 0xFFFF;
	packed_record[14] = (rec->mac_da[0] >> 16) & 0xFFFF;

	packed_record[15] = rec->mac_da[1] & 0xFFFF;

	packed_record[16] = rec->lpbk_packet & 0x1;

	packed_record[16] |= (rec->an_mask & 0x3) << 1;

	packed_record[16] |= (rec->tci_mask & 0x3F) << 3;

	packed_record[16] |= (rec->sci_mask & 0x7F) << 9;
	packed_record[17] = (rec->sci_mask >> 7) & 0x1;

	packed_record[17] |= (rec->eth_type_mask & 0x3) << 1;

	packed_record[17] |= (rec->snap_mask & 0x1F) << 3;

	packed_record[17] |= (rec->llc_mask & 0x7) << 8;

	packed_record[17] |= (rec->_802_2_encapsulate & 0x1) << 11;

	packed_record[17] |= (rec->sa_mask & 0xF) << 12;
	packed_record[18] = (rec->sa_mask >> 4) & 0x3;

	packed_record[18] |= (rec->da_mask & 0x3F) << 2;

	packed_record[18] |= (rec->lpbk_mask & 0x1) << 8;

	packed_record[18] |= (rec->sc_idx & 0x1F) << 9;

	packed_record[18] |= (rec->proc_dest & 0x1) << 14;

	packed_record[18] |= (rec->action & 0x1) << 15;
	packed_record[19] = (rec->action >> 1) & 0x1;

	packed_record[19] |= (rec->ctrl_unctrl & 0x1) << 1;

	packed_record[19] |= (rec->sci_from_table & 0x1) << 2;

	packed_record[19] |= (rec->reserved & 0xF) << 3;

	packed_record[19] |= (rec->valid & 0x1) << 7;

	return set_raw_ingress_record(hw, packed_record, 20, 1,
				      ROWOFFSET_INGRESSPRECLASSRECORD +
					      table_index);
}

int aq_mss_set_ingress_preclass_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_preclass_record *rec,
	u16 table_index)
{
	int err = AQ_API_CALL_SAFE(set_ingress_preclass_record, hw, rec,
				   table_index);

	WARN_ONCE(err, "%s failed with %d\n", __func__, err);

	return err;
}

static int
get_ingress_preclass_record(struct aq_hw_s *hw,
			    struct aq_mss_ingress_preclass_record *rec,
			    u16 table_index)
{
	u16 packed_record[20];
	int ret;

	if (table_index >= NUMROWS_INGRESSPRECLASSRECORD)
		return -EINVAL;

	/* If the row that we want to read is odd, first read the previous even
	 * row, throw that value away, and finally read the desired row.
	 */
	if ((table_index % 2) > 0) {
		ret = get_raw_ingress_record(hw, packed_record, 20, 1,
					     ROWOFFSET_INGRESSPRECLASSRECORD +
						     table_index - 1);
		if (unlikely(ret))
			return ret;
	}

	ret = get_raw_ingress_record(hw, packed_record, 20, 1,
				     ROWOFFSET_INGRESSPRECLASSRECORD +
					     table_index);
	if (unlikely(ret))
		return ret;

	rec->sci[0] = packed_record[0];
	rec->sci[0] |= packed_record[1] << 16;

	rec->sci[1] = packed_record[2];
	rec->sci[1] |= packed_record[3] << 16;

	rec->tci = packed_record[4] & 0xFF;

	rec->encr_offset = (packed_record[4] >> 8) & 0xFF;

	rec->eth_type = packed_record[5];

	rec->snap[0] = packed_record[6];
	rec->snap[0] |= packed_record[7] << 16;

	rec->snap[1] = packed_record[8] & 0xFF;

	rec->llc = (packed_record[8] >> 8) & 0xFF;
	rec->llc |= packed_record[9] << 8;

	rec->mac_sa[0] = packed_record[10];
	rec->mac_sa[0] |= packed_record[11] << 16;

	rec->mac_sa[1] = packed_record[12];

	rec->mac_da[0] = packed_record[13];
	rec->mac_da[0] |= packed_record[14] << 16;

	rec->mac_da[1] = packed_record[15];

	rec->lpbk_packet = packed_record[16] & 0x1;

	rec->an_mask = (packed_record[16] >> 1) & 0x3;

	rec->tci_mask = (packed_record[16] >> 3) & 0x3F;

	rec->sci_mask = (packed_record[16] >> 9) & 0x7F;
	rec->sci_mask |= (packed_record[17] & 0x1) << 7;

	rec->eth_type_mask = (packed_record[17] >> 1) & 0x3;

	rec->snap_mask = (packed_record[17] >> 3) & 0x1F;

	rec->llc_mask = (packed_record[17] >> 8) & 0x7;

	rec->_802_2_encapsulate = (packed_record[17] >> 11) & 0x1;

	rec->sa_mask = (packed_record[17] >> 12) & 0xF;
	rec->sa_mask |= (packed_record[18] & 0x3) << 4;

	rec->da_mask = (packed_record[18] >> 2) & 0x3F;

	rec->lpbk_mask = (packed_record[18] >> 8) & 0x1;

	rec->sc_idx = (packed_record[18] >> 9) & 0x1F;

	rec->proc_dest = (packed_record[18] >> 14) & 0x1;

	rec->action = (packed_record[18] >> 15) & 0x1;
	rec->action |= (packed_record[19] & 0x1) << 1;

	rec->ctrl_unctrl = (packed_record[19] >> 1) & 0x1;

	rec->sci_from_table = (packed_record[19] >> 2) & 0x1;

	rec->reserved = (packed_record[19] >> 3) & 0xF;

	rec->valid = (packed_record[19] >> 7) & 0x1;

	return 0;
}

int aq_mss_get_ingress_preclass_record(struct aq_hw_s *hw,
	struct aq_mss_ingress_preclass_record *rec,
	u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_preclass_record, hw, rec,
				table_index);
}

static int set_ingress_sc_record(struct aq_hw_s *hw,
				 const struct aq_mss_ingress_sc_record *rec,
				 u16 table_index)
{
	u16 packed_record[8];

	if (table_index >= NUMROWS_INGRESSSCRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 8);

	packed_record[0] = rec->stop_time & 0xFFFF;
	packed_record[1] = (rec->stop_time >> 16) & 0xFFFF;

	packed_record[2] = rec->start_time & 0xFFFF;
	packed_record[3] = (rec->start_time >> 16) & 0xFFFF;

	packed_record[4] = rec->validate_frames & 0x3;

	packed_record[4] |= (rec->replay_protect & 0x1) << 2;

	packed_record[4] |= (rec->anti_replay_window & 0x1FFF) << 3;
	packed_record[5] = (rec->anti_replay_window >> 13) & 0xFFFF;
	packed_record[6] = (rec->anti_replay_window >> 29) & 0x7;

	packed_record[6] |= (rec->receiving & 0x1) << 3;

	packed_record[6] |= (rec->fresh & 0x1) << 4;

	packed_record[6] |= (rec->an_rol & 0x1) << 5;

	packed_record[6] |= (rec->reserved & 0x3FF) << 6;
	packed_record[7] = (rec->reserved >> 10) & 0x7FFF;

	packed_record[7] |= (rec->valid & 0x1) << 15;

	return set_raw_ingress_record(hw, packed_record, 8, 3,
				      ROWOFFSET_INGRESSSCRECORD + table_index);
}

int aq_mss_set_ingress_sc_record(struct aq_hw_s *hw,
				 const struct aq_mss_ingress_sc_record *rec,
				 u16 table_index)
{
	int err = AQ_API_CALL_SAFE(set_ingress_sc_record, hw, rec, table_index);

	WARN_ONCE(err, "%s failed with %d\n", __func__, err);

	return err;
}

static int get_ingress_sc_record(struct aq_hw_s *hw,
				 struct aq_mss_ingress_sc_record *rec,
				 u16 table_index)
{
	u16 packed_record[8];
	int ret;

	if (table_index >= NUMROWS_INGRESSSCRECORD)
		return -EINVAL;

	ret = get_raw_ingress_record(hw, packed_record, 8, 3,
				     ROWOFFSET_INGRESSSCRECORD + table_index);
	if (unlikely(ret))
		return ret;

	rec->stop_time = packed_record[0];
	rec->stop_time |= packed_record[1] << 16;

	rec->start_time = packed_record[2];
	rec->start_time |= packed_record[3] << 16;

	rec->validate_frames = packed_record[4] & 0x3;

	rec->replay_protect = (packed_record[4] >> 2) & 0x1;

	rec->anti_replay_window = (packed_record[4] >> 3) & 0x1FFF;
	rec->anti_replay_window |= packed_record[5] << 13;
	rec->anti_replay_window |= (packed_record[6] & 0x7) << 29;

	rec->receiving = (packed_record[6] >> 3) & 0x1;

	rec->fresh = (packed_record[6] >> 4) & 0x1;

	rec->an_rol = (packed_record[6] >> 5) & 0x1;

	rec->reserved = (packed_record[6] >> 6) & 0x3FF;
	rec->reserved |= (packed_record[7] & 0x7FFF) << 10;

	rec->valid = (packed_record[7] >> 15) & 0x1;

	return 0;
}

int aq_mss_get_ingress_sc_record(struct aq_hw_s *hw,
				 struct aq_mss_ingress_sc_record *rec,
				 u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_sc_record, hw, rec, table_index);
}

static int set_ingress_sa_record(struct aq_hw_s *hw,
				 const struct aq_mss_ingress_sa_record *rec,
				 u16 table_index)
{
	u16 packed_record[8];

	if (table_index >= NUMROWS_INGRESSSARECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 8);

	packed_record[0] = rec->stop_time & 0xFFFF;
	packed_record[1] = (rec->stop_time >> 16) & 0xFFFF;

	packed_record[2] = rec->start_time & 0xFFFF;
	packed_record[3] = (rec->start_time >> 16) & 0xFFFF;

	packed_record[4] = rec->next_pn & 0xFFFF;
	packed_record[5] = (rec->next_pn >> 16) & 0xFFFF;

	packed_record[6] = rec->sat_nextpn & 0x1;

	packed_record[6] |= (rec->in_use & 0x1) << 1;

	packed_record[6] |= (rec->fresh & 0x1) << 2;

	packed_record[6] |= (rec->reserved & 0x1FFF) << 3;
	packed_record[7] = (rec->reserved >> 13) & 0x7FFF;

	packed_record[7] |= (rec->valid & 0x1) << 15;

	return set_raw_ingress_record(hw, packed_record, 8, 3,
				      ROWOFFSET_INGRESSSARECORD + table_index);
}

int aq_mss_set_ingress_sa_record(struct aq_hw_s *hw,
				 const struct aq_mss_ingress_sa_record *rec,
				 u16 table_index)
{
	int err = AQ_API_CALL_SAFE(set_ingress_sa_record, hw, rec, table_index);

	WARN_ONCE(err, "%s failed with %d\n", __func__, err);

	return err;
}

static int get_ingress_sa_record(struct aq_hw_s *hw,
				 struct aq_mss_ingress_sa_record *rec,
				 u16 table_index)
{
	u16 packed_record[8];
	int ret;

	if (table_index >= NUMROWS_INGRESSSARECORD)
		return -EINVAL;

	ret = get_raw_ingress_record(hw, packed_record, 8, 3,
				     ROWOFFSET_INGRESSSARECORD + table_index);
	if (unlikely(ret))
		return ret;

	rec->stop_time = packed_record[0];
	rec->stop_time |= packed_record[1] << 16;

	rec->start_time = packed_record[2];
	rec->start_time |= packed_record[3] << 16;

	rec->next_pn = packed_record[4];
	rec->next_pn |= packed_record[5] << 16;

	rec->sat_nextpn = packed_record[6] & 0x1;

	rec->in_use = (packed_record[6] >> 1) & 0x1;

	rec->fresh = (packed_record[6] >> 2) & 0x1;

	rec->reserved = (packed_record[6] >> 3) & 0x1FFF;
	rec->reserved |= (packed_record[7] & 0x7FFF) << 13;

	rec->valid = (packed_record[7] >> 15) & 0x1;

	return 0;
}

int aq_mss_get_ingress_sa_record(struct aq_hw_s *hw,
				 struct aq_mss_ingress_sa_record *rec,
				 u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_sa_record, hw, rec, table_index);
}

static int
set_ingress_sakey_record(struct aq_hw_s *hw,
			 const struct aq_mss_ingress_sakey_record *rec,
			 u16 table_index)
{
	u16 packed_record[18];

	if (table_index >= NUMROWS_INGRESSSAKEYRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 18);

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

	packed_record[16] = rec->key_len & 0x3;

	return set_raw_ingress_record(hw, packed_record, 18, 2,
				      ROWOFFSET_INGRESSSAKEYRECORD +
					      table_index);
}

int aq_mss_set_ingress_sakey_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_sakey_record *rec,
	u16 table_index)
{
	int err = AQ_API_CALL_SAFE(set_ingress_sakey_record, hw, rec,
				   table_index);

	WARN_ONCE(err, "%s failed with %d\n", __func__, err);

	return err;
}

static int get_ingress_sakey_record(struct aq_hw_s *hw,
				    struct aq_mss_ingress_sakey_record *rec,
				    u16 table_index)
{
	u16 packed_record[18];
	int ret;

	if (table_index >= NUMROWS_INGRESSSAKEYRECORD)
		return -EINVAL;

	ret = get_raw_ingress_record(hw, packed_record, 18, 2,
				     ROWOFFSET_INGRESSSAKEYRECORD +
					     table_index);
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

	rec->key_len = (rec->key_len & 0xFFFFFFFC) |
		       (packed_record[16] & 0x3);

	return 0;
}

int aq_mss_get_ingress_sakey_record(struct aq_hw_s *hw,
				    struct aq_mss_ingress_sakey_record *rec,
				    u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_sakey_record, hw, rec, table_index);
}

static int
set_ingress_postclass_record(struct aq_hw_s *hw,
			     const struct aq_mss_ingress_postclass_record *rec,
			     u16 table_index)
{
	u16 packed_record[8];

	if (table_index >= NUMROWS_INGRESSPOSTCLASSRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 8);

	packed_record[0] = rec->byte0 & 0xFF;

	packed_record[0] |= (rec->byte1 & 0xFF) << 8;

	packed_record[1] = rec->byte2 & 0xFF;

	packed_record[1] |= (rec->byte3 & 0xFF) << 8;

	packed_record[2] = rec->eth_type & 0xFFFF;

	packed_record[3] = rec->eth_type_valid & 0x1;

	packed_record[3] |= (rec->vlan_id & 0xFFF) << 1;

	packed_record[3] |= (rec->vlan_up & 0x7) << 13;

	packed_record[4] = rec->vlan_valid & 0x1;

	packed_record[4] |= (rec->sai & 0x1F) << 1;

	packed_record[4] |= (rec->sai_hit & 0x1) << 6;

	packed_record[4] |= (rec->eth_type_mask & 0xF) << 7;

	packed_record[4] |= (rec->byte3_location & 0x1F) << 11;
	packed_record[5] = (rec->byte3_location >> 5) & 0x1;

	packed_record[5] |= (rec->byte3_mask & 0x3) << 1;

	packed_record[5] |= (rec->byte2_location & 0x3F) << 3;

	packed_record[5] |= (rec->byte2_mask & 0x3) << 9;

	packed_record[5] |= (rec->byte1_location & 0x1F) << 11;
	packed_record[6] = (rec->byte1_location >> 5) & 0x1;

	packed_record[6] |= (rec->byte1_mask & 0x3) << 1;

	packed_record[6] |= (rec->byte0_location & 0x3F) << 3;

	packed_record[6] |= (rec->byte0_mask & 0x3) << 9;

	packed_record[6] |= (rec->eth_type_valid_mask & 0x3) << 11;

	packed_record[6] |= (rec->vlan_id_mask & 0x7) << 13;
	packed_record[7] = (rec->vlan_id_mask >> 3) & 0x1;

	packed_record[7] |= (rec->vlan_up_mask & 0x3) << 1;

	packed_record[7] |= (rec->vlan_valid_mask & 0x3) << 3;

	packed_record[7] |= (rec->sai_mask & 0x3) << 5;

	packed_record[7] |= (rec->sai_hit_mask & 0x3) << 7;

	packed_record[7] |= (rec->firstlevel_actions & 0x1) << 9;

	packed_record[7] |= (rec->secondlevel_actions & 0x1) << 10;

	packed_record[7] |= (rec->reserved & 0xF) << 11;

	packed_record[7] |= (rec->valid & 0x1) << 15;

	return set_raw_ingress_record(hw, packed_record, 8, 4,
				      ROWOFFSET_INGRESSPOSTCLASSRECORD +
					      table_index);
}

int aq_mss_set_ingress_postclass_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_postclass_record *rec,
	u16 table_index)
{
	return AQ_API_CALL_SAFE(set_ingress_postclass_record, hw, rec,
				table_index);
}

static int
get_ingress_postclass_record(struct aq_hw_s *hw,
			     struct aq_mss_ingress_postclass_record *rec,
			     u16 table_index)
{
	u16 packed_record[8];
	int ret;

	if (table_index >= NUMROWS_INGRESSPOSTCLASSRECORD)
		return -EINVAL;

	/* If the row that we want to read is odd, first read the previous even
	 * row, throw that value away, and finally read the desired row.
	 */
	if ((table_index % 2) > 0) {
		ret = get_raw_ingress_record(hw, packed_record, 8, 4,
					     ROWOFFSET_INGRESSPOSTCLASSRECORD +
						     table_index - 1);
		if (unlikely(ret))
			return ret;
	}

	ret = get_raw_ingress_record(hw, packed_record, 8, 4,
				     ROWOFFSET_INGRESSPOSTCLASSRECORD +
					     table_index);
	if (unlikely(ret))
		return ret;

	rec->byte0 = packed_record[0] & 0xFF;

	rec->byte1 = (packed_record[0] >> 8) & 0xFF;

	rec->byte2 = packed_record[1] & 0xFF;

	rec->byte3 = (packed_record[1] >> 8) & 0xFF;

	rec->eth_type = packed_record[2];

	rec->eth_type_valid = packed_record[3] & 0x1;

	rec->vlan_id = (packed_record[3] >> 1) & 0xFFF;

	rec->vlan_up = (packed_record[3] >> 13) & 0x7;

	rec->vlan_valid = packed_record[4] & 0x1;

	rec->sai = (packed_record[4] >> 1) & 0x1F;

	rec->sai_hit = (packed_record[4] >> 6) & 0x1;

	rec->eth_type_mask = (packed_record[4] >> 7) & 0xF;

	rec->byte3_location = (packed_record[4] >> 11) & 0x1F;
	rec->byte3_location |= (packed_record[5] & 0x1) << 5;

	rec->byte3_mask = (packed_record[5] >> 1) & 0x3;

	rec->byte2_location = (packed_record[5] >> 3) & 0x3F;

	rec->byte2_mask = (packed_record[5] >> 9) & 0x3;

	rec->byte1_location = (packed_record[5] >> 11) & 0x1F;
	rec->byte1_location |= (packed_record[6] & 0x1) << 5;

	rec->byte1_mask = (packed_record[6] >> 1) & 0x3;

	rec->byte0_location = (packed_record[6] >> 3) & 0x3F;

	rec->byte0_mask = (packed_record[6] >> 9) & 0x3;

	rec->eth_type_valid_mask = (packed_record[6] >> 11) & 0x3;

	rec->vlan_id_mask = (packed_record[6] >> 13) & 0x7;
	rec->vlan_id_mask |= (packed_record[7] & 0x1) << 3;

	rec->vlan_up_mask = (packed_record[7] >> 1) & 0x3;

	rec->vlan_valid_mask = (packed_record[7] >> 3) & 0x3;

	rec->sai_mask = (packed_record[7] >> 5) & 0x3;

	rec->sai_hit_mask = (packed_record[7] >> 7) & 0x3;

	rec->firstlevel_actions = (packed_record[7] >> 9) & 0x1;

	rec->secondlevel_actions = (packed_record[7] >> 10) & 0x1;

	rec->reserved = (packed_record[7] >> 11) & 0xF;

	rec->valid = (packed_record[7] >> 15) & 0x1;

	return 0;
}

int aq_mss_get_ingress_postclass_record(struct aq_hw_s *hw,
	struct aq_mss_ingress_postclass_record *rec,
	u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_postclass_record, hw, rec,
				table_index);
}

static int
set_ingress_postctlf_record(struct aq_hw_s *hw,
			    const struct aq_mss_ingress_postctlf_record *rec,
			    u16 table_index)
{
	u16 packed_record[6];

	if (table_index >= NUMROWS_INGRESSPOSTCTLFRECORD)
		return -EINVAL;

	memset(packed_record, 0, sizeof(u16) * 6);

	packed_record[0] = rec->sa_da[0] & 0xFFFF;
	packed_record[1] = (rec->sa_da[0] >> 16) & 0xFFFF;

	packed_record[2] = rec->sa_da[1] & 0xFFFF;

	packed_record[3] = rec->eth_type & 0xFFFF;

	packed_record[4] = rec->match_mask & 0xFFFF;

	packed_record[5] = rec->match_type & 0xF;

	packed_record[5] |= (rec->action & 0x1) << 4;

	return set_raw_ingress_record(hw, packed_record, 6, 5,
				      ROWOFFSET_INGRESSPOSTCTLFRECORD +
					      table_index);
}

int aq_mss_set_ingress_postctlf_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_postctlf_record *rec,
	u16 table_index)
{
	return AQ_API_CALL_SAFE(set_ingress_postctlf_record, hw, rec,
				table_index);
}

static int
get_ingress_postctlf_record(struct aq_hw_s *hw,
			    struct aq_mss_ingress_postctlf_record *rec,
			    u16 table_index)
{
	u16 packed_record[6];
	int ret;

	if (table_index >= NUMROWS_INGRESSPOSTCTLFRECORD)
		return -EINVAL;

	/* If the row that we want to read is odd, first read the previous even
	 * row, throw that value away, and finally read the desired row.
	 */
	if ((table_index % 2) > 0) {
		ret = get_raw_ingress_record(hw, packed_record, 6, 5,
					     ROWOFFSET_INGRESSPOSTCTLFRECORD +
						     table_index - 1);
		if (unlikely(ret))
			return ret;
	}

	ret = get_raw_ingress_record(hw, packed_record, 6, 5,
				     ROWOFFSET_INGRESSPOSTCTLFRECORD +
					     table_index);
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

int aq_mss_get_ingress_postctlf_record(struct aq_hw_s *hw,
	struct aq_mss_ingress_postctlf_record *rec,
	u16 table_index)
{
	memset(rec, 0, sizeof(*rec));

	return AQ_API_CALL_SAFE(get_ingress_postctlf_record, hw, rec,
				table_index);
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

static int get_egress_sc_counters(struct aq_hw_s *hw,
				  struct aq_mss_egress_sc_counters *counters,
				  u16 sc_index)
{
	u16 packed_record[4];
	int ret;

	if (sc_index >= NUMROWS_EGRESSSCRECORD)
		return -EINVAL;

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sc_index * 8 + 4);
	if (unlikely(ret))
		return ret;
	counters->sc_protected_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sc_protected_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sc_index * 8 + 5);
	if (unlikely(ret))
		return ret;
	counters->sc_encrypted_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sc_encrypted_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sc_index * 8 + 6);
	if (unlikely(ret))
		return ret;
	counters->sc_protected_octets[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sc_protected_octets[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sc_index * 8 + 7);
	if (unlikely(ret))
		return ret;
	counters->sc_encrypted_octets[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sc_encrypted_octets[1] =
		packed_record[2] | (packed_record[3] << 16);

	return 0;
}

int aq_mss_get_egress_sc_counters(struct aq_hw_s *hw,
				  struct aq_mss_egress_sc_counters *counters,
				  u16 sc_index)
{
	memset(counters, 0, sizeof(*counters));

	return AQ_API_CALL_SAFE(get_egress_sc_counters, hw, counters, sc_index);
}

static int get_egress_sa_counters(struct aq_hw_s *hw,
				  struct aq_mss_egress_sa_counters *counters,
				  u16 sa_index)
{
	u16 packed_record[4];
	int ret;

	if (sa_index >= NUMROWS_EGRESSSARECORD)
		return -EINVAL;

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sa_index * 8 + 0);
	if (unlikely(ret))
		return ret;
	counters->sa_hit_drop_redirect[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sa_hit_drop_redirect[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sa_index * 8 + 1);
	if (unlikely(ret))
		return ret;
	counters->sa_protected2_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sa_protected2_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sa_index * 8 + 2);
	if (unlikely(ret))
		return ret;
	counters->sa_protected_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sa_protected_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, sa_index * 8 + 3);
	if (unlikely(ret))
		return ret;
	counters->sa_encrypted_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->sa_encrypted_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	return 0;
}

int aq_mss_get_egress_sa_counters(struct aq_hw_s *hw,
				  struct aq_mss_egress_sa_counters *counters,
				  u16 sa_index)
{
	memset(counters, 0, sizeof(*counters));

	return AQ_API_CALL_SAFE(get_egress_sa_counters, hw, counters, sa_index);
}

static int
get_egress_common_counters(struct aq_hw_s *hw,
			   struct aq_mss_egress_common_counters *counters)
{
	u16 packed_record[4];
	int ret;

	ret = get_raw_egress_record(hw, packed_record, 4, 3, 256 + 0);
	if (unlikely(ret))
		return ret;
	counters->ctl_pkt[0] = packed_record[0] | (packed_record[1] << 16);
	counters->ctl_pkt[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, 256 + 1);
	if (unlikely(ret))
		return ret;
	counters->unknown_sa_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unknown_sa_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, 256 + 2);
	if (unlikely(ret))
		return ret;
	counters->untagged_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->untagged_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, 256 + 3);
	if (unlikely(ret))
		return ret;
	counters->too_long[0] = packed_record[0] | (packed_record[1] << 16);
	counters->too_long[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, 256 + 4);
	if (unlikely(ret))
		return ret;
	counters->ecc_error_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->ecc_error_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_egress_record(hw, packed_record, 4, 3, 256 + 5);
	if (unlikely(ret))
		return ret;
	counters->unctrl_hit_drop_redir[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unctrl_hit_drop_redir[1] =
		packed_record[2] | (packed_record[3] << 16);

	return 0;
}

int aq_mss_get_egress_common_counters(struct aq_hw_s *hw,
	struct aq_mss_egress_common_counters *counters)
{
	memset(counters, 0, sizeof(*counters));

	return AQ_API_CALL_SAFE(get_egress_common_counters, hw, counters);
}

static int clear_egress_counters(struct aq_hw_s *hw)
{
	struct mss_egress_ctl_register ctl_reg;
	int ret;

	memset(&ctl_reg, 0, sizeof(ctl_reg));

	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1, MSS_EGRESS_CTL_REGISTER_ADDR,
			       &ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
			       MSS_EGRESS_CTL_REGISTER_ADDR + 4,
			       &ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	/* Toggle the Egress MIB clear bit 0->1->0 */
	ctl_reg.bits_0.clear_counter = 0;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_CTL_REGISTER_ADDR, ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_CTL_REGISTER_ADDR + 4,
				ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	ctl_reg.bits_0.clear_counter = 1;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_CTL_REGISTER_ADDR, ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_CTL_REGISTER_ADDR + 4,
				ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	ctl_reg.bits_0.clear_counter = 0;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_CTL_REGISTER_ADDR, ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_EGRESS_CTL_REGISTER_ADDR + 4,
				ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	return 0;
}

int aq_mss_clear_egress_counters(struct aq_hw_s *hw)
{
	return AQ_API_CALL_SAFE(clear_egress_counters, hw);
}

static int get_ingress_sa_counters(struct aq_hw_s *hw,
				   struct aq_mss_ingress_sa_counters *counters,
				   u16 sa_index)
{
	u16 packed_record[4];
	int ret;

	if (sa_index >= NUMROWS_INGRESSSARECORD)
		return -EINVAL;

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 0);
	if (unlikely(ret))
		return ret;
	counters->untagged_hit_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->untagged_hit_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 1);
	if (unlikely(ret))
		return ret;
	counters->ctrl_hit_drop_redir_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->ctrl_hit_drop_redir_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 2);
	if (unlikely(ret))
		return ret;
	counters->not_using_sa[0] = packed_record[0] | (packed_record[1] << 16);
	counters->not_using_sa[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 3);
	if (unlikely(ret))
		return ret;
	counters->unused_sa[0] = packed_record[0] | (packed_record[1] << 16);
	counters->unused_sa[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 4);
	if (unlikely(ret))
		return ret;
	counters->not_valid_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->not_valid_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 5);
	if (unlikely(ret))
		return ret;
	counters->invalid_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->invalid_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 6);
	if (unlikely(ret))
		return ret;
	counters->ok_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->ok_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 7);
	if (unlikely(ret))
		return ret;
	counters->late_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->late_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 8);
	if (unlikely(ret))
		return ret;
	counters->delayed_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->delayed_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 9);
	if (unlikely(ret))
		return ret;
	counters->unchecked_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unchecked_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 10);
	if (unlikely(ret))
		return ret;
	counters->validated_octets[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->validated_octets[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6,
				     sa_index * 12 + 11);
	if (unlikely(ret))
		return ret;
	counters->decrypted_octets[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->decrypted_octets[1] =
		packed_record[2] | (packed_record[3] << 16);

	return 0;
}

int aq_mss_get_ingress_sa_counters(struct aq_hw_s *hw,
				   struct aq_mss_ingress_sa_counters *counters,
				   u16 sa_index)
{
	memset(counters, 0, sizeof(*counters));

	return AQ_API_CALL_SAFE(get_ingress_sa_counters, hw, counters,
				sa_index);
}

static int
get_ingress_common_counters(struct aq_hw_s *hw,
			    struct aq_mss_ingress_common_counters *counters)
{
	u16 packed_record[4];
	int ret;

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 0);
	if (unlikely(ret))
		return ret;
	counters->ctl_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->ctl_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 1);
	if (unlikely(ret))
		return ret;
	counters->tagged_miss_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->tagged_miss_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 2);
	if (unlikely(ret))
		return ret;
	counters->untagged_miss_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->untagged_miss_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 3);
	if (unlikely(ret))
		return ret;
	counters->notag_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->notag_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 4);
	if (unlikely(ret))
		return ret;
	counters->untagged_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->untagged_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 5);
	if (unlikely(ret))
		return ret;
	counters->bad_tag_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->bad_tag_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 6);
	if (unlikely(ret))
		return ret;
	counters->no_sci_pkts[0] = packed_record[0] | (packed_record[1] << 16);
	counters->no_sci_pkts[1] = packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 7);
	if (unlikely(ret))
		return ret;
	counters->unknown_sci_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unknown_sci_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 8);
	if (unlikely(ret))
		return ret;
	counters->ctrl_prt_pass_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->ctrl_prt_pass_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 9);
	if (unlikely(ret))
		return ret;
	counters->unctrl_prt_pass_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unctrl_prt_pass_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 10);
	if (unlikely(ret))
		return ret;
	counters->ctrl_prt_fail_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->ctrl_prt_fail_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 11);
	if (unlikely(ret))
		return ret;
	counters->unctrl_prt_fail_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unctrl_prt_fail_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 12);
	if (unlikely(ret))
		return ret;
	counters->too_long_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->too_long_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 13);
	if (unlikely(ret))
		return ret;
	counters->igpoc_ctl_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->igpoc_ctl_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 14);
	if (unlikely(ret))
		return ret;
	counters->ecc_error_pkts[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->ecc_error_pkts[1] =
		packed_record[2] | (packed_record[3] << 16);

	ret = get_raw_ingress_record(hw, packed_record, 4, 6, 385 + 15);
	if (unlikely(ret))
		return ret;
	counters->unctrl_hit_drop_redir[0] =
		packed_record[0] | (packed_record[1] << 16);
	counters->unctrl_hit_drop_redir[1] =
		packed_record[2] | (packed_record[3] << 16);

	return 0;
}

int aq_mss_get_ingress_common_counters(struct aq_hw_s *hw,
	struct aq_mss_ingress_common_counters *counters)
{
	memset(counters, 0, sizeof(*counters));

	return AQ_API_CALL_SAFE(get_ingress_common_counters, hw, counters);
}

static int clear_ingress_counters(struct aq_hw_s *hw)
{
	struct mss_ingress_ctl_register ctl_reg;
	int ret;

	memset(&ctl_reg, 0, sizeof(ctl_reg));

	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
			       MSS_INGRESS_CTL_REGISTER_ADDR, &ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_read(hw, MDIO_MMD_VEND1,
			       MSS_INGRESS_CTL_REGISTER_ADDR + 4,
			       &ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	/* Toggle the Ingress MIB clear bit 0->1->0 */
	ctl_reg.bits_0.clear_count = 0;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_CTL_REGISTER_ADDR, ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_CTL_REGISTER_ADDR + 4,
				ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	ctl_reg.bits_0.clear_count = 1;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_CTL_REGISTER_ADDR, ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_CTL_REGISTER_ADDR + 4,
				ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	ctl_reg.bits_0.clear_count = 0;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_CTL_REGISTER_ADDR, ctl_reg.word_0);
	if (unlikely(ret))
		return ret;
	ret = aq_mss_mdio_write(hw, MDIO_MMD_VEND1,
				MSS_INGRESS_CTL_REGISTER_ADDR + 4,
				ctl_reg.word_1);
	if (unlikely(ret))
		return ret;

	return 0;
}

int aq_mss_clear_ingress_counters(struct aq_hw_s *hw)
{
	return AQ_API_CALL_SAFE(clear_ingress_counters, hw);
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
