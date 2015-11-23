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
#include <linux/skbuff.h>
#include <linux/workqueue.h>

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

/* Reader RF commands */
#define ST21NFCA_WR_XCHG_DATA           0x10

#define ST21NFCA_DEVICE_MGNT_GATE       0x01
#define ST21NFCA_RF_READER_F_GATE       0x14
#define ST21NFCA_RF_CARD_F_GATE		0x24
#define ST21NFCA_APDU_READER_GATE	0xf0
#define ST21NFCA_CONNECTIVITY_GATE	0x41

/*
 * ref ISO7816-3 chap 8.1. the initial character TS is followed by a
 * sequence of at most 32 characters.
 */
#define ST21NFCA_ESE_MAX_LENGTH		33
#define ST21NFCA_ESE_HOST_ID		0xc0

#define DRIVER_DESC "HCI NFC driver for ST21NFCA"

#define ST21NFCA_HCI_MODE		0
#define ST21NFCA_NUM_DEVICES		256

#define ST21NFCA_VENDOR_OUI		0x0080E1 /* STMicroelectronics */
#define ST21NFCA_FACTORY_MODE		2

struct st21nfca_se_status {
	bool is_ese_present;
	bool is_uicc_present;
};

enum st21nfca_state {
	ST21NFCA_ST_COLD,
	ST21NFCA_ST_READY,
};

/**
 * enum nfc_vendor_cmds - supported nfc vendor commands
 *
 * @FACTORY_MODE: Allow to set the driver into a mode where no secure element
 *	are activated. It does not consider any NFC_ATTR_VENDOR_DATA.
 * @HCI_CLEAR_ALL_PIPES: Allow to execute a HCI clear all pipes command.
 *	It does not consider any NFC_ATTR_VENDOR_DATA.
 * @HCI_DM_PUT_DATA: Allow to configure specific CLF registry as for example
 *	RF trimmings or low level drivers configurations (I2C, SPI, SWP).
 * @HCI_DM_UPDATE_AID: Allow to configure an AID routing into the CLF routing
 *	table following RF technology, CLF mode or protocol.
 * @HCI_DM_GET_INFO: Allow to retrieve CLF information.
 * @HCI_DM_GET_DATA: Allow to retrieve CLF configurable data such as low
 *	level drivers configurations or RF trimmings.
 * @HCI_DM_LOAD: Allow to load a firmware into the CLF. A complete
 *	packet can be more than 8KB.
 * @HCI_DM_RESET: Allow to run a CLF reset in order to "commit" CLF
 *	configuration changes without CLF power off.
 * @HCI_GET_PARAM: Allow to retrieve an HCI CLF parameter (for example the
 *	white list).
 * @HCI_DM_FIELD_GENERATOR: Allow to generate different kind of RF
 *	technology. When using this command to anti-collision is done.
 * @HCI_LOOPBACK: Allow to echo a command and test the Dh to CLF
 *	connectivity.
 */
enum nfc_vendor_cmds {
	FACTORY_MODE,
	HCI_CLEAR_ALL_PIPES,
	HCI_DM_PUT_DATA,
	HCI_DM_UPDATE_AID,
	HCI_DM_GET_INFO,
	HCI_DM_GET_DATA,
	HCI_DM_LOAD,
	HCI_DM_RESET,
	HCI_GET_PARAM,
	HCI_DM_FIELD_GENERATOR,
	HCI_LOOPBACK,
};

struct st21nfca_vendor_info {
	struct completion req_completion;
	struct sk_buff *rx_skb;
};

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

struct st21nfca_hci_info {
	struct nfc_phy_ops *phy_ops;
	void *phy_id;

	struct nfc_hci_dev *hdev;
	struct st21nfca_se_status *se_status;

	enum st21nfca_state state;

	struct mutex info_lock;

	int async_cb_type;
	data_exchange_cb_t async_cb;
	void *async_cb_context;

	struct st21nfca_dep_info dep_info;
	struct st21nfca_se_info se_info;
	struct st21nfca_vendor_info vendor_info;
};

int st21nfca_hci_probe(void *phy_id, struct nfc_phy_ops *phy_ops,
		       char *llc_name, int phy_headroom, int phy_tailroom,
		       int phy_payload, struct nfc_hci_dev **hdev,
		       struct st21nfca_se_status *se_status);
void st21nfca_hci_remove(struct nfc_hci_dev *hdev);

int st21nfca_dep_event_received(struct nfc_hci_dev *hdev,
				u8 event, struct sk_buff *skb);
int st21nfca_tm_send_dep_res(struct nfc_hci_dev *hdev, struct sk_buff *skb);

int st21nfca_im_send_atr_req(struct nfc_hci_dev *hdev, u8 *gb, size_t gb_len);
int st21nfca_im_send_dep_req(struct nfc_hci_dev *hdev, struct sk_buff *skb);
void st21nfca_dep_init(struct nfc_hci_dev *hdev);
void st21nfca_dep_deinit(struct nfc_hci_dev *hdev);

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

int st21nfca_hci_loopback_event_received(struct nfc_hci_dev *ndev, u8 event,
					 struct sk_buff *skb);
int st21nfca_vendor_cmds_init(struct nfc_hci_dev *ndev);

#endif /* __LOCAL_ST21NFCA_H_ */
