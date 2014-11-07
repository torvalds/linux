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

#ifndef __LOCAL_NDLC_H_
#define __LOCAL_NDLC_H_

#include <linux/skbuff.h>
#include <net/nfc/nfc.h>

/* Low Level Transport description */
struct llt_ndlc {
	struct nci_dev *ndev;
	struct nfc_phy_ops *ops;
	void *phy_id;

	struct timer_list t1_timer;
	bool t1_active;

	struct timer_list t2_timer;
	bool t2_active;

	struct sk_buff_head rcv_q;
	struct sk_buff_head send_q;
	struct sk_buff_head ack_pending_q;

	struct work_struct sm_work;

	struct device *dev;

	/*
	 * < 0 if hardware error occured
	 * and prevents normal operation.
	 */
	int hard_fault;
};

int ndlc_open(struct llt_ndlc *ndlc);
void ndlc_close(struct llt_ndlc *ndlc);
int ndlc_send(struct llt_ndlc *ndlc, struct sk_buff *skb);
void ndlc_recv(struct llt_ndlc *ndlc, struct sk_buff *skb);
int ndlc_probe(void *phy_id, struct nfc_phy_ops *phy_ops, struct device *dev,
	int phy_headroom, int phy_tailroom, struct llt_ndlc **ndlc_id);
void ndlc_remove(struct llt_ndlc *ndlc);
#endif /* __LOCAL_NDLC_H__ */
