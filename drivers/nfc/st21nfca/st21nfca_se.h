/*
 * Copyright (C) 2014  STMicroelectronics SAS. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ST21NFCA_SE_H
#define __ST21NFCA_SE_H

#include <linux/skbuff.h>
#include <linux/workqueue.h>

/*
 * ref ISO7816-3 chap 8.1. the initial character TS is followed by a
 * sequence of at most 32 characters.
 */
#define ST21NFCA_ESE_MAX_LENGTH			33
#define ST21NFCA_ESE_HOST_ID			0xc0

struct st21nfca_se_info {
	u8 atr[ST21NFCA_ESE_MAX_LENGTH];
	struct completion req_completion;

	struct timer_list bwi_timer;
	int wt_timeout; /* in msecs */
	bool bwi_active;

	struct timer_list se_active_timer;
	bool se_active;
	int expected_pipes;
	int count_pipes;

	bool xch_error;

	se_io_cb_t cb;
	void *cb_context;
};

int st21nfca_connectivity_event_received(struct nfc_hci_dev *hdev, u8 host,
					u8 event, struct sk_buff *skb);
int st21nfca_apdu_reader_event_received(struct nfc_hci_dev *hdev,
					u8 event, struct sk_buff *skb);

int st21nfca_hci_discover_se(struct nfc_hci_dev *hdev);
int st21nfca_hci_enable_se(struct nfc_hci_dev *hdev, u32 se_idx);
int st21nfca_hci_disable_se(struct nfc_hci_dev *hdev, u32 se_idx);
int st21nfca_hci_se_io(struct nfc_hci_dev *hdev, u32 se_idx,
		u8 *apdu, size_t apdu_length,
		se_io_cb_t cb, void *cb_context);

void st21nfca_se_init(struct nfc_hci_dev *hdev);
void st21nfca_se_deinit(struct nfc_hci_dev *hdev);
#endif /* __ST21NFCA_SE_H */
