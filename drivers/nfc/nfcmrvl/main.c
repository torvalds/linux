/*
 * Marvell NFC driver: major functions
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
 */

#include <linux/module.h>
#include <linux/nfc.h>
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include "nfcmrvl.h"

#define VERSION "1.0"

static int nfcmrvl_nci_open(struct nci_dev *ndev)
{
	struct nfcmrvl_private *priv = nci_get_drvdata(ndev);
	int err;

	if (test_and_set_bit(NFCMRVL_NCI_RUNNING, &priv->flags))
		return 0;

	err = priv->if_ops->nci_open(priv);

	if (err)
		clear_bit(NFCMRVL_NCI_RUNNING, &priv->flags);

	return err;
}

static int nfcmrvl_nci_close(struct nci_dev *ndev)
{
	struct nfcmrvl_private *priv = nci_get_drvdata(ndev);

	if (!test_and_clear_bit(NFCMRVL_NCI_RUNNING, &priv->flags))
		return 0;

	priv->if_ops->nci_close(priv);

	return 0;
}

static int nfcmrvl_nci_send(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nfcmrvl_private *priv = nci_get_drvdata(ndev);

	nfc_info(priv->dev, "send entry, len %d\n", skb->len);

	skb->dev = (void *)ndev;

	if (!test_bit(NFCMRVL_NCI_RUNNING, &priv->flags))
		return -EBUSY;

	return priv->if_ops->nci_send(priv, skb);
}

static int nfcmrvl_nci_setup(struct nci_dev *ndev)
{
	__u8 val;

	val = NFCMRVL_GPIO_PIN_NFC_NOT_ALLOWED;
	nci_set_config(ndev, NFCMRVL_NOT_ALLOWED_ID, 1, &val);
	val = NFCMRVL_GPIO_PIN_NFC_ACTIVE;
	nci_set_config(ndev, NFCMRVL_ACTIVE_ID, 1, &val);
	val = NFCMRVL_EXT_COEX_ENABLE;
	nci_set_config(ndev, NFCMRVL_EXT_COEX_ID, 1, &val);

	return 0;
}

static struct nci_ops nfcmrvl_nci_ops = {
	.open = nfcmrvl_nci_open,
	.close = nfcmrvl_nci_close,
	.send = nfcmrvl_nci_send,
	.setup = nfcmrvl_nci_setup,
};

struct nfcmrvl_private *nfcmrvl_nci_register_dev(void *drv_data,
						 struct nfcmrvl_if_ops *ops,
						 struct device *dev)
{
	struct nfcmrvl_private *priv;
	int rc;
	u32 protocols;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->drv_data = drv_data;
	priv->if_ops = ops;
	priv->dev = dev;

	protocols = NFC_PROTO_JEWEL_MASK
		| NFC_PROTO_MIFARE_MASK | NFC_PROTO_FELICA_MASK
		| NFC_PROTO_ISO14443_MASK
		| NFC_PROTO_ISO14443_B_MASK
		| NFC_PROTO_NFC_DEP_MASK;

	priv->ndev = nci_allocate_device(&nfcmrvl_nci_ops, protocols, 0, 0);
	if (!priv->ndev) {
		nfc_err(dev, "nci_allocate_device failed\n");
		rc = -ENOMEM;
		goto error;
	}

	nci_set_drvdata(priv->ndev, priv);

	rc = nci_register_device(priv->ndev);
	if (rc) {
		nfc_err(dev, "nci_register_device failed %d\n", rc);
		nci_free_device(priv->ndev);
		goto error;
	}

	nfc_info(dev, "registered with nci successfully\n");
	return priv;

error:
	kfree(priv);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(nfcmrvl_nci_register_dev);

void nfcmrvl_nci_unregister_dev(struct nfcmrvl_private *priv)
{
	struct nci_dev *ndev = priv->ndev;

	nci_unregister_device(ndev);
	nci_free_device(ndev);
	kfree(priv);
}
EXPORT_SYMBOL_GPL(nfcmrvl_nci_unregister_dev);

int nfcmrvl_nci_recv_frame(struct nfcmrvl_private *priv, void *data, int count)
{
	struct sk_buff *skb;

	skb = nci_skb_alloc(priv->ndev, count, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	memcpy(skb_put(skb, count), data, count);
	nci_recv_frame(priv->ndev, skb);

	return count;
}
EXPORT_SYMBOL_GPL(nfcmrvl_nci_recv_frame);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell NFC driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL v2");
