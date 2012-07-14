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

#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "ath9k.h"
#include "mci.h"

static const u8 ath_mci_duty_cycle[] = { 55, 50, 60, 70, 80, 85, 90, 95, 98 };

static struct ath_mci_profile_info*
ath_mci_find_profile(struct ath_mci_profile *mci,
		     struct ath_mci_profile_info *info)
{
	struct ath_mci_profile_info *entry;

	if (list_empty(&mci->info))
		return NULL;

	list_for_each_entry(entry, &mci->info, list) {
		if (entry->conn_handle == info->conn_handle)
			return entry;
	}
	return NULL;
}

static bool ath_mci_add_profile(struct ath_common *common,
				struct ath_mci_profile *mci,
				struct ath_mci_profile_info *info)
{
	struct ath_mci_profile_info *entry;

	if ((mci->num_sco == ATH_MCI_MAX_SCO_PROFILE) &&
	    (info->type == MCI_GPM_COEX_PROFILE_VOICE))
		return false;

	if (((NUM_PROF(mci) - mci->num_sco) == ATH_MCI_MAX_ACL_PROFILE) &&
	    (info->type != MCI_GPM_COEX_PROFILE_VOICE))
		return false;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return false;

	memcpy(entry, info, 10);
	INC_PROF(mci, info);
	list_add_tail(&entry->list, &mci->info);

	return true;
}

static void ath_mci_del_profile(struct ath_common *common,
				struct ath_mci_profile *mci,
				struct ath_mci_profile_info *entry)
{
	if (!entry)
		return;

	DEC_PROF(mci, entry);
	list_del(&entry->list);
	kfree(entry);
}

void ath_mci_flush_profile(struct ath_mci_profile *mci)
{
	struct ath_mci_profile_info *info, *tinfo;

	mci->aggr_limit = 0;

	if (list_empty(&mci->info))
		return;

	list_for_each_entry_safe(info, tinfo, &mci->info, list) {
		list_del(&info->list);
		DEC_PROF(mci, info);
		kfree(info);
	}
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
	struct ath9k_hw_mci *mci_hw = &sc->sc_ah->btcoex_hw.mci;
	struct ath_mci_profile_info *info;
	u32 num_profile = NUM_PROF(mci);

	if (mci_hw->config & ATH_MCI_CONFIG_DISABLE_TUNING)
		goto skip_tuning;

	btcoex->duty_cycle = ath_mci_duty_cycle[num_profile];

	if (num_profile == 1) {
		info = list_first_entry(&mci->info,
					struct ath_mci_profile_info,
					list);
		if (mci->num_sco) {
			if (info->T == 12)
				mci->aggr_limit = 8;
			else if (info->T == 6) {
				mci->aggr_limit = 6;
				btcoex->duty_cycle = 30;
			}
			ath_dbg(common, MCI,
				"Single SCO, aggregation limit %d 1/4 ms\n",
				mci->aggr_limit);
		} else if (mci->num_pan || mci->num_other_acl) {
			/*
			 * For single PAN/FTP profile, allocate 35% for BT
			 * to improve WLAN throughput.
			 */
			btcoex->duty_cycle = 35;
			btcoex->btcoex_period = 53;
			ath_dbg(common, MCI,
				"Single PAN/FTP bt period %d ms dutycycle %d\n",
				btcoex->duty_cycle, btcoex->btcoex_period);
		} else if (mci->num_hid) {
			btcoex->duty_cycle = 30;
			mci->aggr_limit = 6;
			ath_dbg(common, MCI,
				"Multiple attempt/timeout single HID "
				"aggregation limit 1.5 ms dutycycle 30%%\n");
		}
	} else if (num_profile == 2) {
		if (mci->num_hid == 2)
			btcoex->duty_cycle = 30;
		mci->aggr_limit = 6;
		ath_dbg(common, MCI,
			"Two BT profiles aggr limit 1.5 ms dutycycle %d%%\n",
			btcoex->duty_cycle);
	} else if (num_profile >= 3) {
		mci->aggr_limit = 4;
		ath_dbg(common, MCI,
			"Three or more profiles aggregation limit 1 ms\n");
	}

skip_tuning:
	if (IS_CHAN_2GHZ(sc->sc_ah->curchan)) {
		if (IS_CHAN_HT(sc->sc_ah->curchan))
			ath_mci_adjust_aggr_limit(btcoex);
		else
			btcoex->btcoex_period >>= 1;
	}

	ath9k_btcoex_timer_pause(sc);
	ath9k_hw_btcoex_disable(sc->sc_ah);

	if (IS_CHAN_5GHZ(sc->sc_ah->curchan))
		return;

	btcoex->duty_cycle += (mci->num_bdr ? ATH_MCI_BDR_DUTY_CYCLE : 0);
	if (btcoex->duty_cycle > ATH_MCI_MAX_DUTY_CYCLE)
		btcoex->duty_cycle = ATH_MCI_MAX_DUTY_CYCLE;

	btcoex->btcoex_no_stomp =  btcoex->btcoex_period * 1000 *
		(100 - btcoex->duty_cycle) / 100;

	ath9k_hw_btcoex_enable(sc->sc_ah);
	ath9k_btcoex_timer_resume(sc);
}

