/**
 * Marvell NFC driver
 *
 * Copyright (C) 2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available on the worldwide web at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 **/

/* Define private flags: */
#define NFCMRVL_NCI_RUNNING			1

#define NFCMRVL_EXT_COEX_ID			0xE0
#define NFCMRVL_NOT_ALLOWED_ID			0xE1
#define NFCMRVL_ACTIVE_ID			0xE2
#define NFCMRVL_EXT_COEX_ENABLE			1
#define NFCMRVL_GPIO_PIN_NFC_NOT_ALLOWED	0xA
#define NFCMRVL_GPIO_PIN_NFC_ACTIVE		0xB
#define NFCMRVL_NCI_MAX_EVENT_SIZE		260

/*
** HCI defines
*/

#define NFCMRVL_HCI_EVENT_HEADER_SIZE		0x04
#define NFCMRVL_HCI_EVENT_CODE			0x04
#define NFCMRVL_HCI_NFC_EVENT_CODE		0xFF
#define NFCMRVL_HCI_COMMAND_CODE		0x01
#define NFCMRVL_HCI_OGF				0x81
#define NFCMRVL_HCI_OCF				0xFE

#define NFCMRVL_DEV_FLAG_HCI_MUXED		(1 << 0)
#define NFCMRVL_DEV_FLAG_SET_RESET_N_IO(X)	((X) << 16)
#define NFCMRVL_DEV_FLAG_GET_RESET_N_IO(X)	((X) >> 16)

struct nfcmrvl_private {

	/* Tell if NCI packets are encapsulated in HCI ones */
	int hci_muxed;
	struct nci_dev *ndev;

	/* Reset IO (0 if not available) */
	int reset_n_io;

	unsigned long flags;
	void *drv_data;
	struct device *dev;
	struct nfcmrvl_if_ops *if_ops;
};

struct nfcmrvl_if_ops {
	int (*nci_open) (struct nfcmrvl_private *priv);
	int (*nci_close) (struct nfcmrvl_private *priv);
	int (*nci_send) (struct nfcmrvl_private *priv, struct sk_buff *skb);
};

void nfcmrvl_nci_unregister_dev(struct nfcmrvl_private *priv);
int nfcmrvl_nci_recv_frame(struct nfcmrvl_private *priv, struct sk_buff *skb);
struct nfcmrvl_private *nfcmrvl_nci_register_dev(void *drv_data,
						 struct nfcmrvl_if_ops *ops,
						 struct device *dev,
						 unsigned int flags);

void nfcmrvl_chip_reset(struct nfcmrvl_private *priv);
