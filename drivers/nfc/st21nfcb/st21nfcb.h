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

#ifndef __LOCAL_ST21NFCB_H_
#define __LOCAL_ST21NFCB_H_

#include <net/nfc/nci_core.h>

#include "ndlc.h"

/* Define private flags: */
#define ST21NFCB_NCI_RUNNING			1

struct st21nfcb_nci_info {
	struct llt_ndlc *ndlc;
	unsigned long flags;
};

void st21nfcb_nci_remove(struct nci_dev *ndev);
int st21nfcb_nci_probe(struct llt_ndlc *ndlc, int phy_headroom,
		int phy_tailroom);

#endif /* __LOCAL_ST21NFCB_H_ */
