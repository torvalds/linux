// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/mhi_ep.h>

#include "internal.h"

u32 mhi_ep_mmio_read(struct mhi_ep_cntrl *mhi_cntrl, u32 offset)
{
	return readl(mhi_cntrl->mmio + offset);
}

void mhi_ep_mmio_write(struct mhi_ep_cntrl *mhi_cntrl, u32 offset, u32 val)
{
	writel(val, mhi_cntrl->mmio + offset);
}

void mhi_ep_mmio_masked_write(struct mhi_ep_cntrl *mhi_cntrl, u32 offset, u32 mask, u32 val)
{
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, offset);
	regval &= ~mask;
	regval |= (val << __ffs(mask)) & mask;
	mhi_ep_mmio_write(mhi_cntrl, offset, regval);
}

u32 mhi_ep_mmio_masked_read(struct mhi_ep_cntrl *dev, u32 offset, u32 mask)
{
	u32 regval;

	regval = mhi_ep_mmio_read(dev, offset);
	regval &= mask;
	regval >>= __ffs(mask);

	return regval;
}

void mhi_ep_mmio_get_mhi_state(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_state *state,
				bool *mhi_reset)
{
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_MHICTRL);
	*state = FIELD_GET(MHICTRL_MHISTATE_MASK, regval);
	*mhi_reset = !!FIELD_GET(MHICTRL_RESET_MASK, regval);
}

static void mhi_ep_mmio_set_chdb(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id, bool enable)
{
	u32 chid_mask, chid_shift, chdb_idx, val;

	chid_shift = ch_id % 32;
	chid_mask = BIT(chid_shift);
	chdb_idx = ch_id / 32;

	val = enable ? 1 : 0;

	mhi_ep_mmio_masked_write(mhi_cntrl, MHI_CHDB_INT_MASK_n(chdb_idx), chid_mask, val);

	/* Update the local copy of the channel mask */
	mhi_cntrl->chdb[chdb_idx].mask &= ~chid_mask;
	mhi_cntrl->chdb[chdb_idx].mask |= val << chid_shift;
}

void mhi_ep_mmio_enable_chdb(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id)
{
	mhi_ep_mmio_set_chdb(mhi_cntrl, ch_id, true);
}

void mhi_ep_mmio_disable_chdb(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id)
{
	mhi_ep_mmio_set_chdb(mhi_cntrl, ch_id, false);
}

static void mhi_ep_mmio_set_chdb_interrupts(struct mhi_ep_cntrl *mhi_cntrl, bool enable)
{
	u32 val, i;

	val = enable ? MHI_CHDB_INT_MASK_n_EN_ALL : 0;

	for (i = 0; i < MHI_MASK_ROWS_CH_DB; i++) {
		mhi_ep_mmio_write(mhi_cntrl, MHI_CHDB_INT_MASK_n(i), val);
		mhi_cntrl->chdb[i].mask = val;
	}
}

void mhi_ep_mmio_enable_chdb_interrupts(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_set_chdb_interrupts(mhi_cntrl, true);
}

static void mhi_ep_mmio_mask_chdb_interrupts(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_set_chdb_interrupts(mhi_cntrl, false);
}

bool mhi_ep_mmio_read_chdb_status_interrupts(struct mhi_ep_cntrl *mhi_cntrl)
{
	bool chdb = false;
	u32 i;

	for (i = 0; i < MHI_MASK_ROWS_CH_DB; i++) {
		mhi_cntrl->chdb[i].status = mhi_ep_mmio_read(mhi_cntrl, MHI_CHDB_INT_STATUS_n(i));
		if (mhi_cntrl->chdb[i].status)
			chdb = true;
	}

	/* Return whether a channel doorbell interrupt occurred or not */
	return chdb;
}

static void mhi_ep_mmio_set_erdb_interrupts(struct mhi_ep_cntrl *mhi_cntrl, bool enable)
{
	u32 val, i;

	val = enable ? MHI_ERDB_INT_MASK_n_EN_ALL : 0;

	for (i = 0; i < MHI_MASK_ROWS_EV_DB; i++)
		mhi_ep_mmio_write(mhi_cntrl, MHI_ERDB_INT_MASK_n(i), val);
}

static void mhi_ep_mmio_mask_erdb_interrupts(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_set_erdb_interrupts(mhi_cntrl, false);
}

void mhi_ep_mmio_enable_ctrl_interrupt(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_masked_write(mhi_cntrl, MHI_CTRL_INT_MASK,
				  MHI_CTRL_MHICTRL_MASK, 1);
}

void mhi_ep_mmio_disable_ctrl_interrupt(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_masked_write(mhi_cntrl, MHI_CTRL_INT_MASK,
				  MHI_CTRL_MHICTRL_MASK, 0);
}

