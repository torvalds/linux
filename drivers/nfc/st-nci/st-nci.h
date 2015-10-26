/*
 * NCI based Driver for STMicroelectronics NFC Chip
 *
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

#ifndef __LOCAL_ST_NCI_H_
#define __LOCAL_ST_NCI_H_

#include "ndlc.h"

/* Define private flags: */
#define ST_NCI_RUNNING			1

#define ST_NCI_CORE_PROP                0x01
#define ST_NCI_SET_NFC_MODE             0x02

/*
 * ref ISO7816-3 chap 8.1. the initial character TS is followed by a
 * sequence of at most 32 characters.
 */
#define ST_NCI_ESE_MAX_LENGTH  33
#define ST_NCI_HCI_HOST_ID_ESE 0xc0

struct nci_mode_set_cmd {
	u8 cmd_type;
	u8 mode;
} __packed;

struct nci_mode_set_rsp {
	u8 status;
} __packed;

struct st_nci_se_info {
	u8 atr[ST_NCI_ESE_MAX_LENGTH];
	struct completion req_completion;

	struct timer_list bwi_timer;
	int wt_timeout; /* in msecs */
	bool bwi_active;

	struct timer_list se_active_timer;
	bool se_active;

	bool xch_error;

	se_io_cb_t cb;
	void *cb_context;
};

struct st_nci_info {
	struct llt_ndlc *ndlc;
	unsigned long flags;
	struct st_nci_se_info se_info;
};

void st_nci_remove(struct nci_dev *ndev);
int st_nci_probe(struct llt_ndlc *ndlc, int phy_headroom,
		int phy_tailroom);

int st_nci_se_init(struct nci_dev *ndev);
void st_nci_se_deinit(struct nci_dev *ndev);

int st_nci_discover_se(struct nci_dev *ndev);
int st_nci_enable_se(struct nci_dev *ndev, u32 se_idx);
int st_nci_disable_se(struct nci_dev *ndev, u32 se_idx);
int st_nci_se_io(struct nci_dev *ndev, u32 se_idx,
				u8 *apdu, size_t apdu_length,
				se_io_cb_t cb, void *cb_context);
int st_nci_hci_load_session(struct nci_dev *ndev);
void st_nci_hci_event_received(struct nci_dev *ndev, u8 pipe,
					u8 event, struct sk_buff *skb);
void st_nci_hci_cmd_received(struct nci_dev *ndev, u8 pipe, u8 cmd,
						struct sk_buff *skb);

#endif /* __LOCAL_ST_NCI_H_ */
