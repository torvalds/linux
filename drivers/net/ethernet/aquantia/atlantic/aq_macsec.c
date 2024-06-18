// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "aq_macsec.h"
#include "aq_nic.h"
#include <linux/rtnetlink.h>

#include "macsec/macsec_api.h"
#define AQ_MACSEC_KEY_LEN_128_BIT 16
#define AQ_MACSEC_KEY_LEN_192_BIT 24
#define AQ_MACSEC_KEY_LEN_256_BIT 32

enum aq_clear_type {
	/* update HW configuration */
	AQ_CLEAR_HW = BIT(0),
	/* update SW configuration (busy bits, pointers) */
	AQ_CLEAR_SW = BIT(1),
	/* update both HW and SW configuration */
	AQ_CLEAR_ALL = AQ_CLEAR_HW | AQ_CLEAR_SW,
};

static int aq_clear_txsc(struct aq_nic_s *nic, const int txsc_idx,
			 enum aq_clear_type clear_type);
static int aq_clear_txsa(struct aq_nic_s *nic, struct aq_macsec_txsc *aq_txsc,
			 const int sa_num, enum aq_clear_type clear_type);
static int aq_clear_rxsc(struct aq_nic_s *nic, const int rxsc_idx,
			 enum aq_clear_type clear_type);
static int aq_clear_rxsa(struct aq_nic_s *nic, struct aq_macsec_rxsc *aq_rxsc,
			 const int sa_num, enum aq_clear_type clear_type);
static int aq_clear_secy(struct aq_nic_s *nic, const struct macsec_secy *secy,
			 enum aq_clear_type clear_type);
static int aq_apply_macsec_cfg(struct aq_nic_s *nic);
static int aq_apply_secy_cfg(struct aq_nic_s *nic,
			     const struct macsec_secy *secy);

static void aq_ether_addr_to_mac(u32 mac[2], const unsigned char *emac)
{
	u32 tmp[2] = { 0 };

	memcpy(((u8 *)tmp) + 2, emac, ETH_ALEN);

	mac[0] = swab32(tmp[1]);
	mac[1] = swab32(tmp[0]);
}

/* There's a 1:1 mapping between SecY and TX SC */
static int aq_get_txsc_idx_from_secy(struct aq_macsec_cfg *macsec_cfg,
				     const struct macsec_secy *secy)
{
	int i;

	if (unlikely(!secy))
		return -1;

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (macsec_cfg->aq_txsc[i].sw_secy == secy)
			return i;
	}
	return -1;
}

static int aq_get_rxsc_idx_from_rxsc(struct aq_macsec_cfg *macsec_cfg,
				     const struct macsec_rx_sc *rxsc)
{
	int i;

	if (unlikely(!rxsc))
		return -1;

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (macsec_cfg->aq_rxsc[i].sw_rxsc == rxsc)
			return i;
	}

	return -1;
}

static int aq_get_txsc_idx_from_sc_idx(const enum aq_macsec_sc_sa sc_sa,
				       const int sc_idx)
{
	switch (sc_sa) {
	case aq_macsec_sa_sc_4sa_8sc:
		return sc_idx >> 2;
	case aq_macsec_sa_sc_2sa_16sc:
		return sc_idx >> 1;
	case aq_macsec_sa_sc_1sa_32sc:
		return sc_idx;
	default:
		WARN_ONCE(true, "Invalid sc_sa");
	}
	return -1;
}

/* Rotate keys u32[8] */
static void aq_rotate_keys(u32 (*key)[8], const int key_len)
{
	u32 tmp[8] = { 0 };

	memcpy(&tmp, key, sizeof(tmp));
	memset(*key, 0, sizeof(*key));

	if (key_len == AQ_MACSEC_KEY_LEN_128_BIT) {
		(*key)[0] = swab32(tmp[3]);
		(*key)[1] = swab32(tmp[2]);
		(*key)[2] = swab32(tmp[1]);
		(*key)[3] = swab32(tmp[0]);
	} else if (key_len == AQ_MACSEC_KEY_LEN_192_BIT) {
		(*key)[0] = swab32(tmp[5]);
		(*key)[1] = swab32(tmp[4]);
		(*key)[2] = swab32(tmp[3]);
		(*key)[3] = swab32(tmp[2]);
		(*key)[4] = swab32(tmp[1]);
		(*key)[5] = swab32(tmp[0]);
	} else if (key_len == AQ_MACSEC_KEY_LEN_256_BIT) {
		(*key)[0] = swab32(tmp[7]);
		(*key)[1] = swab32(tmp[6]);
		(*key)[2] = swab32(tmp[5]);
		(*key)[3] = swab32(tmp[4]);
		(*key)[4] = swab32(tmp[3]);
		(*key)[5] = swab32(tmp[2]);
		(*key)[6] = swab32(tmp[1]);
		(*key)[7] = swab32(tmp[0]);
	} else {
		pr_warn("Rotate_keys: invalid key_len\n");
	}
}

#define STATS_2x32_TO_64(stat_field)                                           \
	(((u64)stat_field[1] << 32) | stat_field[0])

static int aq_get_macsec_common_stats(struct aq_hw_s *hw,
				      struct aq_macsec_common_stats *stats)
{
	struct aq_mss_ingress_common_counters ingress_counters;
	struct aq_mss_egress_common_counters egress_counters;
	int ret;

	/* MACSEC counters */
	ret = aq_mss_get_ingress_common_counters(hw, &ingress_counters);
	if (unlikely(ret))
		return ret;

	stats->in.ctl_pkts = STATS_2x32_TO_64(ingress_counters.ctl_pkts);
	stats->in.tagged_miss_pkts =
		STATS_2x32_TO_64(ingress_counters.tagged_miss_pkts);
	stats->in.untagged_miss_pkts =
		STATS_2x32_TO_64(ingress_counters.untagged_miss_pkts);
	stats->in.notag_pkts = STATS_2x32_TO_64(ingress_counters.notag_pkts);
	stats->in.untagged_pkts =
		STATS_2x32_TO_64(ingress_counters.untagged_pkts);
	stats->in.bad_tag_pkts =
		STATS_2x32_TO_64(ingress_counters.bad_tag_pkts);
	stats->in.no_sci_pkts = STATS_2x32_TO_64(ingress_counters.no_sci_pkts);
	stats->in.unknown_sci_pkts =
		STATS_2x32_TO_64(ingress_counters.unknown_sci_pkts);
	stats->in.ctrl_prt_pass_pkts =
		STATS_2x32_TO_64(ingress_counters.ctrl_prt_pass_pkts);
	stats->in.unctrl_prt_pass_pkts =
		STATS_2x32_TO_64(ingress_counters.unctrl_prt_pass_pkts);
	stats->in.ctrl_prt_fail_pkts =
		STATS_2x32_TO_64(ingress_counters.ctrl_prt_fail_pkts);
	stats->in.unctrl_prt_fail_pkts =
		STATS_2x32_TO_64(ingress_counters.unctrl_prt_fail_pkts);
	stats->in.too_long_pkts =
		STATS_2x32_TO_64(ingress_counters.too_long_pkts);
	stats->in.igpoc_ctl_pkts =
		STATS_2x32_TO_64(ingress_counters.igpoc_ctl_pkts);
	stats->in.ecc_error_pkts =
		STATS_2x32_TO_64(ingress_counters.ecc_error_pkts);
	stats->in.unctrl_hit_drop_redir =
		STATS_2x32_TO_64(ingress_counters.unctrl_hit_drop_redir);

	ret = aq_mss_get_egress_common_counters(hw, &egress_counters);
	if (unlikely(ret))
		return ret;
	stats->out.ctl_pkts = STATS_2x32_TO_64(egress_counters.ctl_pkt);
	stats->out.unknown_sa_pkts =
		STATS_2x32_TO_64(egress_counters.unknown_sa_pkts);
	stats->out.untagged_pkts =
		STATS_2x32_TO_64(egress_counters.untagged_pkts);
	stats->out.too_long = STATS_2x32_TO_64(egress_counters.too_long);
	stats->out.ecc_error_pkts =
		STATS_2x32_TO_64(egress_counters.ecc_error_pkts);
	stats->out.unctrl_hit_drop_redir =
		STATS_2x32_TO_64(egress_counters.unctrl_hit_drop_redir);