static void ath_mci_cal_msg(struct ath_softc *sc, u8 opcode, u8 *rx_payload)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;
	u32 payload[4] = {0, 0, 0, 0};

	switch (opcode) {
	case MCI_GPM_BT_CAL_REQ:
		if (mci_hw->bt_state == MCI_BT_AWAKE) {
			ar9003_mci_state(ah, MCI_STATE_SET_BT_CAL_START);
			ieee80211_queue_work(sc->hw, &sc->hw_reset_work);
		}
		ath_dbg(common, MCI, "MCI State : %d\n", mci_hw->bt_state);
		break;
	case MCI_GPM_BT_CAL_GRANT:
		MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_DONE);
		ar9003_mci_send_message(sc->sc_ah, MCI_GPM, 0, payload,
					16, false, true);
		break;
	default:
		ath_dbg(common, MCI, "Unknown GPM CAL message\n");
		break;
	}
}

static void ath9k_mci_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc, mci_work);

	ath_mci_update_scheme(sc);
}

static void ath_mci_process_profile(struct ath_softc *sc,
				    struct ath_mci_profile_info *info)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;
	struct ath_mci_profile_info *entry = NULL;

	entry = ath_mci_find_profile(mci, info);
	if (entry) {
		/*
		 * Two MCI interrupts are generated while connecting to
		 * headset and A2DP profile, but only one MCI interrupt
		 * is generated with last added profile type while disconnecting
		 * both profiles.
		 * So while adding second profile type decrement
		 * the first one.
		 */
		if (entry->type != info->type) {
			DEC_PROF(mci, entry);
			INC_PROF(mci, info);
		}
		memcpy(entry, info, 10);
	}

	if (info->start) {
		if (!entry && !ath_mci_add_profile(common, mci, info))
			return;
	} else
		ath_mci_del_profile(common, mci, entry);

	btcoex->btcoex_period = ATH_MCI_DEF_BT_PERIOD;
	mci->aggr_limit = mci->num_sco ? 6 : 0;

	btcoex->duty_cycle = ath_mci_duty_cycle[NUM_PROF(mci)];
	if (NUM_PROF(mci))
		btcoex->bt_stomp_type = ATH_BTCOEX_STOMP_LOW;
	else
		btcoex->bt_stomp_type = mci->num_mgmt ? ATH_BTCOEX_STOMP_ALL :
							ATH_BTCOEX_STOMP_LOW;

	ieee80211_queue_work(sc->hw, &sc->mci_work);
}

static void ath_mci_process_status(struct ath_softc *sc,
				   struct ath_mci_profile_status *status)
{
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;
	struct ath_mci_profile_info info;
	int i = 0, old_num_mgmt = mci->num_mgmt;

	/* Link status type are not handled */
	if (status->is_link)
		return;

	info.conn_handle = status->conn_handle;
	if (ath_mci_find_profile(mci, &info))
		return;

	if (status->conn_handle >= ATH_MCI_MAX_PROFILE)
		return;

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
		ieee80211_queue_work(sc->hw, &sc->mci_work);
}

