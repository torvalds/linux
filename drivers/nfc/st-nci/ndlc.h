/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NCI based Driver for STMicroelectronics NFC Chip
 *
 * Copyright (C) 2014-2015  STMicroelectronics SAS. All rights reserved.
 */

#ifndef __LOCAL_NDLC_H_
#define __LOCAL_NDLC_H_

#include <linux/skbuff.h>
#include <net/nfc/nfc.h>

struct st_nci_se_status;

/* Low Level Transport description */
struct llt_ndlc {
	struct nci_dev *ndev;
	const struct nfc_phy_ops *ops;
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
	 * < 0 if hardware error occurred
	 * and prevents normal operation.
	 */
	int hard_fault;
	int powered;
};

int ndlc_open(struct llt_ndlc *ndlc);
void ndlc_close(struct llt_ndlc *ndlc);
int ndlc_send(struct llt_ndlc *ndlc, struct sk_buff *skb);
void ndlc_recv(struct llt_ndlc *ndlc, struct sk_buff *skb);
int ndlc_probe(void *phy_id, const struct nfc_phy_ops *phy_ops,
	       struct device *dev, int phy_headroom, int phy_tailroom,
	       struct llt_ndlc **ndlc_id, struct st_nci_se_status *se_status);
void ndlc_remove(struct llt_ndlc *ndlc);
#endif /* __LOCAL_NDLC_H__ */
