// SPDX-License-Identifier: GPL-2.0
/* Marvell MACSEC hardware offload driver
 *
 * Copyright (C) 2022 Marvell.
 */

#include <linux/rtnetlink.h>
#include <linux/bitfield.h>
#include <net/macsec.h>
#include "otx2_common.h"

#define MCS_TCAM0_MAC_DA_MASK		GENMASK_ULL(47, 0)
#define MCS_TCAM0_MAC_SA_MASK		GENMASK_ULL(63, 48)
#define MCS_TCAM1_MAC_SA_MASK		GENMASK_ULL(31, 0)
#define MCS_TCAM1_ETYPE_MASK		GENMASK_ULL(47, 32)

#define MCS_SA_MAP_MEM_SA_USE		BIT_ULL(9)

#define MCS_RX_SECY_PLCY_RW_MASK	GENMASK_ULL(49, 18)
#define MCS_RX_SECY_PLCY_RP		BIT_ULL(17)
#define MCS_RX_SECY_PLCY_AUTH_ENA	BIT_ULL(16)
#define MCS_RX_SECY_PLCY_CIP		GENMASK_ULL(8, 5)
#define MCS_RX_SECY_PLCY_VAL		GENMASK_ULL(2, 1)
#define MCS_RX_SECY_PLCY_ENA		BIT_ULL(0)

#define MCS_TX_SECY_PLCY_MTU		GENMASK_ULL(43, 28)
#define MCS_TX_SECY_PLCY_ST_TCI		GENMASK_ULL(27, 22)
#define MCS_TX_SECY_PLCY_ST_OFFSET	GENMASK_ULL(21, 15)
#define MCS_TX_SECY_PLCY_INS_MODE	BIT_ULL(14)
#define MCS_TX_SECY_PLCY_AUTH_ENA	BIT_ULL(13)
#define MCS_TX_SECY_PLCY_CIP		GENMASK_ULL(5, 2)
#define MCS_TX_SECY_PLCY_PROTECT	BIT_ULL(1)
#define MCS_TX_SECY_PLCY_ENA		BIT_ULL(0)

#define MCS_GCM_AES_128			0
#define MCS_GCM_AES_256			1
#define MCS_GCM_AES_XPN_128		2
#define MCS_GCM_AES_XPN_256		3

#define MCS_TCI_ES			0x40 /* end station */
#define MCS_TCI_SC			0x20 /* SCI present */
#define MCS_TCI_SCB			0x10 /* epon */
#define MCS_TCI_E			0x08 /* encryption */
#define MCS_TCI_C			0x04 /* changed text */

static struct cn10k_mcs_txsc *cn10k_mcs_get_txsc(struct cn10k_mcs_cfg *cfg,
						 struct macsec_secy *secy)
{
	struct cn10k_mcs_txsc *txsc;

	list_for_each_entry(txsc, &cfg->txsc_list, entry) {
		if (txsc->sw_secy == secy)
			return txsc;
	}

	return NULL;
}

static struct cn10k_mcs_rxsc *cn10k_mcs_get_rxsc(struct cn10k_mcs_cfg *cfg,
						 struct macsec_secy *secy,
						 struct macsec_rx_sc *rx_sc)
{
	struct cn10k_mcs_rxsc *rxsc;

	list_for_each_entry(rxsc, &cfg->rxsc_list, entry) {
		if (rxsc->sw_rxsc == rx_sc && rxsc->sw_secy == secy)
			return rxsc;
	}

	return NULL;
}

static const char *rsrc_name(enum mcs_rsrc_type rsrc_type)
{
	switch (rsrc_type) {
	case MCS_RSRC_TYPE_FLOWID:
		return "FLOW";
	case MCS_RSRC_TYPE_SC:
		return "SC";
	case MCS_RSRC_TYPE_SECY:
		return "SECY";
	case MCS_RSRC_TYPE_SA:
		return "SA";
	default:
		return "Unknown";
	};

	return "Unknown";
}

static int cn10k_mcs_alloc_rsrc(struct otx2_nic *pfvf, enum mcs_direction dir,
				enum mcs_rsrc_type type, u16 *rsrc_id)
{
	struct mbox *mbox = &pfvf->mbox;
	struct mcs_alloc_rsrc_req *req;
	struct mcs_alloc_rsrc_rsp *rsp;
	int ret = -ENOMEM;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_alloc_resources(mbox);
	if (!req)
		goto fail;

	req->rsrc_type = type;
	req->rsrc_cnt  = 1;
	req->dir = dir;

	ret = otx2_sync_mbox_msg(mbox);
	if (ret)
		goto fail;

	rsp = (struct mcs_alloc_rsrc_rsp *)otx2_mbox_get_rsp(&pfvf->mbox.mbox,
							     0, &req->hdr);
	if (IS_ERR(rsp) || req->rsrc_cnt != rsp->rsrc_cnt ||
	    req->rsrc_type != rsp->rsrc_type || req->dir != rsp->dir) {
		ret = -EINVAL;
		goto fail;
	}

	switch (rsp->rsrc_type) {
	case MCS_RSRC_TYPE_FLOWID:
		*rsrc_id = rsp->flow_ids[0];
		break;
	case MCS_RSRC_TYPE_SC:
		*rsrc_id = rsp->sc_ids[0];
		break;
	case MCS_RSRC_TYPE_SECY:
		*rsrc_id = rsp->secy_ids[0];
		break;
	case MCS_RSRC_TYPE_SA:
		*rsrc_id = rsp->sa_ids[0];
		break;
	default:
		ret = -EINVAL;
		goto fail;
	}

	mutex_unlock(&mbox->lock);

	return 0;
fail:
	dev_err(pfvf->dev, "Failed to allocate %s %s resource\n",
		dir == MCS_TX ? "TX" : "RX", rsrc_name(type));
	mutex_unlock(&mbox->lock);
	return ret;
}

static void cn10k_mcs_free_rsrc(struct otx2_nic *pfvf, enum mcs_direction dir,
				enum mcs_rsrc_type type, u16 hw_rsrc_id,
				bool all)
{
	struct mcs_clear_stats *clear_req;
	struct mbox *mbox = &pfvf->mbox;
	struct mcs_free_rsrc_req *req;

	mutex_lock(&mbox->lock);

	clear_req = otx2_mbox_alloc_msg_mcs_clear_stats(mbox);
	if (!clear_req)
		goto fail;

	clear_req->id = hw_rsrc_id;
	clear_req->type = type;
	clear_req->dir = dir;

	req = otx2_mbox_alloc_msg_mcs_free_resources(mbox);
	if (!req)
		goto fail;

	req->rsrc_id = hw_rsrc_id;
	req->rsrc_type = type;
	req->dir = dir;
	if (all)
		req->all = 1;

	if (otx2_sync_mbox_msg(&pfvf->mbox))
		goto fail;

	mutex_unlock(&mbox->lock);

	return;
fail:
	dev_err(pfvf->dev, "Failed to free %s %s resource\n",
		dir == MCS_TX ? "TX" : "RX", rsrc_name(type));
	mutex_unlock(&mbox->lock);
}

