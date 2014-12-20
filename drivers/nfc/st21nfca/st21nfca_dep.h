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

#ifndef __ST21NFCA_DEP_H
#define __ST21NFCA_DEP_H

#include <linux/skbuff.h>
#include <linux/workqueue.h>

struct st21nfca_dep_info {
	struct sk_buff *tx_pending;
	struct work_struct tx_work;
	u8 curr_nfc_dep_pni;
	u32 idx;
	u8 to;
	u8 did;
	u8 bsi;
	u8 bri;
	u8 lri;
} __packed;

int st21nfca_dep_event_received(struct nfc_hci_dev *hdev,
				u8 event, struct sk_buff *skb);
int st21nfca_tm_send_dep_res(struct nfc_hci_dev *hdev, struct sk_buff *skb);

int st21nfca_im_send_atr_req(struct nfc_hci_dev *hdev, u8 *gb, size_t gb_len);
int st21nfca_im_send_dep_req(struct nfc_hci_dev *hdev, struct sk_buff *skb);
void st21nfca_dep_init(struct nfc_hci_dev *hdev);
void st21nfca_dep_deinit(struct nfc_hci_dev *hdev);
#endif /* __ST21NFCA_DEP_H */