	return 0;
}

static int aq_get_rxsa_stats(struct aq_hw_s *hw, const int sa_idx,
			     struct aq_macsec_rx_sa_stats *stats)
{
	struct aq_mss_ingress_sa_counters i_sa_counters;
	int ret;

	ret = aq_mss_get_ingress_sa_counters(hw, &i_sa_counters, sa_idx);
	if (unlikely(ret))
		return ret;

	stats->untagged_hit_pkts =
		STATS_2x32_TO_64(i_sa_counters.untagged_hit_pkts);
	stats->ctrl_hit_drop_redir_pkts =
		STATS_2x32_TO_64(i_sa_counters.ctrl_hit_drop_redir_pkts);
	stats->not_using_sa = STATS_2x32_TO_64(i_sa_counters.not_using_sa);
	stats->unused_sa = STATS_2x32_TO_64(i_sa_counters.unused_sa);
	stats->not_valid_pkts = STATS_2x32_TO_64(i_sa_counters.not_valid_pkts);
	stats->invalid_pkts = STATS_2x32_TO_64(i_sa_counters.invalid_pkts);
	stats->ok_pkts = STATS_2x32_TO_64(i_sa_counters.ok_pkts);
	stats->late_pkts = STATS_2x32_TO_64(i_sa_counters.late_pkts);
	stats->delayed_pkts = STATS_2x32_TO_64(i_sa_counters.delayed_pkts);
	stats->unchecked_pkts = STATS_2x32_TO_64(i_sa_counters.unchecked_pkts);
	stats->validated_octets =
		STATS_2x32_TO_64(i_sa_counters.validated_octets);
	stats->decrypted_octets =
		STATS_2x32_TO_64(i_sa_counters.decrypted_octets);

	return 0;
}

static int aq_get_txsa_stats(struct aq_hw_s *hw, const int sa_idx,
			     struct aq_macsec_tx_sa_stats *stats)
{
	struct aq_mss_egress_sa_counters e_sa_counters;
	int ret;

	ret = aq_mss_get_egress_sa_counters(hw, &e_sa_counters, sa_idx);
	if (unlikely(ret))
		return ret;

	stats->sa_hit_drop_redirect =
		STATS_2x32_TO_64(e_sa_counters.sa_hit_drop_redirect);
	stats->sa_protected2_pkts =
		STATS_2x32_TO_64(e_sa_counters.sa_protected2_pkts);
	stats->sa_protected_pkts =
		STATS_2x32_TO_64(e_sa_counters.sa_protected_pkts);
	stats->sa_encrypted_pkts =
		STATS_2x32_TO_64(e_sa_counters.sa_encrypted_pkts);

	return 0;
}

static int aq_get_txsa_next_pn(struct aq_hw_s *hw, const int sa_idx, u32 *pn)
{
	struct aq_mss_egress_sa_record sa_rec;
	int ret;

	ret = aq_mss_get_egress_sa_record(hw, &sa_rec, sa_idx);
	if (likely(!ret))
		*pn = sa_rec.next_pn;

	return ret;
}

static int aq_get_rxsa_next_pn(struct aq_hw_s *hw, const int sa_idx, u32 *pn)
{
	struct aq_mss_ingress_sa_record sa_rec;
	int ret;

	ret = aq_mss_get_ingress_sa_record(hw, &sa_rec, sa_idx);
	if (likely(!ret))
		*pn = (!sa_rec.sat_nextpn) ? sa_rec.next_pn : 0;

	return ret;
}

static int aq_get_txsc_stats(struct aq_hw_s *hw, const int sc_idx,
			     struct aq_macsec_tx_sc_stats *stats)
{
	struct aq_mss_egress_sc_counters e_sc_counters;
	int ret;

	ret = aq_mss_get_egress_sc_counters(hw, &e_sc_counters, sc_idx);
	if (unlikely(ret))
		return ret;

	stats->sc_protected_pkts =
		STATS_2x32_TO_64(e_sc_counters.sc_protected_pkts);
	stats->sc_encrypted_pkts =
		STATS_2x32_TO_64(e_sc_counters.sc_encrypted_pkts);
	stats->sc_protected_octets =
		STATS_2x32_TO_64(e_sc_counters.sc_protected_octets);
	stats->sc_encrypted_octets =
		STATS_2x32_TO_64(e_sc_counters.sc_encrypted_octets);

	return 0;
}

static int aq_mdo_dev_open(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	int ret = 0;

	if (netif_carrier_ok(nic->ndev))
		ret = aq_apply_secy_cfg(nic, ctx->secy);

	return ret;
}

static int aq_mdo_dev_stop(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	int i;

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (nic->macsec_cfg->txsc_idx_busy & BIT(i))
			aq_clear_secy(nic, nic->macsec_cfg->aq_txsc[i].sw_secy,
				      AQ_CLEAR_HW);
	}

	return 0;
}

static int aq_set_txsc(struct aq_nic_s *nic, const int txsc_idx)
{
	struct aq_macsec_txsc *aq_txsc = &nic->macsec_cfg->aq_txsc[txsc_idx];
	struct aq_mss_egress_class_record tx_class_rec = { 0 };
	const struct macsec_secy *secy = aq_txsc->sw_secy;
	struct aq_mss_egress_sc_record sc_rec = { 0 };
	unsigned int sc_idx = aq_txsc->hw_sc_idx;
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	aq_ether_addr_to_mac(tx_class_rec.mac_sa, secy->netdev->dev_addr);

	put_unaligned_be64((__force u64)secy->sci, tx_class_rec.sci);
	tx_class_rec.sci_mask = 0;

	tx_class_rec.sa_mask = 0x3f;

	tx_class_rec.action = 0; /* forward to SA/SC table */
	tx_class_rec.valid = 1;

	tx_class_rec.sc_idx = sc_idx;

	tx_class_rec.sc_sa = nic->macsec_cfg->sc_sa;

	ret = aq_mss_set_egress_class_record(hw, &tx_class_rec, txsc_idx);
	if (ret)
		return ret;

	sc_rec.protect = secy->protect_frames;
	if (secy->tx_sc.encrypt)
		sc_rec.tci |= BIT(1);
	if (secy->tx_sc.scb)
		sc_rec.tci |= BIT(2);
	if (secy->tx_sc.send_sci)
		sc_rec.tci |= BIT(3);
	if (secy->tx_sc.end_station)
		sc_rec.tci |= BIT(4);
	/* The C bit is clear if and only if the Secure Data is
	 * exactly the same as the User Data and the ICV is 16 octets long.
	 */
	if (!(secy->icv_len == 16 && !secy->tx_sc.encrypt))
		sc_rec.tci |= BIT(0);

	sc_rec.an_roll = 0;

	switch (secy->key_len) {
	case AQ_MACSEC_KEY_LEN_128_BIT:
		sc_rec.sak_len = 0;
		break;
	case AQ_MACSEC_KEY_LEN_192_BIT:
		sc_rec.sak_len = 1;
		break;
	case AQ_MACSEC_KEY_LEN_256_BIT:
		sc_rec.sak_len = 2;
		break;
	default:
		WARN_ONCE(true, "Invalid sc_sa");
		return -EINVAL;
	}

	sc_rec.curr_an = secy->tx_sc.encoding_sa;
	sc_rec.valid = 1;
	sc_rec.fresh = 1;

	return aq_mss_set_egress_sc_record(hw, &sc_rec, sc_idx);
}

static u32 aq_sc_idx_max(const enum aq_macsec_sc_sa sc_sa)
{
	u32 result = 0;

	switch (sc_sa) {
	case aq_macsec_sa_sc_4sa_8sc:
		result = 8;
		break;
	case aq_macsec_sa_sc_2sa_16sc:
		result = 16;
		break;
	case aq_macsec_sa_sc_1sa_32sc:
		result = 32;
		break;
	default:
		break;
	}

	return result;
}

