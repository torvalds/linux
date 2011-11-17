/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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
#include "mci.h"

u8 ath_mci_duty_cycle[] = { 0, 50, 60, 70, 80, 85, 90, 95, 98 };

static struct ath_mci_profile_info*
ath_mci_find_profile(struct ath_mci_profile *mci,
		     struct ath_mci_profile_info *info)
{
	struct ath_mci_profile_info *entry;

	list_for_each_entry(entry, &mci->info, list) {
		if (entry->conn_handle == info->conn_handle)
			break;
	}
	return entry;
}

static bool ath_mci_add_profile(struct ath_common *common,
				struct ath_mci_profile *mci,
				struct ath_mci_profile_info *info)
{
	struct ath_mci_profile_info *entry;

	if ((mci->num_sco == ATH_MCI_MAX_SCO_PROFILE) &&
	    (info->type == MCI_GPM_COEX_PROFILE_VOICE)) {
		ath_dbg(common, ATH_DBG_MCI,
			"Too many SCO profile, failed to add new profile\n");
		return false;
	}

	if (((NUM_PROF(mci) - mci->num_sco) == ATH_MCI_MAX_ACL_PROFILE) &&
	    (info->type != MCI_GPM_COEX_PROFILE_VOICE)) {
		ath_dbg(common, ATH_DBG_MCI,
			"Too many ACL profile, failed to add new profile\n");
		return false;
	}

	entry = ath_mci_find_profile(mci, info);

	if (entry)
		memcpy(entry, info, 10);
	else {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			return false;

		memcpy(entry, info, 10);
		INC_PROF(mci, info);
		list_add_tail(&info->list, &mci->info);
	}
	return true;
}

static void ath_mci_del_profile(struct ath_common *common,
				struct ath_mci_profile *mci,
				struct ath_mci_profile_info *info)
{
	struct ath_mci_profile_info *entry;

	entry = ath_mci_find_profile(mci, info);

	if (!entry) {
		ath_dbg(common, ATH_DBG_MCI,
			"Profile to be deleted not found\n");
		return;
	}
	DEC_PROF(mci, entry);
	list_del(&entry->list);
	kfree(entry);
}

void ath_mci_flush_profile(struct ath_mci_profile *mci)
{
	struct ath_mci_profile_info *info, *tinfo;

	list_for_each_entry_safe(info, tinfo, &mci->info, list) {
		list_del(&info->list);
		DEC_PROF(mci, info);
		kfree(info);
	}
	mci->aggr_limit = 0;
}

static void ath_mci_adjust_aggr_limit(struct ath_btcoex *btcoex)
{
	struct ath_mci_profile *mci = &btcoex->mci;
	u32 wlan_airtime = btcoex->btcoex_period *
				(100 - btcoex->duty_cycle) / 100;

	/*
	 * Scale: wlan_airtime is in ms, aggr_limit is in 0.25 ms.
	 * When wlan_airtime is less than 4ms, aggregation limit has to be
	 * adjusted half of wlan_airtime to ensure that the aggregation can fit
	 * without collision with BT traffic.
	 */
	if ((wlan_airtime <= 4) &&
	    (!mci->aggr_limit || (mci->aggr_limit > (2 * wlan_airtime))))
		mci->aggr_limit = 2 * wlan_airtime;
}

