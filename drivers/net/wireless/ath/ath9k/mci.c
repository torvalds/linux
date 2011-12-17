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
		ath_dbg(common, MCI,
			"Too many SCO profile, failed to add new profile\n");
		return false;
	}

	if (((NUM_PROF(mci) - mci->num_sco) == ATH_MCI_MAX_ACL_PROFILE) &&
	    (info->type != MCI_GPM_COEX_PROFILE_VOICE)) {
		ath_dbg(common, MCI,
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
		ath_dbg(common, MCI, "Profile to be deleted not found\n");
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
			ath_dbg(common, MCI,
				"Single SCO, aggregation limit 2 ms\n");
		} else if ((info->type == MCI_GPM_COEX_PROFILE_BNEP) &&
			   !info->master) {
			btcoex->btcoex_period = 60;
			ath_dbg(common, MCI,
				"Single slave PAN/FTP, bt period 60 ms\n");
		} else if ((info->type == MCI_GPM_COEX_PROFILE_HID) &&
			 (info->T > 0 && info->T < 50) &&
			 (info->A > 1 || info->W > 1)) {
			btcoex->duty_cycle = 30;
			mci->aggr_limit = 8;
			ath_dbg(common, MCI,
				"Multiple attempt/timeout single HID "
				"aggregation limit 2 ms dutycycle 30%%\n");
		}
	} else if ((num_profile == 2) && (mci->num_hid == 2)) {
		btcoex->duty_cycle = 30;
		mci->aggr_limit = 8;
		ath_dbg(common, MCI,
			"Two HIDs aggregation limit 2 ms dutycycle 30%%\n");
	} else if (num_profile > 3) {
		mci->aggr_limit = 6;
		ath_dbg(common, MCI,
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


static void ath_mci_cal_msg(struct ath_softc *sc, u8 opcode, u8 *rx_payload)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 payload[4] = {0, 0, 0, 0};

	switch (opcode) {
	case MCI_GPM_BT_CAL_REQ:

		ath_dbg(common, MCI, "MCI received BT_CAL_REQ\n");

		if (ar9003_mci_state(ah, MCI_STATE_BT, NULL) == MCI_BT_AWAKE) {
			ar9003_mci_state(ah, MCI_STATE_SET_BT_CAL_START, NULL);
			ieee80211_queue_work(sc->hw, &sc->hw_reset_work);
		} else
			ath_dbg(common, MCI, "MCI State mismatches: %d\n",
				ar9003_mci_state(ah, MCI_STATE_BT, NULL));

		break;

	case MCI_GPM_BT_CAL_DONE:

		ath_dbg(common, MCI, "MCI received BT_CAL_DONE\n");

		if (ar9003_mci_state(ah, MCI_STATE_BT, NULL) == MCI_BT_CAL)
			ath_dbg(common, MCI, "MCI error illegal!\n");
		else
			ath_dbg(common, MCI, "MCI BT not in CAL state\n");

		break;

	case MCI_GPM_BT_CAL_GRANT:

		ath_dbg(common, MCI, "MCI received BT_CAL_GRANT\n");

		/* Send WLAN_CAL_DONE for now */
		ath_dbg(common, MCI, "MCI send WLAN_CAL_DONE\n");
		MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_DONE);
		ar9003_mci_send_message(sc->sc_ah, MCI_GPM, 0, payload,
					16, false, true);
		break;

	default:
		ath_dbg(common, MCI, "MCI Unknown GPM CAL message\n");
		break;
	}
}