static u32 aq_to_hw_sc_idx(const u32 sc_idx, const enum aq_macsec_sc_sa sc_sa)
{
	switch (sc_sa) {
	case aq_macsec_sa_sc_4sa_8sc:
		return sc_idx << 2;
	case aq_macsec_sa_sc_2sa_16sc:
		return sc_idx << 1;
	case aq_macsec_sa_sc_1sa_32sc:
		return sc_idx;
	default:
		WARN_ONCE(true, "Invalid sc_sa");
	}

	return sc_idx;
}

static enum aq_macsec_sc_sa sc_sa_from_num_an(const int num_an)
{
	enum aq_macsec_sc_sa sc_sa = aq_macsec_sa_sc_not_used;

	switch (num_an) {
	case 4:
		sc_sa = aq_macsec_sa_sc_4sa_8sc;
		break;
	case 2:
		sc_sa = aq_macsec_sa_sc_2sa_16sc;
		break;
	case 1:
		sc_sa = aq_macsec_sa_sc_1sa_32sc;
		break;
	default:
		break;
	}

	return sc_sa;
}

static int aq_mdo_add_secy(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	enum aq_macsec_sc_sa sc_sa;
	u32 txsc_idx;
	int ret = 0;

	if (secy->xpn)
		return -EOPNOTSUPP;

	sc_sa = sc_sa_from_num_an(MACSEC_NUM_AN);
	if (sc_sa == aq_macsec_sa_sc_not_used)
		return -EINVAL;

	if (hweight32(cfg->txsc_idx_busy) >= aq_sc_idx_max(sc_sa))
		return -ENOSPC;

	txsc_idx = ffz(cfg->txsc_idx_busy);
	if (txsc_idx == AQ_MACSEC_MAX_SC)
		return -ENOSPC;

	cfg->sc_sa = sc_sa;
	cfg->aq_txsc[txsc_idx].hw_sc_idx = aq_to_hw_sc_idx(txsc_idx, sc_sa);
	cfg->aq_txsc[txsc_idx].sw_secy = secy;

	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_set_txsc(nic, txsc_idx);

	set_bit(txsc_idx, &cfg->txsc_idx_busy);

	return ret;
}

static int aq_mdo_upd_secy(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	const struct macsec_secy *secy = ctx->secy;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, secy);
	if (txsc_idx < 0)
		return -ENOENT;

	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_set_txsc(nic, txsc_idx);

	return ret;
}

static int aq_clear_txsc(struct aq_nic_s *nic, const int txsc_idx,
			 enum aq_clear_type clear_type)
{
	struct aq_macsec_txsc *tx_sc = &nic->macsec_cfg->aq_txsc[txsc_idx];
	struct aq_mss_egress_class_record tx_class_rec = { 0 };
	struct aq_mss_egress_sc_record sc_rec = { 0 };
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;
	int sa_num;

	for_each_set_bit (sa_num, &tx_sc->tx_sa_idx_busy, AQ_MACSEC_MAX_SA) {
		ret = aq_clear_txsa(nic, tx_sc, sa_num, clear_type);
		if (ret)
			return ret;
	}

	if (clear_type & AQ_CLEAR_HW) {
		ret = aq_mss_set_egress_class_record(hw, &tx_class_rec,
						     txsc_idx);
		if (ret)
			return ret;

		sc_rec.fresh = 1;
		ret = aq_mss_set_egress_sc_record(hw, &sc_rec,
						  tx_sc->hw_sc_idx);
		if (ret)
			return ret;
	}

	if (clear_type & AQ_CLEAR_SW) {
		clear_bit(txsc_idx, &nic->macsec_cfg->txsc_idx_busy);
		nic->macsec_cfg->aq_txsc[txsc_idx].sw_secy = NULL;
	}

	return ret;
}

static int aq_mdo_del_secy(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	int ret = 0;

	if (!nic->macsec_cfg)
		return 0;

	ret = aq_clear_secy(nic, ctx->secy, AQ_CLEAR_ALL);

	return ret;
}

static int aq_update_txsa(struct aq_nic_s *nic, const unsigned int sc_idx,
			  const struct macsec_secy *secy,
			  const struct macsec_tx_sa *tx_sa,
			  const unsigned char *key, const unsigned char an)
{
	const u32 next_pn = tx_sa->next_pn_halves.lower;
	struct aq_mss_egress_sakey_record key_rec;
	const unsigned int sa_idx = sc_idx | an;
	struct aq_mss_egress_sa_record sa_rec;
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	memset(&sa_rec, 0, sizeof(sa_rec));
	sa_rec.valid = tx_sa->active;
	sa_rec.fresh = 1;
	sa_rec.next_pn = next_pn;

	ret = aq_mss_set_egress_sa_record(hw, &sa_rec, sa_idx);
	if (ret)
		return ret;

	if (!key)
		return ret;

	memset(&key_rec, 0, sizeof(key_rec));
	memcpy(&key_rec.key, key, secy->key_len);

	aq_rotate_keys(&key_rec.key, secy->key_len);

	ret = aq_mss_set_egress_sakey_record(hw, &key_rec, sa_idx);

	memzero_explicit(&key_rec, sizeof(key_rec));
	return ret;
}

static int aq_mdo_add_txsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	struct aq_macsec_txsc *aq_txsc;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, secy);
	if (txsc_idx < 0)
		return -EINVAL;

	aq_txsc = &cfg->aq_txsc[txsc_idx];
	set_bit(ctx->sa.assoc_num, &aq_txsc->tx_sa_idx_busy);

	memcpy(aq_txsc->tx_sa_key[ctx->sa.assoc_num], ctx->sa.key,
	       secy->key_len);

	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_update_txsa(nic, aq_txsc->hw_sc_idx, secy,
				     ctx->sa.tx_sa, ctx->sa.key,
				     ctx->sa.assoc_num);

	return ret;
}

static int aq_mdo_upd_txsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	struct aq_macsec_txsc *aq_txsc;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, secy);
	if (txsc_idx < 0)
		return -EINVAL;

	aq_txsc = &cfg->aq_txsc[txsc_idx];
	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_update_txsa(nic, aq_txsc->hw_sc_idx, secy,
				     ctx->sa.tx_sa, NULL, ctx->sa.assoc_num);

	return ret;
}

static int aq_clear_txsa(struct aq_nic_s *nic, struct aq_macsec_txsc *aq_txsc,
			 const int sa_num, enum aq_clear_type clear_type)
{
	const int sa_idx = aq_txsc->hw_sc_idx | sa_num;
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	if (clear_type & AQ_CLEAR_SW)
		clear_bit(sa_num, &aq_txsc->tx_sa_idx_busy);

	if ((clear_type & AQ_CLEAR_HW) && netif_carrier_ok(nic->ndev)) {
		struct aq_mss_egress_sakey_record key_rec;
		struct aq_mss_egress_sa_record sa_rec;

		memset(&sa_rec, 0, sizeof(sa_rec));
		sa_rec.fresh = 1;

		ret = aq_mss_set_egress_sa_record(hw, &sa_rec, sa_idx);
		if (ret)
			return ret;

		memset(&key_rec, 0, sizeof(key_rec));
		return aq_mss_set_egress_sakey_record(hw, &key_rec, sa_idx);
	}

	return 0;
}

static int aq_mdo_del_txsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, ctx->secy);
	if (txsc_idx < 0)
		return -EINVAL;

	ret = aq_clear_txsa(nic, &cfg->aq_txsc[txsc_idx], ctx->sa.assoc_num,
			    AQ_CLEAR_ALL);

	return ret;
}

static int aq_rxsc_validate_frames(const enum macsec_validation_type validate)
{
	switch (validate) {
	case MACSEC_VALIDATE_DISABLED:
		return 2;
	case MACSEC_VALIDATE_CHECK:
		return 1;
	case MACSEC_VALIDATE_STRICT:
		return 0;
	default:
		WARN_ONCE(true, "Invalid validation type");
	}

	return 0;
}