static void ath_mci_update_scheme(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;
	struct ath_mci_profile_info *info;
	u32 num_profile = NUM_PROF(mci);

	if (num_profile == 1) {
		info = list_first_entry(&mci->info,
					struct ath_mci_profile_info,
					list);
		if (mci->num_sco && info->T == 12) {
			mci->aggr_limit = 8;
			ath_dbg(common, ATH_DBG_MCI,
				"Single SCO, aggregation limit 2 ms\n");
		} else if ((info->type == MCI_GPM_COEX_PROFILE_BNEP) &&
			   !info->master) {
			btcoex->btcoex_period = 60;
			ath_dbg(common, ATH_DBG_MCI,
				"Single slave PAN/FTP, bt period 60 ms\n");
		} else if ((info->type == MCI_GPM_COEX_PROFILE_HID) &&
			 (info->T > 0 && info->T < 50) &&
			 (info->A > 1 || info->W > 1)) {
			btcoex->duty_cycle = 30;
			mci->aggr_limit = 8;
			ath_dbg(common, ATH_DBG_MCI,
				"Multiple attempt/timeout single HID "
				"aggregation limit 2 ms dutycycle 30%%\n");
		}
	} else if ((num_profile == 2) && (mci->num_hid == 2)) {
		btcoex->duty_cycle = 30;
		mci->aggr_limit = 8;
		ath_dbg(common, ATH_DBG_MCI,
			"Two HIDs aggregation limit 2 ms dutycycle 30%%\n");
	} else if (num_profile > 3) {
		mci->aggr_limit = 6;
		ath_dbg(common, ATH_DBG_MCI,
			"Three or more profiles aggregation limit 1.5 ms\n");
	}

	if (IS_CHAN_2GHZ(sc->sc_ah->curchan)) {
		if (IS_CHAN_HT(sc->sc_ah->curchan))
			ath_mci_adjust_aggr_limit(btcoex);
		else
			btcoex->btcoex_period >>= 1;
	}

	ath9k_hw_btcoex_disable(sc->sc_ah);
	ath9k_btcoex_timer_pause(sc);

	if (IS_CHAN_5GHZ(sc->sc_ah->curchan))
		return;

	btcoex->duty_cycle += (mci->num_bdr ? ATH_MCI_MAX_DUTY_CYCLE : 0);
	if (btcoex->duty_cycle > ATH_MCI_MAX_DUTY_CYCLE)
		btcoex->duty_cycle = ATH_MCI_MAX_DUTY_CYCLE;

	btcoex->btcoex_period *= 1000;
	btcoex->btcoex_no_stomp =  btcoex->btcoex_period *
					(100 - btcoex->duty_cycle) / 100;

	ath9k_hw_btcoex_enable(sc->sc_ah);
	ath9k_btcoex_timer_resume(sc);
}

void ath_mci_process_profile(struct ath_softc *sc,
			     struct ath_mci_profile_info *info)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;

	if (info->start) {
		if (!ath_mci_add_profile(common, mci, info))
			return;
	} else
		ath_mci_del_profile(common, mci, info);

	btcoex->btcoex_period = ATH_MCI_DEF_BT_PERIOD;
	mci->aggr_limit = mci->num_sco ? 6 : 0;
	if (NUM_PROF(mci)) {
		btcoex->bt_stomp_type = ATH_BTCOEX_STOMP_LOW;
		btcoex->duty_cycle = ath_mci_duty_cycle[NUM_PROF(mci)];
	} else {
		btcoex->bt_stomp_type = mci->num_mgmt ? ATH_BTCOEX_STOMP_ALL :
							ATH_BTCOEX_STOMP_LOW;
		btcoex->duty_cycle = ATH_BTCOEX_DEF_DUTY_CYCLE;
	}

	ath_mci_update_scheme(sc);
}

void ath_mci_process_status(struct ath_softc *sc,
			    struct ath_mci_profile_status *status)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;
	struct ath_mci_profile_info info;
	int i = 0, old_num_mgmt = mci->num_mgmt;

	/* Link status type are not handled */
	if (status->is_link) {
		ath_dbg(common, ATH_DBG_MCI,
			"Skip link type status update\n");
		return;
	}

	memset(&info, 0, sizeof(struct ath_mci_profile_info));

	info.conn_handle = status->conn_handle;
	if (ath_mci_find_profile(mci, &info)) {
		ath_dbg(common, ATH_DBG_MCI,
			"Skip non link state update for existing profile %d\n",
			status->conn_handle);
		return;
	}
	if (status->conn_handle >= ATH_MCI_MAX_PROFILE) {
		ath_dbg(common, ATH_DBG_MCI,
			"Ignore too many non-link update\n");
		return;
	}
	if (status->is_critical)
		__set_bit(status->conn_handle, mci->status);
	else
		__clear_bit(status->conn_handle, mci->status);

	mci->num_mgmt = 0;
	do {
		if (test_bit(i, mci->status))
			mci->num_mgmt++;
	} while (++i < ATH_MCI_MAX_PROFILE);

	if (old_num_mgmt != mci->num_mgmt)
		ath_mci_update_scheme(sc);
}