static void ath_mci_msg(struct ath_softc *sc, u8 opcode, u8 *rx_payload)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_mci_profile_info profile_info;
	struct ath_mci_profile_status profile_status;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	u8 major, minor;
	u32 seq_num;

	switch (opcode) {
	case MCI_GPM_COEX_VERSION_QUERY:
		ar9003_mci_state(ah, MCI_STATE_SEND_WLAN_COEX_VERSION);
		break;
	case MCI_GPM_COEX_VERSION_RESPONSE:
		major = *(rx_payload + MCI_GPM_COEX_B_MAJOR_VERSION);
		minor = *(rx_payload + MCI_GPM_COEX_B_MINOR_VERSION);
		ar9003_mci_set_bt_version(ah, major, minor);
		break;
	case MCI_GPM_COEX_STATUS_QUERY:
		ar9003_mci_send_wlan_channels(ah);
		break;
	case MCI_GPM_COEX_BT_PROFILE_INFO:
		memcpy(&profile_info,
		       (rx_payload + MCI_GPM_COEX_B_PROFILE_TYPE), 10);

		if ((profile_info.type == MCI_GPM_COEX_PROFILE_UNKNOWN) ||
		    (profile_info.type >= MCI_GPM_COEX_PROFILE_MAX)) {
			ath_dbg(common, MCI,
				"Illegal profile type = %d, state = %d\n",
				profile_info.type,
				profile_info.start);
			break;
		}

		ath_mci_process_profile(sc, &profile_info);
		break;
	case MCI_GPM_COEX_BT_STATUS_UPDATE:
		profile_status.is_link = *(rx_payload +
					   MCI_GPM_COEX_B_STATUS_TYPE);
		profile_status.conn_handle = *(rx_payload +
					       MCI_GPM_COEX_B_STATUS_LINKID);
		profile_status.is_critical = *(rx_payload +
					       MCI_GPM_COEX_B_STATUS_STATE);

		seq_num = *((u32 *)(rx_payload + 12));
		ath_dbg(common, MCI,
			"BT_Status_Update: is_link=%d, linkId=%d, state=%d, SEQ=%u\n",
			profile_status.is_link, profile_status.conn_handle,
			profile_status.is_critical, seq_num);

		ath_mci_process_status(sc, &profile_status);
		break;
	default:
		ath_dbg(common, MCI, "Unknown GPM COEX message = 0x%02x\n", opcode);
		break;
	}
}

int ath_mci_setup(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_mci_coex *mci = &sc->mci_coex;
	struct ath_mci_buf *buf = &mci->sched_buf;

	buf->bf_addr = dma_alloc_coherent(sc->dev,
				  ATH_MCI_SCHED_BUF_SIZE + ATH_MCI_GPM_BUF_SIZE,
				  &buf->bf_paddr, GFP_KERNEL);

	if (buf->bf_addr == NULL) {
		ath_dbg(common, FATAL, "MCI buffer alloc failed\n");
		return -ENOMEM;
	}

	memset(buf->bf_addr, MCI_GPM_RSVD_PATTERN,
	       ATH_MCI_SCHED_BUF_SIZE + ATH_MCI_GPM_BUF_SIZE);

	mci->sched_buf.bf_len = ATH_MCI_SCHED_BUF_SIZE;

	mci->gpm_buf.bf_len = ATH_MCI_GPM_BUF_SIZE;
	mci->gpm_buf.bf_addr = (u8 *)mci->sched_buf.bf_addr + mci->sched_buf.bf_len;
	mci->gpm_buf.bf_paddr = mci->sched_buf.bf_paddr + mci->sched_buf.bf_len;

	ar9003_mci_setup(sc->sc_ah, mci->gpm_buf.bf_paddr,
			 mci->gpm_buf.bf_addr, (mci->gpm_buf.bf_len >> 4),
			 mci->sched_buf.bf_paddr);

	INIT_WORK(&sc->mci_work, ath9k_mci_work);
	ath_dbg(common, MCI, "MCI Initialized\n");

	return 0;
}

void ath_mci_cleanup(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	struct ath_mci_coex *mci = &sc->mci_coex;
	struct ath_mci_buf *buf = &mci->sched_buf;

	if (buf->bf_addr)
		dma_free_coherent(sc->dev,
				  ATH_MCI_SCHED_BUF_SIZE + ATH_MCI_GPM_BUF_SIZE,
				  buf->bf_addr, buf->bf_paddr);

	ar9003_mci_cleanup(ah);

	ath_dbg(common, MCI, "MCI De-Initialized\n");
}