static int aq_set_rxsc(struct aq_nic_s *nic, const u32 rxsc_idx)
{
	const struct aq_macsec_rxsc *aq_rxsc =
		&nic->macsec_cfg->aq_rxsc[rxsc_idx];
	struct aq_mss_ingress_preclass_record pre_class_record;
	const struct macsec_rx_sc *rx_sc = aq_rxsc->sw_rxsc;
	const struct macsec_secy *secy = aq_rxsc->sw_secy;
	const u32 hw_sc_idx = aq_rxsc->hw_sc_idx;
	struct aq_mss_ingress_sc_record sc_record;
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	memset(&pre_class_record, 0, sizeof(pre_class_record));
	put_unaligned_be64((__force u64)rx_sc->sci, pre_class_record.sci);
	pre_class_record.sci_mask = 0xff;
	/* match all MACSEC ethertype packets */
	pre_class_record.eth_type = ETH_P_MACSEC;
	pre_class_record.eth_type_mask = 0x3;

	aq_ether_addr_to_mac(pre_class_record.mac_sa, (char *)&rx_sc->sci);
	pre_class_record.sa_mask = 0x3f;

	pre_class_record.an_mask = nic->macsec_cfg->sc_sa;
	pre_class_record.sc_idx = hw_sc_idx;
	/* strip SecTAG & forward for decryption */
	pre_class_record.action = 0x0;
	pre_class_record.valid = 1;

	ret = aq_mss_set_ingress_preclass_record(hw, &pre_class_record,
						 2 * rxsc_idx + 1);
	if (ret)
		return ret;

	/* If SCI is absent, then match by SA alone */
	pre_class_record.sci_mask = 0;
	pre_class_record.sci_from_table = 1;

	ret = aq_mss_set_ingress_preclass_record(hw, &pre_class_record,
						 2 * rxsc_idx);
	if (ret)
		return ret;

	memset(&sc_record, 0, sizeof(sc_record));
	sc_record.validate_frames =
		aq_rxsc_validate_frames(secy->validate_frames);
	if (secy->replay_protect) {
		sc_record.replay_protect = 1;
		sc_record.anti_replay_window = secy->replay_window;
	}
	sc_record.valid = 1;
	sc_record.fresh = 1;

	ret = aq_mss_set_ingress_sc_record(hw, &sc_record, hw_sc_idx);
	if (ret)
		return ret;

	return ret;
}

static int aq_mdo_add_rxsc(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const u32 rxsc_idx_max = aq_sc_idx_max(cfg->sc_sa);
	u32 rxsc_idx;
	int ret = 0;

	if (hweight32(cfg->rxsc_idx_busy) >= rxsc_idx_max)
		return -ENOSPC;

	rxsc_idx = ffz(cfg->rxsc_idx_busy);
	if (rxsc_idx >= rxsc_idx_max)
		return -ENOSPC;

	cfg->aq_rxsc[rxsc_idx].hw_sc_idx = aq_to_hw_sc_idx(rxsc_idx,
							   cfg->sc_sa);
	cfg->aq_rxsc[rxsc_idx].sw_secy = ctx->secy;
	cfg->aq_rxsc[rxsc_idx].sw_rxsc = ctx->rx_sc;

	if (netif_carrier_ok(nic->ndev) && netif_running(ctx->secy->netdev))
		ret = aq_set_rxsc(nic, rxsc_idx);

	if (ret < 0)
		return ret;

	set_bit(rxsc_idx, &cfg->rxsc_idx_busy);

	return 0;
}

static int aq_mdo_upd_rxsc(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	int rxsc_idx;
	int ret = 0;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(nic->macsec_cfg, ctx->rx_sc);
	if (rxsc_idx < 0)
		return -ENOENT;

	if (netif_carrier_ok(nic->ndev) && netif_running(ctx->secy->netdev))
		ret = aq_set_rxsc(nic, rxsc_idx);

	return ret;
}

static int aq_clear_rxsc(struct aq_nic_s *nic, const int rxsc_idx,
			 enum aq_clear_type clear_type)
{
	struct aq_macsec_rxsc *rx_sc = &nic->macsec_cfg->aq_rxsc[rxsc_idx];
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;
	int sa_num;

	for_each_set_bit (sa_num, &rx_sc->rx_sa_idx_busy, AQ_MACSEC_MAX_SA) {
		ret = aq_clear_rxsa(nic, rx_sc, sa_num, clear_type);
		if (ret)
			return ret;
	}

	if (clear_type & AQ_CLEAR_HW) {
		struct aq_mss_ingress_preclass_record pre_class_record;
		struct aq_mss_ingress_sc_record sc_record;

		memset(&pre_class_record, 0, sizeof(pre_class_record));
		memset(&sc_record, 0, sizeof(sc_record));

		ret = aq_mss_set_ingress_preclass_record(hw, &pre_class_record,
							 2 * rxsc_idx);
		if (ret)
			return ret;

		ret = aq_mss_set_ingress_preclass_record(hw, &pre_class_record,
							 2 * rxsc_idx + 1);
		if (ret)
			return ret;

		sc_record.fresh = 1;
		ret = aq_mss_set_ingress_sc_record(hw, &sc_record,
						   rx_sc->hw_sc_idx);
		if (ret)
			return ret;
	}

	if (clear_type & AQ_CLEAR_SW) {
		clear_bit(rxsc_idx, &nic->macsec_cfg->rxsc_idx_busy);
		rx_sc->sw_secy = NULL;
		rx_sc->sw_rxsc = NULL;
	}

	return ret;
}

static int aq_mdo_del_rxsc(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	enum aq_clear_type clear_type = AQ_CLEAR_SW;
	int rxsc_idx;
	int ret = 0;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(nic->macsec_cfg, ctx->rx_sc);
	if (rxsc_idx < 0)
		return -ENOENT;

	if (netif_carrier_ok(nic->ndev))
		clear_type = AQ_CLEAR_ALL;

	ret = aq_clear_rxsc(nic, rxsc_idx, clear_type);

	return ret;
}

static int aq_update_rxsa(struct aq_nic_s *nic, const unsigned int sc_idx,
			  const struct macsec_secy *secy,
			  const struct macsec_rx_sa *rx_sa,
			  const unsigned char *key, const unsigned char an)
{
	struct aq_mss_ingress_sakey_record sa_key_record;
	const u32 next_pn = rx_sa->next_pn_halves.lower;
	struct aq_mss_ingress_sa_record sa_record;
	struct aq_hw_s *hw = nic->aq_hw;
	const int sa_idx = sc_idx | an;
	int ret = 0;

	memset(&sa_record, 0, sizeof(sa_record));
	sa_record.valid = rx_sa->active;
	sa_record.fresh = 1;
	sa_record.next_pn = next_pn;

	ret = aq_mss_set_ingress_sa_record(hw, &sa_record, sa_idx);
	if (ret)
		return ret;

	if (!key)
		return ret;

	memset(&sa_key_record, 0, sizeof(sa_key_record));
	memcpy(&sa_key_record.key, key, secy->key_len);

	switch (secy->key_len) {
	case AQ_MACSEC_KEY_LEN_128_BIT:
		sa_key_record.key_len = 0;
		break;
	case AQ_MACSEC_KEY_LEN_192_BIT:
		sa_key_record.key_len = 1;
		break;
	case AQ_MACSEC_KEY_LEN_256_BIT:
		sa_key_record.key_len = 2;
		break;
	default:
		return -1;
	}

	aq_rotate_keys(&sa_key_record.key, secy->key_len);

	ret = aq_mss_set_ingress_sakey_record(hw, &sa_key_record, sa_idx);

	memzero_explicit(&sa_key_record, sizeof(sa_key_record));
	return ret;
}

