/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ath9k.h"

static const struct wiphy_wowlan_support ath9k_wowlan_support_legacy = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = MAX_NUM_USER_PATTERN,
	.pattern_min_len = 1,
	.pattern_max_len = MAX_PATTERN_SIZE,
};

static const struct wiphy_wowlan_support ath9k_wowlan_support = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = MAX_NUM_PATTERN - 2,
	.pattern_min_len = 1,
	.pattern_max_len = MAX_PATTERN_SIZE,
};

static u8 ath9k_wow_map_triggers(struct ath_softc *sc,
				 struct cfg80211_wowlan *wowlan)
{
	u8 wow_triggers = 0;

	if (wowlan->disconnect)
		wow_triggers |= AH_WOW_LINK_CHANGE |
				AH_WOW_BEACON_MISS;
	if (wowlan->magic_pkt)
		wow_triggers |= AH_WOW_MAGIC_PATTERN_EN;

	if (wowlan->n_patterns)
		wow_triggers |= AH_WOW_USER_PATTERN_EN;

	return wow_triggers;
}

static int ath9k_wow_add_disassoc_deauth_pattern(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int pattern_count = 0;
	int ret, i, byte_cnt = 0;
	u8 dis_deauth_pattern[MAX_PATTERN_SIZE];
	u8 dis_deauth_mask[MAX_PATTERN_SIZE];

	memset(dis_deauth_pattern, 0, MAX_PATTERN_SIZE);
	memset(dis_deauth_mask, 0, MAX_PATTERN_SIZE);

	/*
	 * Create Disassociate / Deauthenticate packet filter
	 *
	 *     2 bytes        2 byte    6 bytes   6 bytes  6 bytes
	 *  +--------------+----------+---------+--------+--------+----
	 *  + Frame Control+ Duration +   DA    +  SA    +  BSSID +
	 *  +--------------+----------+---------+--------+--------+----
	 *
	 * The above is the management frame format for disassociate/
	 * deauthenticate pattern, from this we need to match the first byte
	 * of 'Frame Control' and DA, SA, and BSSID fields
	 * (skipping 2nd byte of FC and Duration field.
	 *
	 * Disassociate pattern
	 * --------------------
	 * Frame control = 00 00 1010
	 * DA, SA, BSSID = x:x:x:x:x:x
	 * Pattern will be A0000000 | x:x:x:x:x:x | x:x:x:x:x:x
	 *			    | x:x:x:x:x:x  -- 22 bytes
	 *
	 * Deauthenticate pattern
	 * ----------------------
	 * Frame control = 00 00 1100
	 * DA, SA, BSSID = x:x:x:x:x:x
	 * Pattern will be C0000000 | x:x:x:x:x:x | x:x:x:x:x:x
	 *			    | x:x:x:x:x:x  -- 22 bytes
	 */

	/* Fill out the mask with all FF's */
	for (i = 0; i < MAX_PATTERN_MASK_SIZE; i++)
		dis_deauth_mask[i] = 0xff;

	/* copy the first byte of frame control field */
	dis_deauth_pattern[byte_cnt] = 0xa0;
	byte_cnt++;

	/* skip 2nd byte of frame control and Duration field */
	byte_cnt += 3;

	/*
	 * need not match the destination mac address, it can be a broadcast
	 * mac address or an unicast to this station
	 */
	byte_cnt += 6;

	/* copy the source mac address */
	memcpy((dis_deauth_pattern + byte_cnt), common->curbssid, ETH_ALEN);

	byte_cnt += 6;

	/* copy the bssid, its same as the source mac address */
	memcpy((dis_deauth_pattern + byte_cnt), common->curbssid, ETH_ALEN);

	/* Create Disassociate pattern mask */
	dis_deauth_mask[0] = 0xfe;
	dis_deauth_mask[1] = 0x03;
	dis_deauth_mask[2] = 0xc0;

	ret = ath9k_hw_wow_apply_pattern(ah, dis_deauth_pattern, dis_deauth_mask,
					 pattern_count, byte_cnt);
	if (ret)
		goto exit;

	pattern_count++;
	/*
	 * for de-authenticate pattern, only the first byte of the frame
	 * control field gets changed from 0xA0 to 0xC0
	 */
	dis_deauth_pattern[0] = 0xC0;

	ret = ath9k_hw_wow_apply_pattern(ah, dis_deauth_pattern, dis_deauth_mask,
					 pattern_count, byte_cnt);
exit:
	return ret;
}

static int ath9k_wow_add_pattern(struct ath_softc *sc,
				 struct cfg80211_wowlan *wowlan)
{
	struct ath_hw *ah = sc->sc_ah;
	struct cfg80211_pkt_pattern *patterns = wowlan->patterns;
	u8 wow_pattern[MAX_PATTERN_SIZE];
	u8 wow_mask[MAX_PATTERN_SIZE];
	int mask_len, ret = 0;
	s8 i = 0;

	for (i = 0; i < wowlan->n_patterns; i++) {
		mask_len = DIV_ROUND_UP(patterns[i].pattern_len, 8);
		memset(wow_pattern, 0, MAX_PATTERN_SIZE);
		memset(wow_mask, 0, MAX_PATTERN_SIZE);
		memcpy(wow_pattern, patterns[i].pattern, patterns[i].pattern_len);
		memcpy(wow_mask, patterns[i].mask, mask_len);

		ret = ath9k_hw_wow_apply_pattern(ah,
						 wow_pattern,
						 wow_mask,
						 i + 2,
						 patterns[i].pattern_len);
		if (ret)
			break;
	}

