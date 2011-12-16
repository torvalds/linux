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

#ifndef MCI_H
#define MCI_H

#define ATH_MCI_SCHED_BUF_SIZE		(16 * 16) /* 16 entries, 4 dword each */
#define ATH_MCI_GPM_MAX_ENTRY		16
#define ATH_MCI_GPM_BUF_SIZE		(ATH_MCI_GPM_MAX_ENTRY * 16)
#define ATH_MCI_DEF_BT_PERIOD		40
#define ATH_MCI_BDR_DUTY_CYCLE		20
#define ATH_MCI_MAX_DUTY_CYCLE		90

#define ATH_MCI_DEF_AGGR_LIMIT		6 /* in 0.24 ms */
#define ATH_MCI_MAX_ACL_PROFILE		7
#define ATH_MCI_MAX_SCO_PROFILE		1
#define ATH_MCI_MAX_PROFILE		(ATH_MCI_MAX_ACL_PROFILE +\
					 ATH_MCI_MAX_SCO_PROFILE)

#define INC_PROF(_mci, _info) do {		 \
		switch (_info->type) {		 \
		case MCI_GPM_COEX_PROFILE_RFCOMM:\
			_mci->num_other_acl++;	 \
			break;			 \
		case MCI_GPM_COEX_PROFILE_A2DP:	 \
			_mci->num_a2dp++;	 \
			if (!_info->edr)	 \
				_mci->num_bdr++; \
			break;			 \
		case MCI_GPM_COEX_PROFILE_HID:	 \
			_mci->num_hid++;	 \
			break;			 \
		case MCI_GPM_COEX_PROFILE_BNEP:	 \
			_mci->num_pan++;	 \
			break;			 \
		case MCI_GPM_COEX_PROFILE_VOICE: \
			_mci->num_sco++;	 \
			break;			 \
		default:			 \
			break;			 \
		}				 \
	} while (0)

#define DEC_PROF(_mci, _info) do {		 \
		switch (_info->type) {		 \
		case MCI_GPM_COEX_PROFILE_RFCOMM:\
			_mci->num_other_acl--;	 \
			break;			 \
		case MCI_GPM_COEX_PROFILE_A2DP:	 \
			_mci->num_a2dp--;	 \
			if (!_info->edr)	 \
				_mci->num_bdr--; \
			break;			 \
		case MCI_GPM_COEX_PROFILE_HID:	 \
			_mci->num_hid--;	 \
			break;			 \
		case MCI_GPM_COEX_PROFILE_BNEP:	 \
			_mci->num_pan--;	 \
			break;			 \
		case MCI_GPM_COEX_PROFILE_VOICE: \
			_mci->num_sco--;	 \
			break;			 \
		default:			 \
			break;			 \
		}				 \
	} while (0)

#define NUM_PROF(_mci)	(_mci->num_other_acl + _mci->num_a2dp + \
			 _mci->num_hid + _mci->num_pan + _mci->num_sco)

struct ath_mci_profile_info {
	u8 type;
	u8 conn_handle;
	bool start;
	bool master;
	bool edr;
	u8 voice_type;
	u16 T;		/* Voice: Tvoice, HID: Tsniff,        in slots */
	u8 W;		/* Voice: Wvoice, HID: Sniff timeout, in slots */
	u8 A;		/*		  HID: Sniff attempt, in slots */
	struct list_head list;
};

struct ath_mci_profile_status {
	bool is_critical;
	bool is_link;
	u8 conn_handle;
};

struct ath_mci_profile {
	struct list_head info;
	DECLARE_BITMAP(status, ATH_MCI_MAX_PROFILE);
	u16 aggr_limit;
	u8 num_mgmt;
	u8 num_sco;
	u8 num_a2dp;
	u8 num_hid;
	u8 num_pan;
	u8 num_other_acl;
	u8 num_bdr;
};


struct ath_mci_buf {
	void *bf_addr;		/* virtual addr of desc */
	dma_addr_t bf_paddr;    /* physical addr of buffer */
	u32 bf_len;		/* len of data */
};

struct ath_mci_coex {
	atomic_t mci_cal_flag;
	struct ath_mci_buf sched_buf;
	struct ath_mci_buf gpm_buf;
	u32 bt_cal_start;
};

void ath_mci_flush_profile(struct ath_mci_profile *mci);
int ath_mci_setup(struct ath_softc *sc);
void ath_mci_cleanup(struct ath_softc *sc);
void ath_mci_intr(struct ath_softc *sc);
#endif
