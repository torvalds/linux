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

#include "st-nci_se.h"
#include "ndlc.h"

/* Define private flags: */
#define ST_NCI_RUNNING			1

#define ST_NCI_CORE_PROP                0x01
#define ST_NCI_SET_NFC_MODE             0x02

struct nci_mode_set_cmd {
	u8 cmd_type;
	u8 mode;
} __packed;

struct nci_mode_set_rsp {
	u8 status;
} __packed;

struct st_nci_info {
	struct llt_ndlc *ndlc;
	unsigned long flags;
	struct st_nci_se_info se_info;
};

void st_nci_remove(struct nci_dev *ndev);
int st_nci_probe(struct llt_ndlc *ndlc, int phy_headroom,
		int phy_tailroom);

#endif /* __LOCAL_ST_NCI_H_ */
