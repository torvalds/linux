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
	.mcs_tx_sa_mem_map_write	= cnf10kb_mcs_tx_sa_mem_map_write,
	.mcs_rx_sa_mem_map_write	= cnf10kb_mcs_rx_sa_mem_map_write,
	.mcs_flowid_secy_map		= cnf10kb_mcs_flowid_secy_map,
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

void cnf10kb_mcs_flowid_secy_map(struct mcs *mcs, struct secy_mem_map *map, int dir)
{
	u64 reg, val;

	val = (map->secy & 0x3F) | (map->ctrl_pkt & 0x1) << 6;
	if (dir == MCS_RX) {
		reg = MCSX_CPM_RX_SLAVE_SECY_MAP_MEMX(map->flow_id);
	} else {
		reg = MCSX_CPM_TX_SLAVE_SECY_MAP_MEM_0X(map->flow_id);
		mcs_reg_write(mcs, reg, map->sci);
		val |= (map->sc & 0x3F) << 7;
		reg = MCSX_CPM_TX_SLAVE_SECY_MAP_MEM_1X(map->flow_id);
	}

	mcs_reg_write(mcs, reg, val);
}

void cnf10kb_mcs_tx_sa_mem_map_write(struct mcs *mcs, struct mcs_tx_sc_sa_map *map)
{
	u64 reg, val;

	val = (map->sa_index0 & 0x7F) | (map->sa_index1 & 0x7F) << 7;

	reg = MCSX_CPM_TX_SLAVE_SA_MAP_MEM_0X(map->sc_id);
	mcs_reg_write(mcs, reg, val);

	if (map->rekey_ena) {
		reg = MCSX_CPM_TX_SLAVE_AUTO_REKEY_ENABLE_0;
		val = mcs_reg_read(mcs, reg);
		val |= BIT_ULL(map->sc_id);
		mcs_reg_write(mcs, reg, val);
	}

	if (map->sa_index0_vld)
		mcs_reg_write(mcs, MCSX_CPM_TX_SLAVE_SA_INDEX0_VLDX(map->sc_id), BIT_ULL(0));

	if (map->sa_index1_vld)
		mcs_reg_write(mcs, MCSX_CPM_TX_SLAVE_SA_INDEX1_VLDX(map->sc_id), BIT_ULL(0));

	mcs_reg_write(mcs, MCSX_CPM_TX_SLAVE_TX_SA_ACTIVEX(map->sc_id), map->tx_sa_active);
}

void cnf10kb_mcs_rx_sa_mem_map_write(struct mcs *mcs, struct mcs_rx_sc_sa_map *map)
{
	u64 val, reg;

	val = (map->sa_index & 0x7F) | (map->sa_in_use << 7);

	reg = MCSX_CPM_RX_SLAVE_SA_MAP_MEMX((4 * map->sc_id) + map->an);
	mcs_reg_write(mcs, reg, val);
}
