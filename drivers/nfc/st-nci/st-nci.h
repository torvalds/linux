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

#define ST_NCI_DEVICE_MGNT_GATE		0x01

#define ST_NCI_VENDOR_OUI 0x0080E1 /* STMicroelectronics */
#define ST_NCI_FACTORY_MODE 2

struct nci_mode_set_cmd {
	u8 cmd_type;
	u8 mode;
} __packed;

struct nci_mode_set_rsp {
	u8 status;
} __packed;

struct st_nci_se_status {
	bool is_ese_present;
	bool is_uicc_present;
};

struct st_nci_se_info {
	struct st_nci_se_status *se_status;
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
 * @HCI_DM_DIRECT_LOAD: Allow to load a firmware into the CLF. A complete
 *	packet can be more than 8KB.
 * @HCI_DM_RESET: Allow to run a CLF reset in order to "commit" CLF
 *	configuration changes without CLF power off.
 * @HCI_GET_PARAM: Allow to retrieve an HCI CLF parameter (for example the
 *	white list).
 * @HCI_DM_FIELD_GENERATOR: Allow to generate different kind of RF
 *	technology. When using this command to anti-collision is done.
 * @LOOPBACK: Allow to echo a command and test the Dh to CLF connectivity.
 * @HCI_DM_VDC_MEASUREMENT_VALUE: Allow to measure the field applied on the
 *	CLF antenna. A value between 0 and 0x0f is returned. 0 is maximum.
 * @HCI_DM_FWUPD_START: Allow to put CLF into firmware update mode. It is a
 *	specific CLF command as there is no GPIO for this.
 * @HCI_DM_FWUPD_END:  Allow to complete firmware update.
 * @HCI_DM_VDC_VALUE_COMPARISON: Allow to compare the field applied on the
 *	CLF antenna to a reference value.
 * @MANUFACTURER_SPECIFIC: Allow to retrieve manufacturer specific data
 *	received during a NCI_CORE_INIT_CMD.
 */
enum nfc_vendor_cmds {
	FACTORY_MODE,
	HCI_CLEAR_ALL_PIPES,
	HCI_DM_PUT_DATA,
	HCI_DM_UPDATE_AID,
	HCI_DM_GET_INFO,
	HCI_DM_GET_DATA,
	HCI_DM_DIRECT_LOAD,
	HCI_DM_RESET,
	HCI_GET_PARAM,
	HCI_DM_FIELD_GENERATOR,
	LOOPBACK,
	HCI_DM_FWUPD_START,
	HCI_DM_FWUPD_END,
	HCI_DM_VDC_MEASUREMENT_VALUE,
	HCI_DM_VDC_VALUE_COMPARISON,
	MANUFACTURER_SPECIFIC,
};

struct st_nci_info {
	struct llt_ndlc *ndlc;
	unsigned long flags;

	struct st_nci_se_info se_info;
};

void st_nci_remove(struct nci_dev *ndev);
int st_nci_probe(struct llt_ndlc *ndlc, int phy_headroom,
		 int phy_tailroom, struct st_nci_se_status *se_status);

int st_nci_se_init(struct nci_dev *ndev, struct st_nci_se_status *se_status);
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

int st_nci_vendor_cmds_init(struct nci_dev *ndev);

#endif /* __LOCAL_ST_NCI_H_ */