static int aq_mdo_add_rxsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	const struct macsec_rx_sc *rx_sc = ctx->sa.rx_sa->sc;
	const struct macsec_secy *secy = ctx->secy;
	struct aq_macsec_rxsc *aq_rxsc;
	int rxsc_idx;
	int ret = 0;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(nic->macsec_cfg, rx_sc);
	if (rxsc_idx < 0)
		return -EINVAL;

	aq_rxsc = &nic->macsec_cfg->aq_rxsc[rxsc_idx];
	set_bit(ctx->sa.assoc_num, &aq_rxsc->rx_sa_idx_busy);

	memcpy(aq_rxsc->rx_sa_key[ctx->sa.assoc_num], ctx->sa.key,
	       secy->key_len);

	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_update_rxsa(nic, aq_rxsc->hw_sc_idx, secy,
				     ctx->sa.rx_sa, ctx->sa.key,
				     ctx->sa.assoc_num);

	return ret;
}

static int aq_mdo_upd_rxsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	const struct macsec_rx_sc *rx_sc = ctx->sa.rx_sa->sc;
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	int rxsc_idx;
	int ret = 0;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(cfg, rx_sc);
	if (rxsc_idx < 0)
		return -EINVAL;

	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_update_rxsa(nic, cfg->aq_rxsc[rxsc_idx].hw_sc_idx,
				     secy, ctx->sa.rx_sa, NULL,
				     ctx->sa.assoc_num);

	return ret;
}

static int aq_clear_rxsa(struct aq_nic_s *nic, struct aq_macsec_rxsc *aq_rxsc,
			 const int sa_num, enum aq_clear_type clear_type)
{
	int sa_idx = aq_rxsc->hw_sc_idx | sa_num;
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	if (clear_type & AQ_CLEAR_SW)
		clear_bit(sa_num, &aq_rxsc->rx_sa_idx_busy);

	if ((clear_type & AQ_CLEAR_HW) && netif_carrier_ok(nic->ndev)) {
		struct aq_mss_ingress_sakey_record sa_key_record;
		struct aq_mss_ingress_sa_record sa_record;

		memset(&sa_key_record, 0, sizeof(sa_key_record));
		memset(&sa_record, 0, sizeof(sa_record));
		sa_record.fresh = 1;
		ret = aq_mss_set_ingress_sa_record(hw, &sa_record, sa_idx);
		if (ret)
			return ret;

		return aq_mss_set_ingress_sakey_record(hw, &sa_key_record,
						       sa_idx);
	}

	return ret;
}

static int aq_mdo_del_rxsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	const struct macsec_rx_sc *rx_sc = ctx->sa.rx_sa->sc;
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	int rxsc_idx;
	int ret = 0;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(cfg, rx_sc);
	if (rxsc_idx < 0)
		return -EINVAL;

	ret = aq_clear_rxsa(nic, &cfg->aq_rxsc[rxsc_idx], ctx->sa.assoc_num,
			    AQ_CLEAR_ALL);

	return ret;
}

static int aq_mdo_get_dev_stats(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_common_stats *stats = &nic->macsec_cfg->stats;
	struct aq_hw_s *hw = nic->aq_hw;

	aq_get_macsec_common_stats(hw, stats);

	ctx->stats.dev_stats->OutPktsUntagged = stats->out.untagged_pkts;
	ctx->stats.dev_stats->InPktsUntagged = stats->in.untagged_pkts;
	ctx->stats.dev_stats->OutPktsTooLong = stats->out.too_long;
	ctx->stats.dev_stats->InPktsNoTag = stats->in.notag_pkts;
	ctx->stats.dev_stats->InPktsBadTag = stats->in.bad_tag_pkts;
	ctx->stats.dev_stats->InPktsUnknownSCI = stats->in.unknown_sci_pkts;
	ctx->stats.dev_stats->InPktsNoSCI = stats->in.no_sci_pkts;
	ctx->stats.dev_stats->InPktsOverrun = 0;

	return 0;
}

static int aq_mdo_get_tx_sc_stats(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_tx_sc_stats *stats;
	struct aq_hw_s *hw = nic->aq_hw;
	struct aq_macsec_txsc *aq_txsc;
	int txsc_idx;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, ctx->secy);
	if (txsc_idx < 0)
		return -ENOENT;

	aq_txsc = &nic->macsec_cfg->aq_txsc[txsc_idx];
	stats = &aq_txsc->stats;
	aq_get_txsc_stats(hw, aq_txsc->hw_sc_idx, stats);

	ctx->stats.tx_sc_stats->OutPktsProtected = stats->sc_protected_pkts;
	ctx->stats.tx_sc_stats->OutPktsEncrypted = stats->sc_encrypted_pkts;
	ctx->stats.tx_sc_stats->OutOctetsProtected = stats->sc_protected_octets;
	ctx->stats.tx_sc_stats->OutOctetsEncrypted = stats->sc_encrypted_octets;

	return 0;
}

static int aq_mdo_get_tx_sa_stats(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	struct aq_macsec_tx_sa_stats *stats;
	struct aq_hw_s *hw = nic->aq_hw;
	const struct macsec_secy *secy;
	struct aq_macsec_txsc *aq_txsc;
	struct macsec_tx_sa *tx_sa;
	unsigned int sa_idx;
	int txsc_idx;
	u32 next_pn;
	int ret;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, ctx->secy);
	if (txsc_idx < 0)
		return -EINVAL;

	aq_txsc = &cfg->aq_txsc[txsc_idx];
	sa_idx = aq_txsc->hw_sc_idx | ctx->sa.assoc_num;
	stats = &aq_txsc->tx_sa_stats[ctx->sa.assoc_num];
	ret = aq_get_txsa_stats(hw, sa_idx, stats);
	if (ret)
		return ret;

	ctx->stats.tx_sa_stats->OutPktsProtected = stats->sa_protected_pkts;
	ctx->stats.tx_sa_stats->OutPktsEncrypted = stats->sa_encrypted_pkts;

	secy = aq_txsc->sw_secy;
	tx_sa = rcu_dereference_bh(secy->tx_sc.sa[ctx->sa.assoc_num]);
	ret = aq_get_txsa_next_pn(hw, sa_idx, &next_pn);
	if (ret == 0) {
		spin_lock_bh(&tx_sa->lock);
		tx_sa->next_pn = next_pn;
		spin_unlock_bh(&tx_sa->lock);
	}

	return ret;
}

static int aq_mdo_get_rx_sc_stats(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	struct aq_macsec_rx_sa_stats *stats;
	struct aq_hw_s *hw = nic->aq_hw;
	struct aq_macsec_rxsc *aq_rxsc;
	unsigned int sa_idx;
	int rxsc_idx;
	int ret = 0;
	int i;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(cfg, ctx->rx_sc);
	if (rxsc_idx < 0)
		return -ENOENT;

	aq_rxsc = &cfg->aq_rxsc[rxsc_idx];
	for (i = 0; i < MACSEC_NUM_AN; i++) {
		if (!test_bit(i, &aq_rxsc->rx_sa_idx_busy))
			continue;

		stats = &aq_rxsc->rx_sa_stats[i];
		sa_idx = aq_rxsc->hw_sc_idx | i;
		ret = aq_get_rxsa_stats(hw, sa_idx, stats);
		if (ret)
			break;

		ctx->stats.rx_sc_stats->InOctetsValidated +=
			stats->validated_octets;
		ctx->stats.rx_sc_stats->InOctetsDecrypted +=
			stats->decrypted_octets;
		ctx->stats.rx_sc_stats->InPktsUnchecked +=
			stats->unchecked_pkts;
		ctx->stats.rx_sc_stats->InPktsDelayed += stats->delayed_pkts;
		ctx->stats.rx_sc_stats->InPktsOK += stats->ok_pkts;
		ctx->stats.rx_sc_stats->InPktsInvalid += stats->invalid_pkts;
		ctx->stats.rx_sc_stats->InPktsLate += stats->late_pkts;
		ctx->stats.rx_sc_stats->InPktsNotValid += stats->not_valid_pkts;
		ctx->stats.rx_sc_stats->InPktsNotUsingSA += stats->not_using_sa;
		ctx->stats.rx_sc_stats->InPktsUnusedSA += stats->unused_sa;
	}

	return ret;
}

