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

#ifndef __LOCAL_ST21NFCA_H_
#define __LOCAL_ST21NFCA_H_

#include <net/nfc/hci.h>

#include "st21nfca_dep.h"

#define HCI_MODE 0

/* framing in HCI mode */
#define ST21NFCA_SOF_EOF_LEN    2

/* Almost every time value is 0 */
#define ST21NFCA_HCI_LLC_LEN    1

/* Size in worst case :
 * In normal case CRC len = 2 but byte stuffing
 * may appear in case one CRC byte = ST21NFCA_SOF_EOF
 */
#define ST21NFCA_HCI_LLC_CRC    4

#define ST21NFCA_HCI_LLC_LEN_CRC        (ST21NFCA_SOF_EOF_LEN + \
						ST21NFCA_HCI_LLC_LEN + \
						ST21NFCA_HCI_LLC_CRC)
#define ST21NFCA_HCI_LLC_MIN_SIZE       (1 + ST21NFCA_HCI_LLC_LEN_CRC)

/* Worst case when adding byte stuffing between each byte */
#define ST21NFCA_HCI_LLC_MAX_PAYLOAD    29
#define ST21NFCA_HCI_LLC_MAX_SIZE       (ST21NFCA_HCI_LLC_LEN_CRC + 1 + \
					ST21NFCA_HCI_LLC_MAX_PAYLOAD)

#define DRIVER_DESC "HCI NFC driver for ST21NFCA"

#define ST21NFCA_HCI_MODE 0

#define ST21NFCA_NUM_DEVICES 256

int st21nfca_hci_probe(void *phy_id, struct nfc_phy_ops *phy_ops,
		       char *llc_name, int phy_headroom, int phy_tailroom,
		       int phy_payload, struct nfc_hci_dev **hdev);
void st21nfca_hci_remove(struct nfc_hci_dev *hdev);

enum st21nfca_state {
	ST21NFCA_ST_COLD,
	ST21NFCA_ST_READY,
};

struct st21nfca_hci_info {
	struct nfc_phy_ops *phy_ops;
	void *phy_id;

	struct nfc_hci_dev *hdev;

	enum st21nfca_state state;

	struct mutex info_lock;

	int async_cb_type;
	data_exchange_cb_t async_cb;
	void *async_cb_context;

	struct st21nfca_dep_info dep_info;
};

/* Reader RF commands */
#define ST21NFCA_WR_XCHG_DATA            0x10

#define ST21NFCA_RF_READER_F_GATE               0x14
#define ST21NFCA_RF_READER_F_DATARATE 0x01
#define ST21NFCA_RF_READER_F_DATARATE_106 0x01
#define ST21NFCA_RF_READER_F_DATARATE_212 0x02
#define ST21NFCA_RF_READER_F_DATARATE_424 0x04
#define ST21NFCA_RF_READER_F_POL_REQ    0x02
#define ST21NFCA_RF_READER_F_POL_REQ_DEFAULT    0xffff0000
#define ST21NFCA_RF_READER_F_NFCID2 0x03
#define ST21NFCA_RF_READER_F_NFCID1 0x04
#define ST21NFCA_RF_READER_F_SENS_RES 0x05

#define ST21NFCA_RF_CARD_F_GATE 0x24
#define ST21NFCA_RF_CARD_F_MODE 0x01
#define ST21NFCA_RF_CARD_F_NFCID2_LIST 0x04
#define ST21NFCA_RF_CARD_F_NFCID1 0x05
#define ST21NFCA_RF_CARD_F_SENS_RES 0x06
#define ST21NFCA_RF_CARD_F_SEL_RES 0x07
#define ST21NFCA_RF_CARD_F_DATARATE 0x08
#define ST21NFCA_RF_CARD_F_DATARATE_106 0x00
#define ST21NFCA_RF_CARD_F_DATARATE_212_424 0x01

#define ST21NFCA_EVT_SEND_DATA 0x10
#define ST21NFCA_EVT_FIELD_ON 0x11
#define ST21NFCA_EVT_CARD_DEACTIVATED 0x12
#define ST21NFCA_EVT_CARD_ACTIVATED 0x13
#define ST21NFCA_EVT_FIELD_OFF 0x14

#endif /* __LOCAL_ST21NFCA_H_ */