void ath_mci_intr(struct ath_softc *sc)
{
	struct ath_mci_coex *mci = &sc->mci_coex;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;
	u32 mci_int, mci_int_rxmsg;
	u32 offset, subtype, opcode;
	u32 *pgpm;
	u32 more_data = MCI_GPM_MORE;
	bool skip_gpm = false;

	ar9003_mci_get_interrupt(sc->sc_ah, &mci_int, &mci_int_rxmsg);

	if (ar9003_mci_state(ah, MCI_STATE_ENABLE) == 0) {
		ar9003_mci_get_next_gpm_offset(ah, true, NULL);
		return;
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE) {
		u32 payload[4] = { 0xffffffff, 0xffffffff,
				   0xffffffff, 0xffffff00};

		/*
		 * The following REMOTE_RESET and SYS_WAKING used to sent
		 * only when BT wake up. Now they are always sent, as a
		 * recovery method to reset BT MCI's RX alignment.
		 */
		ar9003_mci_send_message(ah, MCI_REMOTE_RESET, 0,
					payload, 16, true, false);
		ar9003_mci_send_message(ah, MCI_SYS_WAKING, 0,
					NULL, 0, true, false);

		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE;
		ar9003_mci_state(ah, MCI_STATE_RESET_REQ_WAKE);

		/*
		 * always do this for recovery and 2G/5G toggling and LNA_TRANS
		 */
		ar9003_mci_state(ah, MCI_STATE_SET_BT_AWAKE);
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING) {
		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING;

		if ((mci_hw->bt_state == MCI_BT_SLEEP) &&
		    (ar9003_mci_state(ah, MCI_STATE_REMOTE_SLEEP) !=
		     MCI_BT_SLEEP))
			ar9003_mci_state(ah, MCI_STATE_SET_BT_AWAKE);
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING) {
		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING;

		if ((mci_hw->bt_state == MCI_BT_AWAKE) &&
		    (ar9003_mci_state(ah, MCI_STATE_REMOTE_SLEEP) !=
		     MCI_BT_AWAKE))
			mci_hw->bt_state = MCI_BT_SLEEP;
	}

	if ((mci_int & AR_MCI_INTERRUPT_RX_INVALID_HDR) ||
	    (mci_int & AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT)) {
		ar9003_mci_state(ah, MCI_STATE_RECOVER_RX);
		skip_gpm = true;
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO) {
		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO;
		offset = ar9003_mci_state(ah, MCI_STATE_LAST_SCHD_MSG_OFFSET);
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_GPM) {
		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_GPM;

		while (more_data == MCI_GPM_MORE) {

			pgpm = mci->gpm_buf.bf_addr;
			offset = ar9003_mci_get_next_gpm_offset(ah, false,
								&more_data);

			if (offset == MCI_GPM_INVALID)
				break;

			pgpm += (offset >> 2);

			/*
			 * The first dword is timer.
			 * The real data starts from 2nd dword.
			 */
			subtype = MCI_GPM_TYPE(pgpm);
			opcode = MCI_GPM_OPCODE(pgpm);

			if (skip_gpm)
				goto recycle;

			if (MCI_GPM_IS_CAL_TYPE(subtype)) {
				ath_mci_cal_msg(sc, subtype, (u8 *)pgpm);
			} else {
				switch (subtype) {
				case MCI_GPM_COEX_AGENT:
					ath_mci_msg(sc, opcode, (u8 *)pgpm);
					break;
				default:
					break;
				}
			}
		recycle:
			MCI_GPM_RECYCLE(pgpm);
		}
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_HW_MSG_MASK) {
		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL)
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL;

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_LNA_INFO)
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_LNA_INFO;

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_CONT_INFO) {
			int value_dbm = MS(mci_hw->cont_status,
					   AR_MCI_CONT_RSSI_POWER);

			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_CONT_INFO;

			ath_dbg(common, MCI,
				"MCI CONT_INFO: (%s) pri = %d pwr = %d dBm\n",
				MS(mci_hw->cont_status, AR_MCI_CONT_TXRX) ?
				"tx" : "rx",
				MS(mci_hw->cont_status, AR_MCI_CONT_PRIORITY),
				value_dbm);
		}

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_CONT_NACK)
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_CONT_NACK;

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_CONT_RST)
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_CONT_RST;
	}

	if ((mci_int & AR_MCI_INTERRUPT_RX_INVALID_HDR) ||
	    (mci_int & AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT))
		mci_int &= ~(AR_MCI_INTERRUPT_RX_INVALID_HDR |
			     AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT);
}

void ath_mci_enable(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	if (!common->btcoex_enabled)
		return;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_MCI)
		sc->sc_ah->imask |= ATH9K_INT_MCI;
}