static int aq_mdo_get_rx_sa_stats(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	struct aq_macsec_rx_sa_stats *stats;
	struct aq_hw_s *hw = nic->aq_hw;
	struct aq_macsec_rxsc *aq_rxsc;
	struct macsec_rx_sa *rx_sa;
	unsigned int sa_idx;
	int rxsc_idx;
	u32 next_pn;
	int ret;

	rxsc_idx = aq_get_rxsc_idx_from_rxsc(cfg, ctx->rx_sc);
	if (rxsc_idx < 0)
		return -EINVAL;

	aq_rxsc = &cfg->aq_rxsc[rxsc_idx];
	stats = &aq_rxsc->rx_sa_stats[ctx->sa.assoc_num];
	sa_idx = aq_rxsc->hw_sc_idx | ctx->sa.assoc_num;
	ret = aq_get_rxsa_stats(hw, sa_idx, stats);
	if (ret)
		return ret;

	ctx->stats.rx_sa_stats->InPktsOK = stats->ok_pkts;
	ctx->stats.rx_sa_stats->InPktsInvalid = stats->invalid_pkts;
	ctx->stats.rx_sa_stats->InPktsNotValid = stats->not_valid_pkts;
	ctx->stats.rx_sa_stats->InPktsNotUsingSA = stats->not_using_sa;
	ctx->stats.rx_sa_stats->InPktsUnusedSA = stats->unused_sa;

	rx_sa = rcu_dereference_bh(aq_rxsc->sw_rxsc->sa[ctx->sa.assoc_num]);
	ret = aq_get_rxsa_next_pn(hw, sa_idx, &next_pn);
	if (ret == 0) {
		spin_lock_bh(&rx_sa->lock);
		rx_sa->next_pn = next_pn;
		spin_unlock_bh(&rx_sa->lock);
	}

	return ret;
}

