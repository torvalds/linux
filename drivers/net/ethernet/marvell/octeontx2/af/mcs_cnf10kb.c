// SPDX-License-Identifier: GPL-2.0
/* Marvell MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#include "mcs.h"
#include "mcs_reg.h"

static struct mcs_ops cnf10kb_mcs_ops   = {
	.mcs_set_hw_capabilities	= cnf10kb_mcs_set_hw_capabilities,
	.mcs_parser_cfg			= cnf10kb_mcs_parser_cfg,
};

struct mcs_ops *cnf10kb_get_mac_ops(void)
{
	return &cnf10kb_mcs_ops;
}

void cnf10kb_mcs_set_hw_capabilities(struct mcs *mcs)
{
	struct hwinfo *hw = mcs->hw;

	hw->tcam_entries = 64;		/* TCAM entries */
	hw->secy_entries  = 64;		/* SecY entries */
	hw->sc_entries = 64;		/* SC CAM entries */
	hw->sa_entries = 128;		/* SA entries */
	hw->lmac_cnt = 4;		/* lmacs/ports per mcs block */
	hw->mcs_x2p_intf = 1;		/* x2p clabration intf */
	hw->mcs_blks = 7;		/* MCS blocks */
}

void cnf10kb_mcs_parser_cfg(struct mcs *mcs)
{
	u64 reg, val;

	/* VLAN Ctag */
	val = (0x8100ull & 0xFFFF) | BIT_ULL(20) | BIT_ULL(22);

	reg = MCSX_PEX_RX_SLAVE_CUSTOM_TAGX(0);
	mcs_reg_write(mcs, reg, val);

	reg = MCSX_PEX_TX_SLAVE_CUSTOM_TAGX(0);
	mcs_reg_write(mcs, reg, val);

	/* VLAN STag */
	val = (0x88a8ull & 0xFFFF) | BIT_ULL(20) | BIT_ULL(23);

	/* RX */
	reg = MCSX_PEX_RX_SLAVE_CUSTOM_TAGX(1);
	mcs_reg_write(mcs, reg, val);

	/* TX */
	reg = MCSX_PEX_TX_SLAVE_CUSTOM_TAGX(1);
	mcs_reg_write(mcs, reg, val);

	/* Enable custom tage 0 and 1 and sectag */
	val = BIT_ULL(0) | BIT_ULL(1) | BIT_ULL(12);

	reg = MCSX_PEX_RX_SLAVE_ETYPE_ENABLE;
	mcs_reg_write(mcs, reg, val);

	reg = MCSX_PEX_TX_SLAVE_ETYPE_ENABLE;
	mcs_reg_write(mcs, reg, val);
}
