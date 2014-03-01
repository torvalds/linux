/*
 * Copyright (C) 2011 - 2012  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LOCAL_PN544_H_
#define __LOCAL_PN544_H_

#include <net/nfc/hci.h>

#define DRIVER_DESC "HCI NFC driver for PN544"

#define PN544_HCI_MODE 0
#define PN544_FW_MODE 1

typedef int (*fw_download_t)(void *context, const char *firmware_name);

int pn544_hci_probe(void *phy_id, struct nfc_phy_ops *phy_ops, char *llc_name,
		    int phy_headroom, int phy_tailroom, int phy_payload,
		    fw_download_t fw_download, struct nfc_hci_dev **hdev);
void pn544_hci_remove(struct nfc_hci_dev *hdev);

#endif /* __LOCAL_PN544_H_ */