	return ret;
}

int ath9k_suspend(struct ieee80211_hw *hw,
		  struct cfg80211_wowlan *wowlan)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u8 triggers;
	int ret = 0;

	ath9k_deinit_channel_context(sc);

	mutex_lock(&sc->mutex);

	if (test_bit(ATH_OP_INVALID, &common->op_flags)) {
		ath_err(common, "Device not present\n");
		ret = -ENODEV;
		goto fail_wow;
	}

	if (WARN_ON(!wowlan)) {
		ath_err(common, "None of the WoW triggers enabled\n");
		ret = -EINVAL;
		goto fail_wow;
	}

	if (sc->cur_chan->nvifs > 1) {
		ath_dbg(common, WOW, "WoW for multivif is not yet supported\n");
		ret = 1;
		goto fail_wow;
	}

	if (ath9k_is_chanctx_enabled()) {
		if (test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags)) {
			ath_dbg(common, WOW,
				"Multi-channel WOW is not supported\n");
			ret = 1;
			goto fail_wow;
		}
	}

	if (!test_bit(ATH_OP_PRIM_STA_VIF, &common->op_flags)) {
		ath_dbg(common, WOW, "None of the STA vifs are associated\n");
		ret = 1;
		goto fail_wow;
	}

	triggers = ath9k_wow_map_triggers(sc, wowlan);
	if (!triggers) {
		ath_dbg(common, WOW, "No valid WoW triggers\n");
		ret = 1;
		goto fail_wow;
	}

	ath_cancel_work(sc);
	ath_stop_ani(sc);

	ath9k_ps_wakeup(sc);

	ath9k_stop_btcoex(sc);

	/*
	 * Enable wake up on receiving disassoc/deauth
	 * frame by default.
	 */
	ret = ath9k_wow_add_disassoc_deauth_pattern(sc);
	if (ret) {
		ath_err(common,
			"Unable to add disassoc/deauth pattern: %d\n", ret);
		goto fail_wow;
	}

	if (triggers & AH_WOW_USER_PATTERN_EN) {
		ret = ath9k_wow_add_pattern(sc, wowlan);
		if (ret) {
			ath_err(common,
				"Unable to add user pattern: %d\n", ret);
			goto fail_wow;
		}
	}

	spin_lock_bh(&sc->sc_pcu_lock);
	/*
	 * To avoid false wake, we enable beacon miss interrupt only
	 * when we go to sleep. We save the current interrupt mask
	 * so we can restore it after the system wakes up
	 */
	sc->wow_intr_before_sleep = ah->imask;
	ah->imask &= ~ATH9K_INT_GLOBAL;
	ath9k_hw_disable_interrupts(ah);
	ah->imask = ATH9K_INT_BMISS | ATH9K_INT_GLOBAL;
	ath9k_hw_set_interrupts(ah);
	ath9k_hw_enable_interrupts(ah);

	spin_unlock_bh(&sc->sc_pcu_lock);

	/*
	 * we can now sync irq and kill any running tasklets, since we already
	 * disabled interrupts and not holding a spin lock
	 */
	synchronize_irq(sc->irq);
	tasklet_kill(&sc->intr_tq);

	ath9k_hw_wow_enable(ah, triggers);

	ath9k_ps_restore(sc);
	ath_dbg(common, WOW, "Suspend with WoW triggers: 0x%x\n", triggers);

	set_bit(ATH_OP_WOW_ENABLED, &common->op_flags);
fail_wow:
	mutex_unlock(&sc->mutex);
	return ret;
}

int ath9k_resume(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u8 status;

	mutex_lock(&sc->mutex);

	ath9k_ps_wakeup(sc);

	spin_lock_bh(&sc->sc_pcu_lock);

	ath9k_hw_disable_interrupts(ah);
	ah->imask = sc->wow_intr_before_sleep;
	ath9k_hw_set_interrupts(ah);
	ath9k_hw_enable_interrupts(ah);

	spin_unlock_bh(&sc->sc_pcu_lock);

	status = ath9k_hw_wow_wakeup(ah);
	ath_dbg(common, WOW, "Resume with WoW status: 0x%x\n", status);

	ath_restart_work(sc);
	ath9k_start_btcoex(sc);

	clear_bit(ATH_OP_WOW_ENABLED, &common->op_flags);

	ath9k_ps_restore(sc);
	mutex_unlock(&sc->mutex);

	return 0;
}

void ath9k_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct ath_softc *sc = hw->priv;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	mutex_lock(&sc->mutex);
	device_set_wakeup_enable(sc->dev, enabled);
	mutex_unlock(&sc->mutex);

	ath_dbg(common, WOW, "WoW wakeup source is %s\n",
		(enabled) ? "enabled" : "disabled");
}

void ath9k_init_wow(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;

	if ((sc->driver_data & ATH9K_PCI_WOW) || sc->force_wow) {
		if (AR_SREV_9462_20_OR_LATER(ah) || AR_SREV_9565_11_OR_LATER(ah))
			hw->wiphy->wowlan = &ath9k_wowlan_support;
		else
			hw->wiphy->wowlan = &ath9k_wowlan_support_legacy;

		device_init_wakeup(sc->dev, 1);
	}
}

void ath9k_deinit_wow(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;

	if ((sc->driver_data & ATH9K_PCI_WOW) || sc->force_wow)
		device_init_wakeup(sc->dev, 0);
}