static int cn10k_mcs_alloc_txsa(struct otx2_nic *pfvf, u16 *hw_sa_id)
{
	return cn10k_mcs_alloc_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SA, hw_sa_id);
}

static int cn10k_mcs_alloc_rxsa(struct otx2_nic *pfvf, u16 *hw_sa_id)
{
	return cn10k_mcs_alloc_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SA, hw_sa_id);
}

static void cn10k_mcs_free_txsa(struct otx2_nic *pfvf, u16 hw_sa_id)
{
	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SA, hw_sa_id, false);
}

static void cn10k_mcs_free_rxsa(struct otx2_nic *pfvf, u16 hw_sa_id)
{
	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SA, hw_sa_id, false);
}

static int cn10k_mcs_write_rx_secy(struct otx2_nic *pfvf,
				   struct macsec_secy *secy, u8 hw_secy_id)
{
	struct mcs_secy_plcy_write_req *req;
	struct mbox *mbox = &pfvf->mbox;
	u64 policy;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_secy_plcy_write(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	policy = FIELD_PREP(MCS_RX_SECY_PLCY_RW_MASK, secy->replay_window);
	if (secy->replay_protect)
		policy |= MCS_RX_SECY_PLCY_RP;

	policy |= MCS_RX_SECY_PLCY_AUTH_ENA;
	policy |= FIELD_PREP(MCS_RX_SECY_PLCY_CIP, MCS_GCM_AES_128);
	policy |= FIELD_PREP(MCS_RX_SECY_PLCY_VAL, secy->validate_frames);

	policy |= MCS_RX_SECY_PLCY_ENA;

	req->plcy = policy;
	req->secy_id = hw_secy_id;
	req->dir = MCS_RX;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_rx_flowid(struct otx2_nic *pfvf,
				     struct cn10k_mcs_rxsc *rxsc, u8 hw_secy_id)
{
	struct macsec_rx_sc *sw_rx_sc = rxsc->sw_rxsc;
	struct macsec_secy *secy = rxsc->sw_secy;
	struct mcs_flowid_entry_write_req *req;
	struct mbox *mbox = &pfvf->mbox;
	u64 mac_da;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_flowid_entry_write(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	mac_da = ether_addr_to_u64(secy->netdev->dev_addr);

	req->data[0] = FIELD_PREP(MCS_TCAM0_MAC_DA_MASK, mac_da);
	req->mask[0] = ~0ULL;
	req->mask[0] = ~MCS_TCAM0_MAC_DA_MASK;

	req->data[1] = FIELD_PREP(MCS_TCAM1_ETYPE_MASK, ETH_P_MACSEC);
	req->mask[1] = ~0ULL;
	req->mask[1] &= ~MCS_TCAM1_ETYPE_MASK;

	req->mask[2] = ~0ULL;
	req->mask[3] = ~0ULL;

	req->flow_id = rxsc->hw_flow_id;
	req->secy_id = hw_secy_id;
	req->sc_id = rxsc->hw_sc_id;
	req->dir = MCS_RX;

	if (sw_rx_sc->active)
		req->ena = 1;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_sc_cam(struct otx2_nic *pfvf,
				  struct cn10k_mcs_rxsc *rxsc, u8 hw_secy_id)
{
	struct macsec_rx_sc *sw_rx_sc = rxsc->sw_rxsc;
	struct mcs_rx_sc_cam_write_req *sc_req;
	struct mbox *mbox = &pfvf->mbox;
	int ret;

	mutex_lock(&mbox->lock);

	sc_req = otx2_mbox_alloc_msg_mcs_rx_sc_cam_write(mbox);
	if (!sc_req) {
		ret = -ENOMEM;
		goto fail;
	}

	sc_req->sci = (__force u64)cpu_to_be64((__force u64)sw_rx_sc->sci);
	sc_req->sc_id = rxsc->hw_sc_id;
	sc_req->secy_id = hw_secy_id;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_rx_sa_plcy(struct otx2_nic *pfvf,
				      struct macsec_secy *secy,
				      struct cn10k_mcs_rxsc *rxsc,
				      u8 assoc_num, bool sa_in_use)
{
	unsigned char *src = rxsc->sa_key[assoc_num];
	struct mcs_sa_plcy_write_req *plcy_req;
	struct mcs_rx_sc_sa_map *map_req;
	struct mbox *mbox = &pfvf->mbox;
	u8 reg, key_len;
	int ret;

	mutex_lock(&mbox->lock);

	plcy_req = otx2_mbox_alloc_msg_mcs_sa_plcy_write(mbox);
	if (!plcy_req) {
		ret = -ENOMEM;
		goto fail;
	}

	map_req = otx2_mbox_alloc_msg_mcs_rx_sc_sa_map_write(mbox);
	if (!map_req) {
		otx2_mbox_reset(&mbox->mbox, 0);
		ret = -ENOMEM;
		goto fail;
	}

	for (reg = 0, key_len = 0; key_len < secy->key_len; key_len += 8) {
		memcpy((u8 *)&plcy_req->plcy[0][reg],
		       (src + reg * 8), 8);
		reg++;
	}

	plcy_req->sa_index[0] = rxsc->hw_sa_id[assoc_num];
	plcy_req->sa_cnt = 1;
	plcy_req->dir = MCS_RX;

	map_req->sa_index = rxsc->hw_sa_id[assoc_num];
	map_req->sa_in_use = sa_in_use;
	map_req->sc_id = rxsc->hw_sc_id;
	map_req->an = assoc_num;

	/* Send two messages together */
	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_rx_sa_pn(struct otx2_nic *pfvf,
				    struct cn10k_mcs_rxsc *rxsc,
				    u8 assoc_num, u64 next_pn)
{
	struct mcs_pn_table_write_req *req;
	struct mbox *mbox = &pfvf->mbox;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_pn_table_write(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	req->pn_id = rxsc->hw_sa_id[assoc_num];
	req->next_pn = next_pn;
	req->dir = MCS_RX;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_tx_secy(struct otx2_nic *pfvf,
				   struct macsec_secy *secy,
				   struct cn10k_mcs_txsc *txsc)
{
	struct mcs_secy_plcy_write_req *req;
	struct mbox *mbox = &pfvf->mbox;
	struct macsec_tx_sc *sw_tx_sc;
	/* Insert SecTag after 12 bytes (DA+SA)*/
	u8 tag_offset = 12;
	u8 sectag_tci = 0;
	u64 policy;
	int ret;

	sw_tx_sc = &secy->tx_sc;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_secy_plcy_write(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	if (sw_tx_sc->send_sci) {
		sectag_tci |= MCS_TCI_SC;
	} else {
		if (sw_tx_sc->end_station)
			sectag_tci |= MCS_TCI_ES;
		if (sw_tx_sc->scb)
			sectag_tci |= MCS_TCI_SCB;
	}

	if (sw_tx_sc->encrypt)
		sectag_tci |= (MCS_TCI_E | MCS_TCI_C);

	policy = FIELD_PREP(MCS_TX_SECY_PLCY_MTU, secy->netdev->mtu);
	/* Write SecTag excluding AN bits(1..0) */
	policy |= FIELD_PREP(MCS_TX_SECY_PLCY_ST_TCI, sectag_tci >> 2);
	policy |= FIELD_PREP(MCS_TX_SECY_PLCY_ST_OFFSET, tag_offset);
	policy |= MCS_TX_SECY_PLCY_INS_MODE;
	policy |= MCS_TX_SECY_PLCY_AUTH_ENA;
	policy |= FIELD_PREP(MCS_TX_SECY_PLCY_CIP, MCS_GCM_AES_128);

	if (secy->protect_frames)
		policy |= MCS_TX_SECY_PLCY_PROTECT;

	/* If the encodingsa does not exist/active and protect is
	 * not set then frames can be sent out as it is. Hence enable
	 * the policy irrespective of secy operational when !protect.
	 */
	if (!secy->protect_frames || secy->operational)
		policy |= MCS_TX_SECY_PLCY_ENA;

	req->plcy = policy;
	req->secy_id = txsc->hw_secy_id_tx;
	req->dir = MCS_TX;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_tx_flowid(struct otx2_nic *pfvf,
				     struct macsec_secy *secy,
				     struct cn10k_mcs_txsc *txsc)
{
	struct mcs_flowid_entry_write_req *req;
	struct mbox *mbox = &pfvf->mbox;
	u64 mac_sa;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_flowid_entry_write(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	mac_sa = ether_addr_to_u64(secy->netdev->dev_addr);

	req->data[0] = FIELD_PREP(MCS_TCAM0_MAC_SA_MASK, mac_sa);
	req->data[1] = FIELD_PREP(MCS_TCAM1_MAC_SA_MASK, mac_sa >> 16);

	req->mask[0] = ~0ULL;
	req->mask[0] &= ~MCS_TCAM0_MAC_SA_MASK;

	req->mask[1] = ~0ULL;
	req->mask[1] &= ~MCS_TCAM1_MAC_SA_MASK;

	req->mask[2] = ~0ULL;
	req->mask[3] = ~0ULL;

	req->flow_id = txsc->hw_flow_id;
	req->secy_id = txsc->hw_secy_id_tx;
	req->sc_id = txsc->hw_sc_id;
	req->sci = (__force u64)cpu_to_be64((__force u64)secy->sci);
	req->dir = MCS_TX;
	/* This can be enabled since stack xmits packets only when interface is up */
	req->ena = 1;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_link_tx_sa2sc(struct otx2_nic *pfvf,
				   struct macsec_secy *secy,
				   struct cn10k_mcs_txsc *txsc,
				   u8 sa_num, bool sa_active)
{
	struct mcs_tx_sc_sa_map *map_req;
	struct mbox *mbox = &pfvf->mbox;
	int ret;

	/* Link the encoding_sa only to SC out of all SAs */
	if (txsc->encoding_sa != sa_num)
		return 0;

	mutex_lock(&mbox->lock);

	map_req = otx2_mbox_alloc_msg_mcs_tx_sc_sa_map_write(mbox);
	if (!map_req) {
		otx2_mbox_reset(&mbox->mbox, 0);
		ret = -ENOMEM;
		goto fail;
	}

	map_req->sa_index0 = txsc->hw_sa_id[sa_num];
	map_req->sa_index0_vld = sa_active;
	map_req->sectag_sci = (__force u64)cpu_to_be64((__force u64)secy->sci);
	map_req->sc_id = txsc->hw_sc_id;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_write_tx_sa_plcy(struct otx2_nic *pfvf,
				      struct macsec_secy *secy,
				      struct cn10k_mcs_txsc *txsc,
				      u8 assoc_num)
{
	unsigned char *src = txsc->sa_key[assoc_num];
	struct mcs_sa_plcy_write_req *plcy_req;
	struct mbox *mbox = &pfvf->mbox;
	u8 reg, key_len;
	int ret;

	mutex_lock(&mbox->lock);

	plcy_req = otx2_mbox_alloc_msg_mcs_sa_plcy_write(mbox);
	if (!plcy_req) {
		ret = -ENOMEM;
		goto fail;
	}

	for (reg = 0, key_len = 0; key_len < secy->key_len; key_len += 8) {
		memcpy((u8 *)&plcy_req->plcy[0][reg], (src + reg * 8), 8);
		reg++;
	}

	plcy_req->plcy[0][8] = assoc_num;
	plcy_req->sa_index[0] = txsc->hw_sa_id[assoc_num];
	plcy_req->sa_cnt = 1;
	plcy_req->dir = MCS_TX;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_write_tx_sa_pn(struct otx2_nic *pfvf,
				struct cn10k_mcs_txsc *txsc,
				u8 assoc_num, u64 next_pn)
{
	struct mcs_pn_table_write_req *req;
	struct mbox *mbox = &pfvf->mbox;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_pn_table_write(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	req->pn_id = txsc->hw_sa_id[assoc_num];
	req->next_pn = next_pn;
	req->dir = MCS_TX;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_ena_dis_flowid(struct otx2_nic *pfvf, u16 hw_flow_id,
				    bool enable, enum mcs_direction dir)
{
	struct mcs_flowid_ena_dis_entry *req;
	struct mbox *mbox = &pfvf->mbox;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_flowid_ena_entry(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	req->flow_id = hw_flow_id;
	req->ena = enable;
	req->dir = dir;

	ret = otx2_sync_mbox_msg(mbox);

fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_sa_stats(struct otx2_nic *pfvf, u8 hw_sa_id,
			      struct mcs_sa_stats *rsp_p,
			      enum mcs_direction dir, bool clear)
{
	struct mcs_clear_stats *clear_req;
	struct mbox *mbox = &pfvf->mbox;
	struct mcs_stats_req *req;
	struct mcs_sa_stats *rsp;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_get_sa_stats(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	req->id = hw_sa_id;
	req->dir = dir;

	if (!clear)
		goto send_msg;

	clear_req = otx2_mbox_alloc_msg_mcs_clear_stats(mbox);
	if (!clear_req) {
		ret = -ENOMEM;
		goto fail;
	}
	clear_req->id = hw_sa_id;
	clear_req->dir = dir;
	clear_req->type = MCS_RSRC_TYPE_SA;

send_msg:
	ret = otx2_sync_mbox_msg(mbox);
	if (ret)
		goto fail;

	rsp = (struct mcs_sa_stats *)otx2_mbox_get_rsp(&pfvf->mbox.mbox,
						       0, &req->hdr);
	if (IS_ERR(rsp)) {
		ret = PTR_ERR(rsp);
		goto fail;
	}

	memcpy(rsp_p, rsp, sizeof(*rsp_p));

	mutex_unlock(&mbox->lock);

	return 0;
fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_sc_stats(struct otx2_nic *pfvf, u8 hw_sc_id,
			      struct mcs_sc_stats *rsp_p,
			      enum mcs_direction dir, bool clear)
{
	struct mcs_clear_stats *clear_req;
	struct mbox *mbox = &pfvf->mbox;
	struct mcs_stats_req *req;
	struct mcs_sc_stats *rsp;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_get_sc_stats(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	req->id = hw_sc_id;
	req->dir = dir;

	if (!clear)
		goto send_msg;

	clear_req = otx2_mbox_alloc_msg_mcs_clear_stats(mbox);
	if (!clear_req) {
		ret = -ENOMEM;
		goto fail;
	}
	clear_req->id = hw_sc_id;
	clear_req->dir = dir;
	clear_req->type = MCS_RSRC_TYPE_SC;

send_msg:
	ret = otx2_sync_mbox_msg(mbox);
	if (ret)
		goto fail;

	rsp = (struct mcs_sc_stats *)otx2_mbox_get_rsp(&pfvf->mbox.mbox,
						       0, &req->hdr);
	if (IS_ERR(rsp)) {
		ret = PTR_ERR(rsp);
		goto fail;
	}

	memcpy(rsp_p, rsp, sizeof(*rsp_p));

	mutex_unlock(&mbox->lock);

	return 0;
fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static int cn10k_mcs_secy_stats(struct otx2_nic *pfvf, u8 hw_secy_id,
				struct mcs_secy_stats *rsp_p,
				enum mcs_direction dir, bool clear)
{
	struct mcs_clear_stats *clear_req;
	struct mbox *mbox = &pfvf->mbox;
	struct mcs_secy_stats *rsp;
	struct mcs_stats_req *req;
	int ret;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_get_secy_stats(mbox);
	if (!req) {
		ret = -ENOMEM;
		goto fail;
	}

	req->id = hw_secy_id;
	req->dir = dir;

	if (!clear)
		goto send_msg;

	clear_req = otx2_mbox_alloc_msg_mcs_clear_stats(mbox);
	if (!clear_req) {
		ret = -ENOMEM;
		goto fail;
	}
	clear_req->id = hw_secy_id;
	clear_req->dir = dir;
	clear_req->type = MCS_RSRC_TYPE_SECY;

send_msg:
	ret = otx2_sync_mbox_msg(mbox);
	if (ret)
		goto fail;

	rsp = (struct mcs_secy_stats *)otx2_mbox_get_rsp(&pfvf->mbox.mbox,
							 0, &req->hdr);
	if (IS_ERR(rsp)) {
		ret = PTR_ERR(rsp);
		goto fail;
	}

	memcpy(rsp_p, rsp, sizeof(*rsp_p));

	mutex_unlock(&mbox->lock);

	return 0;
fail:
	mutex_unlock(&mbox->lock);
	return ret;
}

static struct cn10k_mcs_txsc *cn10k_mcs_create_txsc(struct otx2_nic *pfvf)
{
	struct cn10k_mcs_txsc *txsc;
	int ret;

	txsc = kzalloc(sizeof(*txsc), GFP_KERNEL);
	if (!txsc)
		return ERR_PTR(-ENOMEM);

	ret = cn10k_mcs_alloc_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_FLOWID,
				   &txsc->hw_flow_id);
	if (ret)
		goto fail;

	/* For a SecY, one TX secy and one RX secy HW resources are needed */
	ret = cn10k_mcs_alloc_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SECY,
				   &txsc->hw_secy_id_tx);
	if (ret)
		goto free_flowid;

	ret = cn10k_mcs_alloc_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SECY,
				   &txsc->hw_secy_id_rx);
	if (ret)
		goto free_tx_secy;

	ret = cn10k_mcs_alloc_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SC,
				   &txsc->hw_sc_id);
	if (ret)
		goto free_rx_secy;

	return txsc;
free_rx_secy:
	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SECY,
			    txsc->hw_secy_id_rx, false);
free_tx_secy:
	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SECY,
			    txsc->hw_secy_id_tx, false);
free_flowid:
	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_FLOWID,
			    txsc->hw_flow_id, false);
fail:
	kfree(txsc);
	return ERR_PTR(ret);
}

/* Free Tx SC and its SAs(if any) resources to AF
 */
static void cn10k_mcs_delete_txsc(struct otx2_nic *pfvf,
				  struct cn10k_mcs_txsc *txsc)
{
	u8 sa_bmap = txsc->sa_bmap;
	u8 sa_num = 0;

	while (sa_bmap) {
		if (sa_bmap & 1) {
			cn10k_mcs_write_tx_sa_plcy(pfvf, txsc->sw_secy,
						   txsc, sa_num);
			cn10k_mcs_free_txsa(pfvf, txsc->hw_sa_id[sa_num]);
		}
		sa_num++;
		sa_bmap >>= 1;
	}

	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SC,
			    txsc->hw_sc_id, false);
	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SECY,
			    txsc->hw_secy_id_rx, false);
	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SECY,
			    txsc->hw_secy_id_tx, false);
	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_FLOWID,
			    txsc->hw_flow_id, false);
}

static struct cn10k_mcs_rxsc *cn10k_mcs_create_rxsc(struct otx2_nic *pfvf)
{
	struct cn10k_mcs_rxsc *rxsc;
	int ret;

	rxsc = kzalloc(sizeof(*rxsc), GFP_KERNEL);
	if (!rxsc)
		return ERR_PTR(-ENOMEM);

	ret = cn10k_mcs_alloc_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_FLOWID,
				   &rxsc->hw_flow_id);
	if (ret)
		goto fail;

	ret = cn10k_mcs_alloc_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SC,
				   &rxsc->hw_sc_id);
	if (ret)
		goto free_flowid;

	return rxsc;
free_flowid:
	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_FLOWID,
			    rxsc->hw_flow_id, false);
fail:
	kfree(rxsc);
	return ERR_PTR(ret);
}

/* Free Rx SC and its SAs(if any) resources to AF
 */
static void cn10k_mcs_delete_rxsc(struct otx2_nic *pfvf,
				  struct cn10k_mcs_rxsc *rxsc)
{
	u8 sa_bmap = rxsc->sa_bmap;
	u8 sa_num = 0;

	while (sa_bmap) {
		if (sa_bmap & 1) {
			cn10k_mcs_write_rx_sa_plcy(pfvf, rxsc->sw_secy, rxsc,
						   sa_num, false);
			cn10k_mcs_free_rxsa(pfvf, rxsc->hw_sa_id[sa_num]);
		}
		sa_num++;
		sa_bmap >>= 1;
	}

	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SC,
			    rxsc->hw_sc_id, false);
	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_FLOWID,
			    rxsc->hw_flow_id, false);
}

static int cn10k_mcs_secy_tx_cfg(struct otx2_nic *pfvf, struct macsec_secy *secy,
				 struct cn10k_mcs_txsc *txsc,
				 struct macsec_tx_sa *sw_tx_sa, u8 sa_num)
{
	if (sw_tx_sa) {
		cn10k_mcs_write_tx_sa_plcy(pfvf, secy, txsc, sa_num);
		cn10k_write_tx_sa_pn(pfvf, txsc, sa_num,
				     sw_tx_sa->next_pn_halves.lower);
		cn10k_mcs_link_tx_sa2sc(pfvf, secy, txsc, sa_num,
					sw_tx_sa->active);
	}

	cn10k_mcs_write_tx_secy(pfvf, secy, txsc);
	cn10k_mcs_write_tx_flowid(pfvf, secy, txsc);
	/* When updating secy, change RX secy also */
	cn10k_mcs_write_rx_secy(pfvf, secy, txsc->hw_secy_id_rx);

	return 0;
}

static int cn10k_mcs_secy_rx_cfg(struct otx2_nic *pfvf,
				 struct macsec_secy *secy, u8 hw_secy_id)
{
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct cn10k_mcs_rxsc *mcs_rx_sc;
	struct macsec_rx_sc *sw_rx_sc;
	struct macsec_rx_sa *sw_rx_sa;
	u8 sa_num;

	for (sw_rx_sc = rcu_dereference_bh(secy->rx_sc); sw_rx_sc && sw_rx_sc->active;
	     sw_rx_sc = rcu_dereference_bh(sw_rx_sc->next)) {
		mcs_rx_sc = cn10k_mcs_get_rxsc(cfg, secy, sw_rx_sc);
		if (unlikely(!mcs_rx_sc))
			continue;

		for (sa_num = 0; sa_num < CN10K_MCS_SA_PER_SC; sa_num++) {
			sw_rx_sa = rcu_dereference_bh(sw_rx_sc->sa[sa_num]);
			if (!sw_rx_sa)
				continue;

			cn10k_mcs_write_rx_sa_plcy(pfvf, secy, mcs_rx_sc,
						   sa_num, sw_rx_sa->active);
			cn10k_mcs_write_rx_sa_pn(pfvf, mcs_rx_sc, sa_num,
						 sw_rx_sa->next_pn_halves.lower);
		}

		cn10k_mcs_write_rx_flowid(pfvf, mcs_rx_sc, hw_secy_id);
		cn10k_mcs_write_sc_cam(pfvf, mcs_rx_sc, hw_secy_id);
	}

	return 0;
}

static int cn10k_mcs_disable_rxscs(struct otx2_nic *pfvf,
				   struct macsec_secy *secy,
				   bool delete)
{
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct cn10k_mcs_rxsc *mcs_rx_sc;
	struct macsec_rx_sc *sw_rx_sc;
	int ret;

	for (sw_rx_sc = rcu_dereference_bh(secy->rx_sc); sw_rx_sc && sw_rx_sc->active;
	     sw_rx_sc = rcu_dereference_bh(sw_rx_sc->next)) {
		mcs_rx_sc = cn10k_mcs_get_rxsc(cfg, secy, sw_rx_sc);
		if (unlikely(!mcs_rx_sc))
			continue;

		ret = cn10k_mcs_ena_dis_flowid(pfvf, mcs_rx_sc->hw_flow_id,
					       false, MCS_RX);
		if (ret)
			dev_err(pfvf->dev, "Failed to disable TCAM for SC %d\n",
				mcs_rx_sc->hw_sc_id);
		if (delete) {
			cn10k_mcs_delete_rxsc(pfvf, mcs_rx_sc);
			list_del(&mcs_rx_sc->entry);
			kfree(mcs_rx_sc);
		}
	}

	return 0;
}

static void cn10k_mcs_sync_stats(struct otx2_nic *pfvf, struct macsec_secy *secy,
				 struct cn10k_mcs_txsc *txsc)
{
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct mcs_secy_stats rx_rsp = { 0 };
	struct mcs_sc_stats sc_rsp = { 0 };
	struct cn10k_mcs_rxsc *rxsc;

	/* Because of shared counters for some stats in the hardware, when
	 * updating secy policy take a snapshot of current stats and reset them.
	 * Below are the effected stats because of shared counters.
	 */

	/* Check if sync is really needed */
	if (secy->validate_frames == txsc->last_validate_frames &&
	    secy->replay_protect == txsc->last_replay_protect)
		return;

	cn10k_mcs_secy_stats(pfvf, txsc->hw_secy_id_rx, &rx_rsp, MCS_RX, true);

	txsc->stats.InPktsBadTag += rx_rsp.pkt_badtag_cnt;
	txsc->stats.InPktsUnknownSCI += rx_rsp.pkt_nosa_cnt;
	txsc->stats.InPktsNoSCI += rx_rsp.pkt_nosaerror_cnt;
	if (txsc->last_validate_frames == MACSEC_VALIDATE_STRICT)
		txsc->stats.InPktsNoTag += rx_rsp.pkt_untaged_cnt;
	else
		txsc->stats.InPktsUntagged += rx_rsp.pkt_untaged_cnt;

	list_for_each_entry(rxsc, &cfg->rxsc_list, entry) {
		cn10k_mcs_sc_stats(pfvf, rxsc->hw_sc_id, &sc_rsp, MCS_RX, true);

		rxsc->stats.InOctetsValidated += sc_rsp.octet_validate_cnt;
		rxsc->stats.InOctetsDecrypted += sc_rsp.octet_decrypt_cnt;

		rxsc->stats.InPktsInvalid += sc_rsp.pkt_invalid_cnt;
		rxsc->stats.InPktsNotValid += sc_rsp.pkt_notvalid_cnt;

		if (txsc->last_replay_protect)
			rxsc->stats.InPktsLate += sc_rsp.pkt_late_cnt;
		else
			rxsc->stats.InPktsDelayed += sc_rsp.pkt_late_cnt;

		if (txsc->last_validate_frames == MACSEC_VALIDATE_DISABLED)
			rxsc->stats.InPktsUnchecked += sc_rsp.pkt_unchecked_cnt;
		else
			rxsc->stats.InPktsOK += sc_rsp.pkt_unchecked_cnt;
	}

	txsc->last_validate_frames = secy->validate_frames;
	txsc->last_replay_protect = secy->replay_protect;
}

static int cn10k_mdo_open(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	struct macsec_tx_sa *sw_tx_sa;
	struct cn10k_mcs_txsc *txsc;
	u8 sa_num;
	int err;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	sa_num = txsc->encoding_sa;
	sw_tx_sa = rcu_dereference_bh(secy->tx_sc.sa[sa_num]);

	err = cn10k_mcs_secy_tx_cfg(pfvf, secy, txsc, sw_tx_sa, sa_num);
	if (err)
		return err;

	return cn10k_mcs_secy_rx_cfg(pfvf, secy, txsc->hw_secy_id_rx);
}

static int cn10k_mdo_stop(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct cn10k_mcs_txsc *txsc;
	int err;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	err = cn10k_mcs_ena_dis_flowid(pfvf, txsc->hw_flow_id, false, MCS_TX);
	if (err)
		return err;

	return cn10k_mcs_disable_rxscs(pfvf, ctx->secy, false);
}

static int cn10k_mdo_add_secy(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	struct cn10k_mcs_txsc *txsc;

	if (secy->icv_len != MACSEC_DEFAULT_ICV_LEN)
		return -EOPNOTSUPP;

	/* Stick to 16 bytes key len until XPN support is added */
	if (secy->key_len != 16)
		return -EOPNOTSUPP;

	if (secy->xpn)
		return -EOPNOTSUPP;

	txsc = cn10k_mcs_create_txsc(pfvf);
	if (IS_ERR(txsc))
		return -ENOSPC;

	txsc->sw_secy = secy;
	txsc->encoding_sa = secy->tx_sc.encoding_sa;
	txsc->last_validate_frames = secy->validate_frames;
	txsc->last_replay_protect = secy->replay_protect;

	list_add(&txsc->entry, &cfg->txsc_list);

	if (netif_running(secy->netdev))
		return cn10k_mcs_secy_tx_cfg(pfvf, secy, txsc, NULL, 0);

	return 0;
}

static int cn10k_mdo_upd_secy(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	struct macsec_tx_sa *sw_tx_sa;
	struct cn10k_mcs_txsc *txsc;
	bool active;
	u8 sa_num;
	int err;

	txsc = cn10k_mcs_get_txsc(cfg, secy);
	if (!txsc)
		return -ENOENT;

	/* Encoding SA got changed */
	if (txsc->encoding_sa != secy->tx_sc.encoding_sa) {
		txsc->encoding_sa = secy->tx_sc.encoding_sa;
		sa_num = txsc->encoding_sa;
		sw_tx_sa = rcu_dereference_bh(secy->tx_sc.sa[sa_num]);
		active = sw_tx_sa ? sw_tx_sa->active : false;
		cn10k_mcs_link_tx_sa2sc(pfvf, secy, txsc, sa_num, active);
	}

	if (netif_running(secy->netdev)) {
		cn10k_mcs_sync_stats(pfvf, secy, txsc);

		err = cn10k_mcs_secy_tx_cfg(pfvf, secy, txsc, NULL, 0);
		if (err)
			return err;
	}

	return 0;
}

static int cn10k_mdo_del_secy(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct cn10k_mcs_txsc *txsc;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	cn10k_mcs_ena_dis_flowid(pfvf, txsc->hw_flow_id, false, MCS_TX);
	cn10k_mcs_disable_rxscs(pfvf, ctx->secy, true);
	cn10k_mcs_delete_txsc(pfvf, txsc);
	list_del(&txsc->entry);
	kfree(txsc);

	return 0;
}

static int cn10k_mdo_add_txsa(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct macsec_tx_sa *sw_tx_sa = ctx->sa.tx_sa;
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_txsc *txsc;
	int err;

	txsc = cn10k_mcs_get_txsc(cfg, secy);
	if (!txsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	if (cn10k_mcs_alloc_txsa(pfvf, &txsc->hw_sa_id[sa_num]))
		return -ENOSPC;

	memcpy(&txsc->sa_key[sa_num], ctx->sa.key, secy->key_len);
	txsc->sa_bmap |= 1 << sa_num;

	if (netif_running(secy->netdev)) {
		err = cn10k_mcs_write_tx_sa_plcy(pfvf, secy, txsc, sa_num);
		if (err)
			return err;

		err = cn10k_write_tx_sa_pn(pfvf, txsc, sa_num,
					   sw_tx_sa->next_pn_halves.lower);
		if (err)
			return err;

		err = cn10k_mcs_link_tx_sa2sc(pfvf, secy, txsc,
					      sa_num, sw_tx_sa->active);
		if (err)
			return err;
	}

	return 0;
}

static int cn10k_mdo_upd_txsa(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct macsec_tx_sa *sw_tx_sa = ctx->sa.tx_sa;
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_txsc *txsc;
	int err;

	txsc = cn10k_mcs_get_txsc(cfg, secy);
	if (!txsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	if (netif_running(secy->netdev)) {
		/* Keys cannot be changed after creation */
		err = cn10k_write_tx_sa_pn(pfvf, txsc, sa_num,
					   sw_tx_sa->next_pn_halves.lower);
		if (err)
			return err;

		err = cn10k_mcs_link_tx_sa2sc(pfvf, secy, txsc,
					      sa_num, sw_tx_sa->active);
		if (err)
			return err;
	}

	return 0;
}

static int cn10k_mdo_del_txsa(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_txsc *txsc;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	cn10k_mcs_free_txsa(pfvf, txsc->hw_sa_id[sa_num]);
	txsc->sa_bmap &= ~(1 << sa_num);

	return 0;
}

static int cn10k_mdo_add_rxsc(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	struct cn10k_mcs_rxsc *rxsc;
	struct cn10k_mcs_txsc *txsc;
	int err;

	txsc = cn10k_mcs_get_txsc(cfg, secy);
	if (!txsc)
		return -ENOENT;

	rxsc = cn10k_mcs_create_rxsc(pfvf);
	if (IS_ERR(rxsc))
		return -ENOSPC;

	rxsc->sw_secy = ctx->secy;
	rxsc->sw_rxsc = ctx->rx_sc;
	list_add(&rxsc->entry, &cfg->rxsc_list);

	if (netif_running(secy->netdev)) {
		err = cn10k_mcs_write_rx_flowid(pfvf, rxsc, txsc->hw_secy_id_rx);
		if (err)
			return err;

		err = cn10k_mcs_write_sc_cam(pfvf, rxsc, txsc->hw_secy_id_rx);
		if (err)
			return err;
	}

	return 0;
}

static int cn10k_mdo_upd_rxsc(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	bool enable = ctx->rx_sc->active;
	struct cn10k_mcs_rxsc *rxsc;

	rxsc = cn10k_mcs_get_rxsc(cfg, secy, ctx->rx_sc);
	if (!rxsc)
		return -ENOENT;

	if (netif_running(secy->netdev))
		return cn10k_mcs_ena_dis_flowid(pfvf, rxsc->hw_flow_id,
						enable, MCS_RX);

	return 0;
}

static int cn10k_mdo_del_rxsc(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct cn10k_mcs_rxsc *rxsc;

	rxsc = cn10k_mcs_get_rxsc(cfg, ctx->secy, ctx->rx_sc);
	if (!rxsc)
		return -ENOENT;

	cn10k_mcs_ena_dis_flowid(pfvf, rxsc->hw_flow_id, false, MCS_RX);
	cn10k_mcs_delete_rxsc(pfvf, rxsc);
	list_del(&rxsc->entry);
	kfree(rxsc);

	return 0;
}

static int cn10k_mdo_add_rxsa(struct macsec_context *ctx)
{
	struct macsec_rx_sc *sw_rx_sc = ctx->sa.rx_sa->sc;
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_rx_sa *rx_sa = ctx->sa.rx_sa;
	u64 next_pn = rx_sa->next_pn_halves.lower;
	struct macsec_secy *secy = ctx->secy;
	bool sa_in_use = rx_sa->active;
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_rxsc *rxsc;
	int err;

	rxsc = cn10k_mcs_get_rxsc(cfg, secy, sw_rx_sc);
	if (!rxsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	if (cn10k_mcs_alloc_rxsa(pfvf, &rxsc->hw_sa_id[sa_num]))
		return -ENOSPC;

	memcpy(&rxsc->sa_key[sa_num], ctx->sa.key, ctx->secy->key_len);
	rxsc->sa_bmap |= 1 << sa_num;

	if (netif_running(secy->netdev)) {
		err = cn10k_mcs_write_rx_sa_plcy(pfvf, secy, rxsc,
						 sa_num, sa_in_use);
		if (err)
			return err;

		err = cn10k_mcs_write_rx_sa_pn(pfvf, rxsc, sa_num, next_pn);
		if (err)
			return err;
	}

	return 0;
}

static int cn10k_mdo_upd_rxsa(struct macsec_context *ctx)
{
	struct macsec_rx_sc *sw_rx_sc = ctx->sa.rx_sa->sc;
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_rx_sa *rx_sa = ctx->sa.rx_sa;
	u64 next_pn = rx_sa->next_pn_halves.lower;
	struct macsec_secy *secy = ctx->secy;
	bool sa_in_use = rx_sa->active;
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_rxsc *rxsc;
	int err;

	rxsc = cn10k_mcs_get_rxsc(cfg, secy, sw_rx_sc);
	if (!rxsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	if (netif_running(secy->netdev)) {
		err = cn10k_mcs_write_rx_sa_plcy(pfvf, secy, rxsc, sa_num, sa_in_use);
		if (err)
			return err;

		err = cn10k_mcs_write_rx_sa_pn(pfvf, rxsc, sa_num, next_pn);
		if (err)
			return err;
	}

	return 0;
}

static int cn10k_mdo_del_rxsa(struct macsec_context *ctx)
{
	struct macsec_rx_sc *sw_rx_sc = ctx->sa.rx_sa->sc;
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_rxsc *rxsc;

	rxsc = cn10k_mcs_get_rxsc(cfg, ctx->secy, sw_rx_sc);
	if (!rxsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	cn10k_mcs_write_rx_sa_plcy(pfvf, ctx->secy, rxsc, sa_num, false);
	cn10k_mcs_free_rxsa(pfvf, rxsc->hw_sa_id[sa_num]);

	rxsc->sa_bmap &= ~(1 << sa_num);

	return 0;
}

static int cn10k_mdo_get_dev_stats(struct macsec_context *ctx)
{
	struct mcs_secy_stats tx_rsp = { 0 }, rx_rsp = { 0 };
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	struct cn10k_mcs_txsc *txsc;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	cn10k_mcs_secy_stats(pfvf, txsc->hw_secy_id_tx, &tx_rsp, MCS_TX, false);
	ctx->stats.dev_stats->OutPktsUntagged = tx_rsp.pkt_untagged_cnt;
	ctx->stats.dev_stats->OutPktsTooLong = tx_rsp.pkt_toolong_cnt;

	cn10k_mcs_secy_stats(pfvf, txsc->hw_secy_id_rx, &rx_rsp, MCS_RX, true);
	txsc->stats.InPktsBadTag += rx_rsp.pkt_badtag_cnt;
	txsc->stats.InPktsUnknownSCI += rx_rsp.pkt_nosa_cnt;
	txsc->stats.InPktsNoSCI += rx_rsp.pkt_nosaerror_cnt;
	if (secy->validate_frames == MACSEC_VALIDATE_STRICT)
		txsc->stats.InPktsNoTag += rx_rsp.pkt_untaged_cnt;
	else
		txsc->stats.InPktsUntagged += rx_rsp.pkt_untaged_cnt;
	txsc->stats.InPktsOverrun = 0;

	ctx->stats.dev_stats->InPktsNoTag = txsc->stats.InPktsNoTag;
	ctx->stats.dev_stats->InPktsUntagged = txsc->stats.InPktsUntagged;
	ctx->stats.dev_stats->InPktsBadTag = txsc->stats.InPktsBadTag;
	ctx->stats.dev_stats->InPktsUnknownSCI = txsc->stats.InPktsUnknownSCI;
	ctx->stats.dev_stats->InPktsNoSCI = txsc->stats.InPktsNoSCI;
	ctx->stats.dev_stats->InPktsOverrun = txsc->stats.InPktsOverrun;

	return 0;
}

static int cn10k_mdo_get_tx_sc_stats(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct mcs_sc_stats rsp = { 0 };
	struct cn10k_mcs_txsc *txsc;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	cn10k_mcs_sc_stats(pfvf, txsc->hw_sc_id, &rsp, MCS_TX, false);

	ctx->stats.tx_sc_stats->OutPktsProtected = rsp.pkt_protected_cnt;
	ctx->stats.tx_sc_stats->OutPktsEncrypted = rsp.pkt_encrypt_cnt;
	ctx->stats.tx_sc_stats->OutOctetsProtected = rsp.octet_protected_cnt;
	ctx->stats.tx_sc_stats->OutOctetsEncrypted = rsp.octet_encrypt_cnt;

	return 0;
}

static int cn10k_mdo_get_tx_sa_stats(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct mcs_sa_stats rsp = { 0 };
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_txsc *txsc;

	txsc = cn10k_mcs_get_txsc(cfg, ctx->secy);
	if (!txsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	cn10k_mcs_sa_stats(pfvf, txsc->hw_sa_id[sa_num], &rsp, MCS_TX, false);

	ctx->stats.tx_sa_stats->OutPktsProtected = rsp.pkt_protected_cnt;
	ctx->stats.tx_sa_stats->OutPktsEncrypted = rsp.pkt_encrypt_cnt;

	return 0;
}

static int cn10k_mdo_get_rx_sc_stats(struct macsec_context *ctx)
{
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_secy *secy = ctx->secy;
	struct mcs_sc_stats rsp = { 0 };
	struct cn10k_mcs_rxsc *rxsc;

	rxsc = cn10k_mcs_get_rxsc(cfg, secy, ctx->rx_sc);
	if (!rxsc)
		return -ENOENT;

	cn10k_mcs_sc_stats(pfvf, rxsc->hw_sc_id, &rsp, MCS_RX, true);

	rxsc->stats.InOctetsValidated += rsp.octet_validate_cnt;
	rxsc->stats.InOctetsDecrypted += rsp.octet_decrypt_cnt;

	rxsc->stats.InPktsInvalid += rsp.pkt_invalid_cnt;
	rxsc->stats.InPktsNotValid += rsp.pkt_notvalid_cnt;

	if (secy->replay_protect)
		rxsc->stats.InPktsLate += rsp.pkt_late_cnt;
	else
		rxsc->stats.InPktsDelayed += rsp.pkt_late_cnt;

	if (secy->validate_frames == MACSEC_VALIDATE_DISABLED)
		rxsc->stats.InPktsUnchecked += rsp.pkt_unchecked_cnt;
	else
		rxsc->stats.InPktsOK += rsp.pkt_unchecked_cnt;

	ctx->stats.rx_sc_stats->InOctetsValidated = rxsc->stats.InOctetsValidated;
	ctx->stats.rx_sc_stats->InOctetsDecrypted = rxsc->stats.InOctetsDecrypted;
	ctx->stats.rx_sc_stats->InPktsInvalid = rxsc->stats.InPktsInvalid;
	ctx->stats.rx_sc_stats->InPktsNotValid = rxsc->stats.InPktsNotValid;
	ctx->stats.rx_sc_stats->InPktsLate = rxsc->stats.InPktsLate;
	ctx->stats.rx_sc_stats->InPktsDelayed = rxsc->stats.InPktsDelayed;
	ctx->stats.rx_sc_stats->InPktsUnchecked = rxsc->stats.InPktsUnchecked;
	ctx->stats.rx_sc_stats->InPktsOK = rxsc->stats.InPktsOK;

	return 0;
}

static int cn10k_mdo_get_rx_sa_stats(struct macsec_context *ctx)
{
	struct macsec_rx_sc *sw_rx_sc = ctx->sa.rx_sa->sc;
	struct otx2_nic *pfvf = netdev_priv(ctx->netdev);
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct mcs_sa_stats rsp = { 0 };
	u8 sa_num = ctx->sa.assoc_num;
	struct cn10k_mcs_rxsc *rxsc;

	rxsc = cn10k_mcs_get_rxsc(cfg, ctx->secy, sw_rx_sc);
	if (!rxsc)
		return -ENOENT;

	if (sa_num >= CN10K_MCS_SA_PER_SC)
		return -EOPNOTSUPP;

	cn10k_mcs_sa_stats(pfvf, rxsc->hw_sa_id[sa_num], &rsp, MCS_RX, false);

	ctx->stats.rx_sa_stats->InPktsOK = rsp.pkt_ok_cnt;
	ctx->stats.rx_sa_stats->InPktsInvalid = rsp.pkt_invalid_cnt;
	ctx->stats.rx_sa_stats->InPktsNotValid = rsp.pkt_notvalid_cnt;
	ctx->stats.rx_sa_stats->InPktsNotUsingSA = rsp.pkt_nosaerror_cnt;
	ctx->stats.rx_sa_stats->InPktsUnusedSA = rsp.pkt_nosa_cnt;

	return 0;
}

static const struct macsec_ops cn10k_mcs_ops = {
	.mdo_dev_open = cn10k_mdo_open,
	.mdo_dev_stop = cn10k_mdo_stop,
	.mdo_add_secy = cn10k_mdo_add_secy,
	.mdo_upd_secy = cn10k_mdo_upd_secy,
	.mdo_del_secy = cn10k_mdo_del_secy,
	.mdo_add_rxsc = cn10k_mdo_add_rxsc,
	.mdo_upd_rxsc = cn10k_mdo_upd_rxsc,
	.mdo_del_rxsc = cn10k_mdo_del_rxsc,
	.mdo_add_rxsa = cn10k_mdo_add_rxsa,
	.mdo_upd_rxsa = cn10k_mdo_upd_rxsa,
	.mdo_del_rxsa = cn10k_mdo_del_rxsa,
	.mdo_add_txsa = cn10k_mdo_add_txsa,
	.mdo_upd_txsa = cn10k_mdo_upd_txsa,
	.mdo_del_txsa = cn10k_mdo_del_txsa,
	.mdo_get_dev_stats = cn10k_mdo_get_dev_stats,
	.mdo_get_tx_sc_stats = cn10k_mdo_get_tx_sc_stats,
	.mdo_get_tx_sa_stats = cn10k_mdo_get_tx_sa_stats,
	.mdo_get_rx_sc_stats = cn10k_mdo_get_rx_sc_stats,
	.mdo_get_rx_sa_stats = cn10k_mdo_get_rx_sa_stats,
};

void cn10k_handle_mcs_event(struct otx2_nic *pfvf, struct mcs_intr_info *event)
{
	struct cn10k_mcs_cfg *cfg = pfvf->macsec_cfg;
	struct macsec_tx_sa *sw_tx_sa = NULL;
	struct macsec_secy *secy = NULL;
	struct cn10k_mcs_txsc *txsc;
	u8 an;

	if (!test_bit(CN10K_HW_MACSEC, &pfvf->hw.cap_flag))
		return;

	if (!(event->intr_mask & MCS_CPM_TX_PACKET_XPN_EQ0_INT))
		return;

	/* Find the SecY to which the expired hardware SA is mapped */
	list_for_each_entry(txsc, &cfg->txsc_list, entry) {
		for (an = 0; an < CN10K_MCS_SA_PER_SC; an++)
			if (txsc->hw_sa_id[an] == event->sa_id) {
				secy = txsc->sw_secy;
				sw_tx_sa = rcu_dereference_bh(secy->tx_sc.sa[an]);
			}
	}

	if (secy && sw_tx_sa)
		macsec_pn_wrapped(secy, sw_tx_sa);
}

int cn10k_mcs_init(struct otx2_nic *pfvf)
{
	struct mbox *mbox = &pfvf->mbox;
	struct cn10k_mcs_cfg *cfg;
	struct mcs_intr_cfg *req;

	if (!test_bit(CN10K_HW_MACSEC, &pfvf->hw.cap_flag))
		return 0;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	INIT_LIST_HEAD(&cfg->txsc_list);
	INIT_LIST_HEAD(&cfg->rxsc_list);
	pfvf->macsec_cfg = cfg;

	pfvf->netdev->features |= NETIF_F_HW_MACSEC;
	pfvf->netdev->macsec_ops = &cn10k_mcs_ops;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_mcs_intr_cfg(mbox);
	if (!req)
		goto fail;

	req->intr_mask = MCS_CPM_TX_PACKET_XPN_EQ0_INT;

	if (otx2_sync_mbox_msg(mbox))
		goto fail;

	mutex_unlock(&mbox->lock);

	return 0;
fail:
	dev_err(pfvf->dev, "Cannot notify PN wrapped event\n");
	mutex_unlock(&mbox->lock);
	return 0;
}

void cn10k_mcs_free(struct otx2_nic *pfvf)
{
	if (!test_bit(CN10K_HW_MACSEC, &pfvf->hw.cap_flag))
		return;

	cn10k_mcs_free_rsrc(pfvf, MCS_TX, MCS_RSRC_TYPE_SECY, 0, true);
	cn10k_mcs_free_rsrc(pfvf, MCS_RX, MCS_RSRC_TYPE_SECY, 0, true);
	kfree(pfvf->macsec_cfg);
	pfvf->macsec_cfg = NULL;
}