void mhi_ep_mmio_enable_cmdb_interrupt(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_masked_write(mhi_cntrl, MHI_CTRL_INT_MASK,
				  MHI_CTRL_CRDB_MASK, 1);
}

void mhi_ep_mmio_disable_cmdb_interrupt(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_masked_write(mhi_cntrl, MHI_CTRL_INT_MASK,
				  MHI_CTRL_CRDB_MASK, 0);
}

void mhi_ep_mmio_mask_interrupts(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_disable_ctrl_interrupt(mhi_cntrl);
	mhi_ep_mmio_disable_cmdb_interrupt(mhi_cntrl);
	mhi_ep_mmio_mask_chdb_interrupts(mhi_cntrl);
	mhi_ep_mmio_mask_erdb_interrupts(mhi_cntrl);
}

static void mhi_ep_mmio_clear_interrupts(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 i;

	for (i = 0; i < MHI_MASK_ROWS_CH_DB; i++)
		mhi_ep_mmio_write(mhi_cntrl, MHI_CHDB_INT_CLEAR_n(i),
				   MHI_CHDB_INT_CLEAR_n_CLEAR_ALL);

	for (i = 0; i < MHI_MASK_ROWS_EV_DB; i++)
		mhi_ep_mmio_write(mhi_cntrl, MHI_ERDB_INT_CLEAR_n(i),
				   MHI_ERDB_INT_CLEAR_n_CLEAR_ALL);

	mhi_ep_mmio_write(mhi_cntrl, MHI_CTRL_INT_CLEAR,
			   MHI_CTRL_INT_MMIO_WR_CLEAR |
			   MHI_CTRL_INT_CRDB_CLEAR |
			   MHI_CTRL_INT_CRDB_MHICTRL_CLEAR);
}

void mhi_ep_mmio_get_chc_base(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_CCABAP_HIGHER);
	mhi_cntrl->ch_ctx_host_pa = regval;
	mhi_cntrl->ch_ctx_host_pa <<= 32;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_CCABAP_LOWER);
	mhi_cntrl->ch_ctx_host_pa |= regval;
}

void mhi_ep_mmio_get_erc_base(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_ECABAP_HIGHER);
	mhi_cntrl->ev_ctx_host_pa = regval;
	mhi_cntrl->ev_ctx_host_pa <<= 32;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_ECABAP_LOWER);
	mhi_cntrl->ev_ctx_host_pa |= regval;
}

void mhi_ep_mmio_get_crc_base(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_CRCBAP_HIGHER);
	mhi_cntrl->cmd_ctx_host_pa = regval;
	mhi_cntrl->cmd_ctx_host_pa <<= 32;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_CRCBAP_LOWER);
	mhi_cntrl->cmd_ctx_host_pa |= regval;
}

u64 mhi_ep_mmio_get_db(struct mhi_ep_ring *ring)
{
	struct mhi_ep_cntrl *mhi_cntrl = ring->mhi_cntrl;
	u64 db_offset;
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, ring->db_offset_h);
	db_offset = regval;
	db_offset <<= 32;

	regval = mhi_ep_mmio_read(mhi_cntrl, ring->db_offset_l);
	db_offset |= regval;

	return db_offset;
}

void mhi_ep_mmio_set_env(struct mhi_ep_cntrl *mhi_cntrl, u32 value)
{
	mhi_ep_mmio_write(mhi_cntrl, EP_BHI_EXECENV, value);
}

void mhi_ep_mmio_clear_reset(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_masked_write(mhi_cntrl, EP_MHICTRL, MHICTRL_RESET_MASK, 0);
}

void mhi_ep_mmio_reset(struct mhi_ep_cntrl *mhi_cntrl)
{
	mhi_ep_mmio_write(mhi_cntrl, EP_MHICTRL, 0);
	mhi_ep_mmio_write(mhi_cntrl, EP_MHISTATUS, 0);
	mhi_ep_mmio_clear_interrupts(mhi_cntrl);
}

void mhi_ep_mmio_init(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 regval;

	mhi_cntrl->chdb_offset = mhi_ep_mmio_read(mhi_cntrl, EP_CHDBOFF);
	mhi_cntrl->erdb_offset = mhi_ep_mmio_read(mhi_cntrl, EP_ERDBOFF);

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_MHICFG);
	mhi_cntrl->event_rings = FIELD_GET(MHICFG_NER_MASK, regval);
	mhi_cntrl->hw_event_rings = FIELD_GET(MHICFG_NHWER_MASK, regval);

	mhi_ep_mmio_reset(mhi_cntrl);
}

void mhi_ep_mmio_update_ner(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 regval;

	regval = mhi_ep_mmio_read(mhi_cntrl, EP_MHICFG);
	mhi_cntrl->event_rings = FIELD_GET(MHICFG_NER_MASK, regval);
	mhi_cntrl->hw_event_rings = FIELD_GET(MHICFG_NHWER_MASK, regval);
}