static void ath_mci_process_profile(struct ath_softc *sc,
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

static void ath_mci_process_status(struct ath_softc *sc,
				   struct ath_mci_profile_status *status)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;
	struct ath_mci_profile_info info;
	int i = 0, old_num_mgmt = mci->num_mgmt;

	/* Link status type are not handled */
	if (status->is_link) {
		ath_dbg(common, MCI, "Skip link type status update\n");
		return;
	}

	memset(&info, 0, sizeof(struct ath_mci_profile_info));

	info.conn_handle = status->conn_handle;
	if (ath_mci_find_profile(mci, &info)) {
		ath_dbg(common, MCI,
			"Skip non link state update for existing profile %d\n",
			status->conn_handle);
		return;
	}
	if (status->conn_handle >= ATH_MCI_MAX_PROFILE) {
		ath_dbg(common, MCI, "Ignore too many non-link update\n");
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

static void ath_mci_msg(struct ath_softc *sc, u8 opcode, u8 *rx_payload)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_mci_profile_info profile_info;
	struct ath_mci_profile_status profile_status;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	u32 version;
	u8 major;
	u8 minor;
	u32 seq_num;

	switch (opcode) {

	case MCI_GPM_COEX_VERSION_QUERY:
		ath_dbg(common, MCI, "MCI Recv GPM COEX Version Query\n");
		version = ar9003_mci_state(ah,
				MCI_STATE_SEND_WLAN_COEX_VERSION, NULL);
		break;

	case MCI_GPM_COEX_VERSION_RESPONSE:
		ath_dbg(common, MCI, "MCI Recv GPM COEX Version Response\n");
		major = *(rx_payload + MCI_GPM_COEX_B_MAJOR_VERSION);
		minor = *(rx_payload + MCI_GPM_COEX_B_MINOR_VERSION);
		ath_dbg(common, MCI, "MCI BT Coex version: %d.%d\n",
			major, minor);
		version = (major << 8) + minor;
		version = ar9003_mci_state(ah,
			  MCI_STATE_SET_BT_COEX_VERSION, &version);
		break;

	case MCI_GPM_COEX_STATUS_QUERY:
		ath_dbg(common, MCI,
			"MCI Recv GPM COEX Status Query = 0x%02x\n",
			*(rx_payload + MCI_GPM_COEX_B_WLAN_BITMAP));
		ar9003_mci_state(ah,
		MCI_STATE_SEND_WLAN_CHANNELS, NULL);
		break;

	case MCI_GPM_COEX_BT_PROFILE_INFO:
		ath_dbg(common, MCI, "MCI Recv GPM Coex BT profile info\n");
		memcpy(&profile_info,
		       (rx_payload + MCI_GPM_COEX_B_PROFILE_TYPE), 10);

		if ((profile_info.type == MCI_GPM_COEX_PROFILE_UNKNOWN)
		    || (profile_info.type >=
					    MCI_GPM_COEX_PROFILE_MAX)) {

			ath_dbg(common, MCI,
				"illegal profile type = %d, state = %d\n",
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
			"MCI Recv GPM COEX BT_Status_Update: is_link=%d, linkId=%d, state=%d, SEQ=%d\n",
			profile_status.is_link, profile_status.conn_handle,
			profile_status.is_critical, seq_num);

		ath_mci_process_status(sc, &profile_status);
		break;

	default:
		ath_dbg(common, MCI, "MCI Unknown GPM COEX message = 0x%02x\n",
			opcode);
		break;
	}
}

static int ath_mci_buf_alloc(struct ath_softc *sc, struct ath_mci_buf *buf)
{
	int error = 0;

	buf->bf_addr = dma_alloc_coherent(sc->dev, buf->bf_len,
					  &buf->bf_paddr, GFP_KERNEL);

	if (buf->bf_addr == NULL) {
		error = -ENOMEM;
		goto fail;
	}

	return 0;

fail:
	memset(buf, 0, sizeof(*buf));
	return error;
}

static void ath_mci_buf_free(struct ath_softc *sc, struct ath_mci_buf *buf)
{
	if (buf->bf_addr) {
		dma_free_coherent(sc->dev, buf->bf_len, buf->bf_addr,
							buf->bf_paddr);
		memset(buf, 0, sizeof(*buf));
	}
}

int ath_mci_setup(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_mci_coex *mci = &sc->mci_coex;
	int error = 0;

	if (!ATH9K_HW_CAP_MCI)
		return 0;

	mci->sched_buf.bf_len = ATH_MCI_SCHED_BUF_SIZE + ATH_MCI_GPM_BUF_SIZE;

	if (ath_mci_buf_alloc(sc, &mci->sched_buf)) {
		ath_dbg(common, FATAL, "MCI buffer alloc failed\n");
		error = -ENOMEM;
		goto fail;
	}

	mci->sched_buf.bf_len = ATH_MCI_SCHED_BUF_SIZE;

	memset(mci->sched_buf.bf_addr, MCI_GPM_RSVD_PATTERN,
						mci->sched_buf.bf_len);

	mci->gpm_buf.bf_len = ATH_MCI_GPM_BUF_SIZE;
	mci->gpm_buf.bf_addr = (u8 *)mci->sched_buf.bf_addr +
							mci->sched_buf.bf_len;
	mci->gpm_buf.bf_paddr = mci->sched_buf.bf_paddr + mci->sched_buf.bf_len;

	/* initialize the buffer */
	memset(mci->gpm_buf.bf_addr, MCI_GPM_RSVD_PATTERN, mci->gpm_buf.bf_len);

	ar9003_mci_setup(sc->sc_ah, mci->gpm_buf.bf_paddr,
			 mci->gpm_buf.bf_addr, (mci->gpm_buf.bf_len >> 4),
			 mci->sched_buf.bf_paddr);
fail:
	return error;
}

void ath_mci_cleanup(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_mci_coex *mci = &sc->mci_coex;

	if (!ATH9K_HW_CAP_MCI)
		return;

	/*
	 * both schedule and gpm buffers will be released
	 */
	ath_mci_buf_free(sc, &mci->sched_buf);
	ar9003_mci_cleanup(ah);
}

void ath_mci_intr(struct ath_softc *sc)
{
	struct ath_mci_coex *mci = &sc->mci_coex;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 mci_int, mci_int_rxmsg;
	u32 offset, subtype, opcode;
	u32 *pgpm;
	u32 more_data = MCI_GPM_MORE;
	bool skip_gpm = false;

	if (!ATH9K_HW_CAP_MCI)
		return;

	ar9003_mci_get_interrupt(sc->sc_ah, &mci_int, &mci_int_rxmsg);

	if (ar9003_mci_state(ah, MCI_STATE_ENABLE, NULL) == 0) {

		ar9003_mci_state(sc->sc_ah, MCI_STATE_INIT_GPM_OFFSET, NULL);
		ath_dbg(common, MCI, "MCI interrupt but MCI disabled\n");

		ath_dbg(common, MCI,
			"MCI interrupt: intr = 0x%x, intr_rxmsg = 0x%x\n",
			mci_int, mci_int_rxmsg);
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
		ath_dbg(common, MCI, "MCI interrupt send REMOTE_RESET\n");

		ar9003_mci_send_message(ah, MCI_REMOTE_RESET, 0,
					payload, 16, true, false);
		ath_dbg(common, MCI, "MCI interrupt send SYS_WAKING\n");
		ar9003_mci_send_message(ah, MCI_SYS_WAKING, 0,
					NULL, 0, true, false);

		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE;
		ar9003_mci_state(ah, MCI_STATE_RESET_REQ_WAKE, NULL);

		/*
		 * always do this for recovery and 2G/5G toggling and LNA_TRANS
		 */
		ath_dbg(common, MCI, "MCI Set BT state to AWAKE\n");
		ar9003_mci_state(ah, MCI_STATE_SET_BT_AWAKE, NULL);
	}

	/* Processing SYS_WAKING/SYS_SLEEPING */
	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING) {
		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING;

		if (ar9003_mci_state(ah, MCI_STATE_BT, NULL) == MCI_BT_SLEEP) {

			if (ar9003_mci_state(ah, MCI_STATE_REMOTE_SLEEP, NULL)
					== MCI_BT_SLEEP)
				ath_dbg(common, MCI,
					"MCI BT stays in sleep mode\n");
			else {
				ath_dbg(common, MCI,
					"MCI Set BT state to AWAKE\n");
				ar9003_mci_state(ah,
						 MCI_STATE_SET_BT_AWAKE, NULL);
			}
		} else
			ath_dbg(common, MCI, "MCI BT stays in AWAKE mode\n");
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING) {

		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING;

		if (ar9003_mci_state(ah, MCI_STATE_BT, NULL) == MCI_BT_AWAKE) {

			if (ar9003_mci_state(ah, MCI_STATE_REMOTE_SLEEP, NULL)
					== MCI_BT_AWAKE)
				ath_dbg(common, MCI,
					"MCI BT stays in AWAKE mode\n");
			else {
				ath_dbg(common, MCI,
					"MCI SetBT state to SLEEP\n");
				ar9003_mci_state(ah, MCI_STATE_SET_BT_SLEEP,
						 NULL);
			}
		} else
			ath_dbg(common, MCI, "MCI BT stays in SLEEP mode\n");
	}

	if ((mci_int & AR_MCI_INTERRUPT_RX_INVALID_HDR) ||
	    (mci_int & AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT)) {

		ath_dbg(common, MCI, "MCI RX broken, skip GPM msgs\n");
		ar9003_mci_state(ah, MCI_STATE_RECOVER_RX, NULL);
		skip_gpm = true;
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO) {

		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO;
		offset = ar9003_mci_state(ah, MCI_STATE_LAST_SCHD_MSG_OFFSET,
					  NULL);
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_GPM) {

		mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_GPM;

		while (more_data == MCI_GPM_MORE) {

			pgpm = mci->gpm_buf.bf_addr;
			offset = ar9003_mci_state(ah,
					MCI_STATE_NEXT_GPM_OFFSET, &more_data);

			if (offset == MCI_GPM_INVALID)
				break;

			pgpm += (offset >> 2);

			/*
			 * The first dword is timer.
			 * The real data starts from 2nd dword.
			 */

			subtype = MCI_GPM_TYPE(pgpm);
			opcode = MCI_GPM_OPCODE(pgpm);

			if (!skip_gpm) {

				if (MCI_GPM_IS_CAL_TYPE(subtype))
					ath_mci_cal_msg(sc, subtype,
							(u8 *) pgpm);
				else {
					switch (subtype) {
					case MCI_GPM_COEX_AGENT:
						ath_mci_msg(sc, opcode,
							    (u8 *) pgpm);
						break;
					default:
						break;
					}
				}
			}
			MCI_GPM_RECYCLE(pgpm);
		}
	}

	if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_HW_MSG_MASK) {

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL)
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL;

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_LNA_INFO) {
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_LNA_INFO;
			ath_dbg(common, MCI, "MCI LNA_INFO\n");
		}

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_CONT_INFO) {

			int value_dbm = ar9003_mci_state(ah,
					MCI_STATE_CONT_RSSI_POWER, NULL);

			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_CONT_INFO;

			if (ar9003_mci_state(ah, MCI_STATE_CONT_TXRX, NULL))
				ath_dbg(common, MCI,
					"MCI CONT_INFO: (tx) pri = %d, pwr = %d dBm\n",
					ar9003_mci_state(ah,
						MCI_STATE_CONT_PRIORITY, NULL),
					value_dbm);
			else
				ath_dbg(common, MCI,
					"MCI CONT_INFO: (rx) pri = %d,pwr = %d dBm\n",
					ar9003_mci_state(ah,
						MCI_STATE_CONT_PRIORITY, NULL),
					value_dbm);
		}

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_CONT_NACK) {
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_CONT_NACK;
			ath_dbg(common, MCI, "MCI CONT_NACK\n");
		}

		if (mci_int_rxmsg & AR_MCI_INTERRUPT_RX_MSG_CONT_RST) {
			mci_int_rxmsg &= ~AR_MCI_INTERRUPT_RX_MSG_CONT_RST;
			ath_dbg(common, MCI, "MCI CONT_RST\n");
		}
	}

	if ((mci_int & AR_MCI_INTERRUPT_RX_INVALID_HDR) ||
	    (mci_int & AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT))
		mci_int &= ~(AR_MCI_INTERRUPT_RX_INVALID_HDR |
			     AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT);

	if (mci_int_rxmsg & 0xfffffffe)
		ath_dbg(common, MCI, "MCI not processed mci_int_rxmsg = 0x%x\n",
			mci_int_rxmsg);
}