static int apply_txsc_cfg(struct aq_nic_s *nic, const int txsc_idx)
{
	struct aq_macsec_txsc *aq_txsc = &nic->macsec_cfg->aq_txsc[txsc_idx];
	const struct macsec_secy *secy = aq_txsc->sw_secy;
	struct macsec_tx_sa *tx_sa;
	int ret = 0;
	int i;

	if (!netif_running(secy->netdev))
		return ret;

	ret = aq_set_txsc(nic, txsc_idx);
	if (ret)
		return ret;

	for (i = 0; i < MACSEC_NUM_AN; i++) {
		tx_sa = rcu_dereference_bh(secy->tx_sc.sa[i]);
		if (tx_sa) {
			ret = aq_update_txsa(nic, aq_txsc->hw_sc_idx, secy,
					     tx_sa, aq_txsc->tx_sa_key[i], i);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int apply_rxsc_cfg(struct aq_nic_s *nic, const int rxsc_idx)
{
	struct aq_macsec_rxsc *aq_rxsc = &nic->macsec_cfg->aq_rxsc[rxsc_idx];
	const struct macsec_secy *secy = aq_rxsc->sw_secy;
	struct macsec_rx_sa *rx_sa;
	int ret = 0;
	int i;

	if (!netif_running(secy->netdev))
		return ret;

	ret = aq_set_rxsc(nic, rxsc_idx);
	if (ret)
		return ret;

	for (i = 0; i < MACSEC_NUM_AN; i++) {
		rx_sa = rcu_dereference_bh(aq_rxsc->sw_rxsc->sa[i]);
		if (rx_sa) {
			ret = aq_update_rxsa(nic, aq_rxsc->hw_sc_idx, secy,
					     rx_sa, aq_rxsc->rx_sa_key[i], i);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int aq_clear_secy(struct aq_nic_s *nic, const struct macsec_secy *secy,
			 enum aq_clear_type clear_type)
{
	struct macsec_rx_sc *rx_sc;
	int txsc_idx;
	int rxsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, secy);
	if (txsc_idx >= 0) {
		ret = aq_clear_txsc(nic, txsc_idx, clear_type);
		if (ret)
			return ret;
	}

	for (rx_sc = rcu_dereference_bh(secy->rx_sc); rx_sc;
	     rx_sc = rcu_dereference_bh(rx_sc->next)) {
		rxsc_idx = aq_get_rxsc_idx_from_rxsc(nic->macsec_cfg, rx_sc);
		if (rxsc_idx < 0)
			continue;

		ret = aq_clear_rxsc(nic, rxsc_idx, clear_type);
		if (ret)
			return ret;
	}

	return ret;
}

static int aq_apply_secy_cfg(struct aq_nic_s *nic,
			     const struct macsec_secy *secy)
{
	struct macsec_rx_sc *rx_sc;
	int txsc_idx;
	int rxsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, secy);
	if (txsc_idx >= 0)
		apply_txsc_cfg(nic, txsc_idx);

	for (rx_sc = rcu_dereference_bh(secy->rx_sc); rx_sc && rx_sc->active;
	     rx_sc = rcu_dereference_bh(rx_sc->next)) {
		rxsc_idx = aq_get_rxsc_idx_from_rxsc(nic->macsec_cfg, rx_sc);
		if (unlikely(rxsc_idx < 0))
			continue;

		ret = apply_rxsc_cfg(nic, rxsc_idx);
		if (ret)
			return ret;
	}

	return ret;
}

static int aq_apply_macsec_cfg(struct aq_nic_s *nic)
{
	int ret = 0;
	int i;

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (nic->macsec_cfg->txsc_idx_busy & BIT(i)) {
			ret = apply_txsc_cfg(nic, i);
			if (ret)
				return ret;
		}
	}

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (nic->macsec_cfg->rxsc_idx_busy & BIT(i)) {
			ret = apply_rxsc_cfg(nic, i);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int aq_sa_from_sa_idx(const enum aq_macsec_sc_sa sc_sa, const int sa_idx)
{
	switch (sc_sa) {
	case aq_macsec_sa_sc_4sa_8sc:
		return sa_idx & 3;
	case aq_macsec_sa_sc_2sa_16sc:
		return sa_idx & 1;
	case aq_macsec_sa_sc_1sa_32sc:
		return 0;
	default:
		WARN_ONCE(true, "Invalid sc_sa");
	}
	return -EINVAL;
}

static int aq_sc_idx_from_sa_idx(const enum aq_macsec_sc_sa sc_sa,
				 const int sa_idx)
{
	switch (sc_sa) {
	case aq_macsec_sa_sc_4sa_8sc:
		return sa_idx & ~3;
	case aq_macsec_sa_sc_2sa_16sc:
		return sa_idx & ~1;
	case aq_macsec_sa_sc_1sa_32sc:
		return sa_idx;
	default:
		WARN_ONCE(true, "Invalid sc_sa");
	}
	return -EINVAL;
}

static void aq_check_txsa_expiration(struct aq_nic_s *nic)
{
	u32 egress_sa_expired, egress_sa_threshold_expired;
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	struct aq_hw_s *hw = nic->aq_hw;
	struct aq_macsec_txsc *aq_txsc;
	const struct macsec_secy *secy;
	int sc_idx = 0, txsc_idx = 0;
	enum aq_macsec_sc_sa sc_sa;
	struct macsec_tx_sa *tx_sa;
	unsigned char an = 0;
	int ret;
	int i;

	sc_sa = cfg->sc_sa;

	ret = aq_mss_get_egress_sa_expired(hw, &egress_sa_expired);
	if (unlikely(ret))
		return;

	ret = aq_mss_get_egress_sa_threshold_expired(hw,
		&egress_sa_threshold_expired);

	for (i = 0; i < AQ_MACSEC_MAX_SA; i++) {
		if (egress_sa_expired & BIT(i)) {
			an = aq_sa_from_sa_idx(sc_sa, i);
			sc_idx = aq_sc_idx_from_sa_idx(sc_sa, i);
			txsc_idx = aq_get_txsc_idx_from_sc_idx(sc_sa, sc_idx);
			if (txsc_idx < 0)
				continue;

			aq_txsc = &cfg->aq_txsc[txsc_idx];
			if (!(cfg->txsc_idx_busy & BIT(txsc_idx))) {
				netdev_warn(nic->ndev,
					"PN threshold expired on invalid TX SC");
				continue;
			}

			secy = aq_txsc->sw_secy;
			if (!netif_running(secy->netdev)) {
				netdev_warn(nic->ndev,
					"PN threshold expired on down TX SC");
				continue;
			}

			if (unlikely(!(aq_txsc->tx_sa_idx_busy & BIT(an)))) {
				netdev_warn(nic->ndev,
					"PN threshold expired on invalid TX SA");
				continue;
			}

			tx_sa = rcu_dereference_bh(secy->tx_sc.sa[an]);
			macsec_pn_wrapped((struct macsec_secy *)secy, tx_sa);
		}
	}

	aq_mss_set_egress_sa_expired(hw, egress_sa_expired);
	if (likely(!ret))
		aq_mss_set_egress_sa_threshold_expired(hw,
			egress_sa_threshold_expired);
}

#define AQ_LOCKED_MDO_DEF(mdo)						\
static int aq_locked_mdo_##mdo(struct macsec_context *ctx)		\
{									\
	struct aq_nic_s *nic = macsec_netdev_priv(ctx->netdev);		\
	int ret;							\
	mutex_lock(&nic->macsec_mutex);					\
	ret = aq_mdo_##mdo(ctx);					\
	mutex_unlock(&nic->macsec_mutex);				\
	return ret;							\
}

AQ_LOCKED_MDO_DEF(dev_open)
AQ_LOCKED_MDO_DEF(dev_stop)
AQ_LOCKED_MDO_DEF(add_secy)
AQ_LOCKED_MDO_DEF(upd_secy)
AQ_LOCKED_MDO_DEF(del_secy)
AQ_LOCKED_MDO_DEF(add_rxsc)
AQ_LOCKED_MDO_DEF(upd_rxsc)
AQ_LOCKED_MDO_DEF(del_rxsc)
AQ_LOCKED_MDO_DEF(add_rxsa)
AQ_LOCKED_MDO_DEF(upd_rxsa)
AQ_LOCKED_MDO_DEF(del_rxsa)
AQ_LOCKED_MDO_DEF(add_txsa)
AQ_LOCKED_MDO_DEF(upd_txsa)
AQ_LOCKED_MDO_DEF(del_txsa)
AQ_LOCKED_MDO_DEF(get_dev_stats)
AQ_LOCKED_MDO_DEF(get_tx_sc_stats)
AQ_LOCKED_MDO_DEF(get_tx_sa_stats)
AQ_LOCKED_MDO_DEF(get_rx_sc_stats)
AQ_LOCKED_MDO_DEF(get_rx_sa_stats)

const struct macsec_ops aq_macsec_ops = {
	.mdo_dev_open = aq_locked_mdo_dev_open,
	.mdo_dev_stop = aq_locked_mdo_dev_stop,
	.mdo_add_secy = aq_locked_mdo_add_secy,
	.mdo_upd_secy = aq_locked_mdo_upd_secy,
	.mdo_del_secy = aq_locked_mdo_del_secy,
	.mdo_add_rxsc = aq_locked_mdo_add_rxsc,
	.mdo_upd_rxsc = aq_locked_mdo_upd_rxsc,
	.mdo_del_rxsc = aq_locked_mdo_del_rxsc,
	.mdo_add_rxsa = aq_locked_mdo_add_rxsa,
	.mdo_upd_rxsa = aq_locked_mdo_upd_rxsa,
	.mdo_del_rxsa = aq_locked_mdo_del_rxsa,
	.mdo_add_txsa = aq_locked_mdo_add_txsa,
	.mdo_upd_txsa = aq_locked_mdo_upd_txsa,
	.mdo_del_txsa = aq_locked_mdo_del_txsa,
	.mdo_get_dev_stats = aq_locked_mdo_get_dev_stats,
	.mdo_get_tx_sc_stats = aq_locked_mdo_get_tx_sc_stats,
	.mdo_get_tx_sa_stats = aq_locked_mdo_get_tx_sa_stats,
	.mdo_get_rx_sc_stats = aq_locked_mdo_get_rx_sc_stats,
	.mdo_get_rx_sa_stats = aq_locked_mdo_get_rx_sa_stats,
};

int aq_macsec_init(struct aq_nic_s *nic)
{
	struct aq_macsec_cfg *cfg;
	u32 caps_lo;

	if (!nic->aq_fw_ops->get_link_capabilities)
		return 0;

	caps_lo = nic->aq_fw_ops->get_link_capabilities(nic->aq_hw);

	if (!(caps_lo & BIT(CAPS_LO_MACSEC)))
		return 0;

	nic->macsec_cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!nic->macsec_cfg)
		return -ENOMEM;

	nic->ndev->features |= NETIF_F_HW_MACSEC;
	nic->ndev->macsec_ops = &aq_macsec_ops;
	mutex_init(&nic->macsec_mutex);

	return 0;
}

void aq_macsec_free(struct aq_nic_s *nic)
{
	kfree(nic->macsec_cfg);
	nic->macsec_cfg = NULL;
}

int aq_macsec_enable(struct aq_nic_s *nic)
{
	u32 ctl_ether_types[1] = { ETH_P_PAE };
	struct macsec_msg_fw_response resp = { 0 };
	struct macsec_msg_fw_request msg = { 0 };
	struct aq_hw_s *hw = nic->aq_hw;
	int num_ctl_ether_types = 0;
	int index = 0, tbl_idx;
	int ret;

	if (!nic->macsec_cfg)
		return 0;

	mutex_lock(&nic->macsec_mutex);

	if (nic->aq_fw_ops->send_macsec_req) {
		struct macsec_cfg_request cfg = { 0 };

		cfg.enabled = 1;
		cfg.egress_threshold = 0xffffffff;
		cfg.ingress_threshold = 0xffffffff;
		cfg.interrupts_enabled = 1;

		msg.msg_type = macsec_cfg_msg;
		msg.cfg = cfg;

		ret = nic->aq_fw_ops->send_macsec_req(hw, &msg, &resp);
		if (ret)
			goto unlock;
	}

	/* Init Ethertype bypass filters */
	for (index = 0; index < ARRAY_SIZE(ctl_ether_types); index++) {
		struct aq_mss_ingress_prectlf_record rx_prectlf_rec;
		struct aq_mss_egress_ctlf_record tx_ctlf_rec;

		if (ctl_ether_types[index] == 0)
			continue;

		memset(&tx_ctlf_rec, 0, sizeof(tx_ctlf_rec));
		tx_ctlf_rec.eth_type = ctl_ether_types[index];
		tx_ctlf_rec.match_type = 4; /* Match eth_type only */
		tx_ctlf_rec.match_mask = 0xf; /* match for eth_type */
		tx_ctlf_rec.action = 0; /* Bypass MACSEC modules */
		tbl_idx = NUMROWS_EGRESSCTLFRECORD - num_ctl_ether_types - 1;
		aq_mss_set_egress_ctlf_record(hw, &tx_ctlf_rec, tbl_idx);

		memset(&rx_prectlf_rec, 0, sizeof(rx_prectlf_rec));
		rx_prectlf_rec.eth_type = ctl_ether_types[index];
		rx_prectlf_rec.match_type = 4; /* Match eth_type only */
		rx_prectlf_rec.match_mask = 0xf; /* match for eth_type */
		rx_prectlf_rec.action = 0; /* Bypass MACSEC modules */
		tbl_idx =
			NUMROWS_INGRESSPRECTLFRECORD - num_ctl_ether_types - 1;
		aq_mss_set_ingress_prectlf_record(hw, &rx_prectlf_rec, tbl_idx);

		num_ctl_ether_types++;
	}

	ret = aq_apply_macsec_cfg(nic);

unlock:
	mutex_unlock(&nic->macsec_mutex);
	return ret;
}

void aq_macsec_work(struct aq_nic_s *nic)
{
	if (!nic->macsec_cfg)
		return;

	if (!netif_carrier_ok(nic->ndev))
		return;

	mutex_lock(&nic->macsec_mutex);
	aq_check_txsa_expiration(nic);
	mutex_unlock(&nic->macsec_mutex);
}

int aq_macsec_rx_sa_cnt(struct aq_nic_s *nic)
{
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	int i, cnt = 0;

	if (!cfg)
		return 0;

	mutex_lock(&nic->macsec_mutex);

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (!test_bit(i, &cfg->rxsc_idx_busy))
			continue;
		cnt += hweight_long(cfg->aq_rxsc[i].rx_sa_idx_busy);
	}

	mutex_unlock(&nic->macsec_mutex);
	return cnt;
}

int aq_macsec_tx_sc_cnt(struct aq_nic_s *nic)
{
	int cnt;

	if (!nic->macsec_cfg)
		return 0;

	mutex_lock(&nic->macsec_mutex);
	cnt = hweight_long(nic->macsec_cfg->txsc_idx_busy);
	mutex_unlock(&nic->macsec_mutex);

	return cnt;
}

int aq_macsec_tx_sa_cnt(struct aq_nic_s *nic)
{
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	int i, cnt = 0;

	if (!cfg)
		return 0;

	mutex_lock(&nic->macsec_mutex);

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (!test_bit(i, &cfg->txsc_idx_busy))
			continue;
		cnt += hweight_long(cfg->aq_txsc[i].tx_sa_idx_busy);
	}

	mutex_unlock(&nic->macsec_mutex);
	return cnt;
}

static int aq_macsec_update_stats(struct aq_nic_s *nic)
{
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	struct aq_hw_s *hw = nic->aq_hw;
	struct aq_macsec_txsc *aq_txsc;
	struct aq_macsec_rxsc *aq_rxsc;
	int i, sa_idx, assoc_num;
	int ret = 0;

	aq_get_macsec_common_stats(hw, &cfg->stats);

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (!(cfg->txsc_idx_busy & BIT(i)))
			continue;
		aq_txsc = &cfg->aq_txsc[i];

		ret = aq_get_txsc_stats(hw, aq_txsc->hw_sc_idx,
					&aq_txsc->stats);
		if (ret)
			return ret;

		for (assoc_num = 0; assoc_num < MACSEC_NUM_AN; assoc_num++) {
			if (!test_bit(assoc_num, &aq_txsc->tx_sa_idx_busy))
				continue;
			sa_idx = aq_txsc->hw_sc_idx | assoc_num;
			ret = aq_get_txsa_stats(hw, sa_idx,
					      &aq_txsc->tx_sa_stats[assoc_num]);
			if (ret)
				return ret;
		}
	}

	for (i = 0; i < AQ_MACSEC_MAX_SC; i++) {
		if (!(test_bit(i, &cfg->rxsc_idx_busy)))
			continue;
		aq_rxsc = &cfg->aq_rxsc[i];

		for (assoc_num = 0; assoc_num < MACSEC_NUM_AN; assoc_num++) {
			if (!test_bit(assoc_num, &aq_rxsc->rx_sa_idx_busy))
				continue;
			sa_idx = aq_rxsc->hw_sc_idx | assoc_num;

			ret = aq_get_rxsa_stats(hw, sa_idx,
					      &aq_rxsc->rx_sa_stats[assoc_num]);
			if (ret)
				return ret;
		}
	}

	return ret;
}

u64 *aq_macsec_get_stats(struct aq_nic_s *nic, u64 *data)
{
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	struct aq_macsec_common_stats *common_stats;
	struct aq_macsec_tx_sc_stats *txsc_stats;
	struct aq_macsec_tx_sa_stats *txsa_stats;
	struct aq_macsec_rx_sa_stats *rxsa_stats;
	struct aq_macsec_txsc *aq_txsc;
	struct aq_macsec_rxsc *aq_rxsc;
	unsigned int assoc_num;
	unsigned int sc_num;
	unsigned int i = 0U;

	if (!cfg)
		return data;

	mutex_lock(&nic->macsec_mutex);

	aq_macsec_update_stats(nic);

	common_stats = &cfg->stats;
	data[i] = common_stats->in.ctl_pkts;
	data[++i] = common_stats->in.tagged_miss_pkts;
	data[++i] = common_stats->in.untagged_miss_pkts;
	data[++i] = common_stats->in.notag_pkts;
	data[++i] = common_stats->in.untagged_pkts;
	data[++i] = common_stats->in.bad_tag_pkts;
	data[++i] = common_stats->in.no_sci_pkts;
	data[++i] = common_stats->in.unknown_sci_pkts;
	data[++i] = common_stats->in.ctrl_prt_pass_pkts;
	data[++i] = common_stats->in.unctrl_prt_pass_pkts;
	data[++i] = common_stats->in.ctrl_prt_fail_pkts;
	data[++i] = common_stats->in.unctrl_prt_fail_pkts;
	data[++i] = common_stats->in.too_long_pkts;
	data[++i] = common_stats->in.igpoc_ctl_pkts;
	data[++i] = common_stats->in.ecc_error_pkts;
	data[++i] = common_stats->in.unctrl_hit_drop_redir;
	data[++i] = common_stats->out.ctl_pkts;
	data[++i] = common_stats->out.unknown_sa_pkts;
	data[++i] = common_stats->out.untagged_pkts;
	data[++i] = common_stats->out.too_long;
	data[++i] = common_stats->out.ecc_error_pkts;
	data[++i] = common_stats->out.unctrl_hit_drop_redir;

	for (sc_num = 0; sc_num < AQ_MACSEC_MAX_SC; sc_num++) {
		if (!(test_bit(sc_num, &cfg->txsc_idx_busy)))
			continue;

		aq_txsc = &cfg->aq_txsc[sc_num];
		txsc_stats = &aq_txsc->stats;

		data[++i] = txsc_stats->sc_protected_pkts;
		data[++i] = txsc_stats->sc_encrypted_pkts;
		data[++i] = txsc_stats->sc_protected_octets;
		data[++i] = txsc_stats->sc_encrypted_octets;

		for (assoc_num = 0; assoc_num < MACSEC_NUM_AN; assoc_num++) {
			if (!test_bit(assoc_num, &aq_txsc->tx_sa_idx_busy))
				continue;

			txsa_stats = &aq_txsc->tx_sa_stats[assoc_num];

			data[++i] = txsa_stats->sa_hit_drop_redirect;
			data[++i] = txsa_stats->sa_protected2_pkts;
			data[++i] = txsa_stats->sa_protected_pkts;
			data[++i] = txsa_stats->sa_encrypted_pkts;
		}
	}

	for (sc_num = 0; sc_num < AQ_MACSEC_MAX_SC; sc_num++) {
		if (!(test_bit(sc_num, &cfg->rxsc_idx_busy)))
			continue;

		aq_rxsc = &cfg->aq_rxsc[sc_num];

		for (assoc_num = 0; assoc_num < MACSEC_NUM_AN; assoc_num++) {
			if (!test_bit(assoc_num, &aq_rxsc->rx_sa_idx_busy))
				continue;

			rxsa_stats = &aq_rxsc->rx_sa_stats[assoc_num];

			data[++i] = rxsa_stats->untagged_hit_pkts;
			data[++i] = rxsa_stats->ctrl_hit_drop_redir_pkts;
			data[++i] = rxsa_stats->not_using_sa;
			data[++i] = rxsa_stats->unused_sa;
			data[++i] = rxsa_stats->not_valid_pkts;
			data[++i] = rxsa_stats->invalid_pkts;
			data[++i] = rxsa_stats->ok_pkts;
			data[++i] = rxsa_stats->late_pkts;
			data[++i] = rxsa_stats->delayed_pkts;
			data[++i] = rxsa_stats->unchecked_pkts;
			data[++i] = rxsa_stats->validated_octets;
			data[++i] = rxsa_stats->decrypted_octets;
		}
	}

	i++;

	data += i;

	mutex_unlock(&nic->macsec_mutex);

	return data;
}
