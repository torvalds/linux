/**
 * Marvell NFC driver
 *
 * Copyright (C) 2014-2015, Marvell International Ltd.
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

#ifndef _NFCMRVL_H_
#define _NFCMRVL_H_

#include <linux/platform_data/nfcmrvl.h>

#include "fw_dnld.h"

/* Define private flags: */
#define NFCMRVL_NCI_RUNNING			1
#define NFCMRVL_PHY_ERROR			2

#define NFCMRVL_EXT_COEX_ID			0xE0
#define NFCMRVL_NOT_ALLOWED_ID			0xE1
#define NFCMRVL_ACTIVE_ID			0xE2
#define NFCMRVL_EXT_COEX_ENABLE			1
#define NFCMRVL_GPIO_PIN_NFC_NOT_ALLOWED	0xA
#define NFCMRVL_GPIO_PIN_NFC_ACTIVE		0xB
#define NFCMRVL_NCI_MAX_EVENT_SIZE		260

/*
** NCI FW Parmaters
*/

#define NFCMRVL_PB_BAIL_OUT			0x11
#define NFCMRVL_PROP_REF_CLOCK			0xF0
#define NFCMRVL_PROP_SET_HI_CONFIG		0xF1

/*
** HCI defines
*/

#define NFCMRVL_HCI_EVENT_HEADER_SIZE		0x04
#define NFCMRVL_HCI_EVENT_CODE			0x04
#define NFCMRVL_HCI_NFC_EVENT_CODE		0xFF
#define NFCMRVL_HCI_COMMAND_CODE		0x01
#define NFCMRVL_HCI_OGF				0x81
#define NFCMRVL_HCI_OCF				0xFE

enum nfcmrvl_phy {
	NFCMRVL_PHY_USB		= 0,
	NFCMRVL_PHY_UART	= 1,
	NFCMRVL_PHY_I2C		= 2,
	NFCMRVL_PHY_SPI		= 3,
};

struct nfcmrvl_private {

	unsigned long flags;

	/* Platform configuration */
	struct nfcmrvl_platform_data config;

	/* Parent dev */
	struct nci_dev *ndev;

	/* FW download context */
	struct nfcmrvl_fw_dnld fw_dnld;

	/* FW download support */
	bool support_fw_dnld;

	/*
	** PHY related information
	*/

	/* PHY driver context */
	void *drv_data;
	/* PHY device */
	struct device *dev;
	/* PHY type */
	enum nfcmrvl_phy phy;
	/* Low level driver ops */
	struct nfcmrvl_if_ops *if_ops;
};

struct nfcmrvl_if_ops {
	int (*nci_open) (struct nfcmrvl_private *priv);
	int (*nci_close) (struct nfcmrvl_private *priv);
	int (*nci_send) (struct nfcmrvl_private *priv, struct sk_buff *skb);
	void (*nci_update_config)(struct nfcmrvl_private *priv,
				  const void *param);
};

void nfcmrvl_nci_unregister_dev(struct nfcmrvl_private *priv);
int nfcmrvl_nci_recv_frame(struct nfcmrvl_private *priv, struct sk_buff *skb);
struct nfcmrvl_private *nfcmrvl_nci_register_dev(enum nfcmrvl_phy phy,
				void *drv_data,
				struct nfcmrvl_if_ops *ops,
				struct device *dev,
				struct nfcmrvl_platform_data *pdata);


void nfcmrvl_chip_reset(struct nfcmrvl_private *priv);
void nfcmrvl_chip_halt(struct nfcmrvl_private *priv);

int nfcmrvl_parse_dt(struct device_node *node,
		     struct nfcmrvl_platform_data *pdata);

#endif
