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
static int aq_clear_secy(struct aq_nic_s *nic, const struct macsec_secy *secy,
			 enum aq_clear_type clear_type);
static int aq_apply_macsec_cfg(struct aq_nic_s *nic);
static int aq_apply_secy_cfg(struct aq_nic_s *nic,
			     const struct macsec_secy *secy);

static void aq_ether_addr_to_mac(u32 mac[2], unsigned char *emac)
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

static int aq_mdo_dev_open(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	int ret = 0;

	if (ctx->prepare)
		return 0;

	if (netif_carrier_ok(nic->ndev))
		ret = aq_apply_secy_cfg(nic, ctx->secy);

	return ret;
}

static int aq_mdo_dev_stop(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	int i;

	if (ctx->prepare)
		return 0;

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
	};

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
	};

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
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	enum aq_macsec_sc_sa sc_sa;
	u32 txsc_idx;
	int ret = 0;

	sc_sa = sc_sa_from_num_an(MACSEC_NUM_AN);
	if (sc_sa == aq_macsec_sa_sc_not_used)
		return -EINVAL;

	if (hweight32(cfg->txsc_idx_busy) >= aq_sc_idx_max(sc_sa))
		return -ENOSPC;

	txsc_idx = ffz(cfg->txsc_idx_busy);
	if (txsc_idx == AQ_MACSEC_MAX_SC)
		return -ENOSPC;

	if (ctx->prepare)
		return 0;

	cfg->sc_sa = sc_sa;
	cfg->aq_txsc[txsc_idx].hw_sc_idx = aq_to_hw_sc_idx(txsc_idx, sc_sa);
	cfg->aq_txsc[txsc_idx].sw_secy = secy;

	if (netif_carrier_ok(nic->ndev) && netif_running(secy->netdev))
		ret = aq_set_txsc(nic, txsc_idx);

	set_bit(txsc_idx, &cfg->txsc_idx_busy);

	return 0;
}

static int aq_mdo_upd_secy(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	const struct macsec_secy *secy = ctx->secy;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, secy);
	if (txsc_idx < 0)
		return -ENOENT;

	if (ctx->prepare)
		return 0;

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
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	int ret = 0;

	if (ctx->prepare)
		return 0;

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
	struct aq_mss_egress_sakey_record key_rec;
	const unsigned int sa_idx = sc_idx | an;
	struct aq_mss_egress_sa_record sa_rec;
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	memset(&sa_rec, 0, sizeof(sa_rec));
	sa_rec.valid = tx_sa->active;
	sa_rec.fresh = 1;
	sa_rec.next_pn = tx_sa->next_pn;

	ret = aq_mss_set_egress_sa_record(hw, &sa_rec, sa_idx);
	if (ret)
		return ret;

	if (!key)
		return ret;

	memset(&key_rec, 0, sizeof(key_rec));
	memcpy(&key_rec.key, key, secy->key_len);

	aq_rotate_keys(&key_rec.key, secy->key_len);

	ret = aq_mss_set_egress_sakey_record(hw, &key_rec, sa_idx);

	return ret;
}

static int aq_mdo_add_txsa(struct macsec_context *ctx)
{
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	struct aq_macsec_txsc *aq_txsc;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, secy);
	if (txsc_idx < 0)
		return -EINVAL;

	if (ctx->prepare)
		return 0;

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
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	const struct macsec_secy *secy = ctx->secy;
	struct aq_macsec_txsc *aq_txsc;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, secy);
	if (txsc_idx < 0)
		return -EINVAL;

	if (ctx->prepare)
		return 0;

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
	struct aq_nic_s *nic = netdev_priv(ctx->netdev);
	struct aq_macsec_cfg *cfg = nic->macsec_cfg;
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(cfg, ctx->secy);
	if (txsc_idx < 0)
		return -EINVAL;

	if (ctx->prepare)
		return 0;

	ret = aq_clear_txsa(nic, &cfg->aq_txsc[txsc_idx], ctx->sa.assoc_num,
			    AQ_CLEAR_ALL);

	return ret;
}

static int aq_mdo_add_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_upd_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_del_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_add_rxsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_upd_rxsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_del_rxsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
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

static int aq_clear_secy(struct aq_nic_s *nic, const struct macsec_secy *secy,
			 enum aq_clear_type clear_type)
{
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, secy);
	if (txsc_idx >= 0) {
		ret = aq_clear_txsc(nic, txsc_idx, clear_type);
		if (ret)
			return ret;
	}

	return ret;
}

static int aq_apply_secy_cfg(struct aq_nic_s *nic,
			     const struct macsec_secy *secy)
{
	int txsc_idx;
	int ret = 0;

	txsc_idx = aq_get_txsc_idx_from_secy(nic->macsec_cfg, secy);
	if (txsc_idx >= 0)
		apply_txsc_cfg(nic, txsc_idx);

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

const struct macsec_ops aq_macsec_ops = {
	.mdo_dev_open = aq_mdo_dev_open,
	.mdo_dev_stop = aq_mdo_dev_stop,
	.mdo_add_secy = aq_mdo_add_secy,
	.mdo_upd_secy = aq_mdo_upd_secy,
	.mdo_del_secy = aq_mdo_del_secy,
	.mdo_add_rxsc = aq_mdo_add_rxsc,
	.mdo_upd_rxsc = aq_mdo_upd_rxsc,
	.mdo_del_rxsc = aq_mdo_del_rxsc,
	.mdo_add_rxsa = aq_mdo_add_rxsa,
	.mdo_upd_rxsa = aq_mdo_upd_rxsa,
	.mdo_del_rxsa = aq_mdo_del_rxsa,
	.mdo_add_txsa = aq_mdo_add_txsa,
	.mdo_upd_txsa = aq_mdo_upd_txsa,
	.mdo_del_txsa = aq_mdo_del_txsa,
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

	rtnl_lock();

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

		num_ctl_ether_types++;
	}

	ret = aq_apply_macsec_cfg(nic);

unlock:
	rtnl_unlock();
	return ret;
}

void aq_macsec_work(struct aq_nic_s *nic)
{
	if (!nic->macsec_cfg)
		return;

	if (!netif_carrier_ok(nic->ndev))
		return;

	rtnl_lock();
	aq_check_txsa_expiration(nic);
	rtnl_unlock();
}
