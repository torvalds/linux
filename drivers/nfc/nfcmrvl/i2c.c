/**
 * Marvell NFC-over-I2C driver: I2C interface related functions
 *
 * Copyright (C) 2015, Marvell International Ltd.
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

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/nfc.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include "nfcmrvl.h"

struct nfcmrvl_i2c_drv_data {
	unsigned long flags;
	struct device *dev;
	struct i2c_client *i2c;
	struct nfcmrvl_private *priv;
};

static int nfcmrvl_i2c_read(struct nfcmrvl_i2c_drv_data *drv_data,
			    struct sk_buff **skb)
{
	int ret;
	struct nci_ctrl_hdr nci_hdr;

	/* Read NCI header to know the payload size */
	ret = i2c_master_recv(drv_data->i2c, (u8 *)&nci_hdr, NCI_CTRL_HDR_SIZE);
	if (ret != NCI_CTRL_HDR_SIZE) {
		nfc_err(&drv_data->i2c->dev, "cannot read NCI header\n");
		return -EBADMSG;
	}

	if (nci_hdr.plen > NCI_MAX_PAYLOAD_SIZE) {
		nfc_err(&drv_data->i2c->dev, "invalid packet payload size\n");
		return -EBADMSG;
	}

	*skb = nci_skb_alloc(drv_data->priv->ndev,
			     nci_hdr.plen + NCI_CTRL_HDR_SIZE, GFP_KERNEL);
	if (!*skb)
		return -ENOMEM;

	/* Copy NCI header into the SKB */
	skb_put_data(*skb, &nci_hdr, NCI_CTRL_HDR_SIZE);

	if (nci_hdr.plen) {
		/* Read the NCI payload */
		ret = i2c_master_recv(drv_data->i2c,
				      skb_put(*skb, nci_hdr.plen),
				      nci_hdr.plen);

		if (ret != nci_hdr.plen) {
			nfc_err(&drv_data->i2c->dev,
				"Invalid frame payload length: %u (expected %u)\n",
				ret, nci_hdr.plen);
			kfree_skb(*skb);
			return -EBADMSG;
		}
	}

	return 0;
}

static irqreturn_t nfcmrvl_i2c_int_irq_thread_fn(int irq, void *drv_data_ptr)
{
	struct nfcmrvl_i2c_drv_data *drv_data = drv_data_ptr;
	struct sk_buff *skb = NULL;
	int ret;

	if (!drv_data->priv)
		return IRQ_HANDLED;

	if (test_bit(NFCMRVL_PHY_ERROR, &drv_data->priv->flags))
		return IRQ_HANDLED;

	ret = nfcmrvl_i2c_read(drv_data, &skb);

	switch (ret) {
	case -EREMOTEIO:
		set_bit(NFCMRVL_PHY_ERROR, &drv_data->priv->flags);
		break;
	case -ENOMEM:
	case -EBADMSG:
		nfc_err(&drv_data->i2c->dev, "read failed %d\n", ret);
		break;
	default:
		if (nfcmrvl_nci_recv_frame(drv_data->priv, skb) < 0)
			nfc_err(&drv_data->i2c->dev, "corrupted RX packet\n");
		break;
	}
	return IRQ_HANDLED;
}

static int nfcmrvl_i2c_nci_open(struct nfcmrvl_private *priv)
{
	struct nfcmrvl_i2c_drv_data *drv_data = priv->drv_data;

	if (!drv_data)
		return -ENODEV;

	return 0;
}

static int nfcmrvl_i2c_nci_close(struct nfcmrvl_private *priv)
{
	return 0;
}

static int nfcmrvl_i2c_nci_send(struct nfcmrvl_private *priv,
				struct sk_buff *skb)
{
	struct nfcmrvl_i2c_drv_data *drv_data = priv->drv_data;
	int ret;

	if (test_bit(NFCMRVL_PHY_ERROR, &priv->flags))
		return -EREMOTEIO;

	ret = i2c_master_send(drv_data->i2c, skb->data, skb->len);

	/* Retry if chip was in standby */
	if (ret == -EREMOTEIO) {
		nfc_info(drv_data->dev, "chip may sleep, retry\n");
		usleep_range(6000, 10000);
		ret = i2c_master_send(drv_data->i2c, skb->data, skb->len);
	}

	if (ret >= 0) {
		if (ret != skb->len) {
			nfc_err(drv_data->dev,
				"Invalid length sent: %u (expected %u)\n",
				ret, skb->len);
			ret = -EREMOTEIO;
		} else
			ret = 0;
	}

	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	consume_skb(skb);
	return 0;
}

static void nfcmrvl_i2c_nci_update_config(struct nfcmrvl_private *priv,
					  const void *param)
{
}

static struct nfcmrvl_if_ops i2c_ops = {
	.nci_open = nfcmrvl_i2c_nci_open,
	.nci_close = nfcmrvl_i2c_nci_close,
	.nci_send = nfcmrvl_i2c_nci_send,
	.nci_update_config = nfcmrvl_i2c_nci_update_config,
};

static int nfcmrvl_i2c_parse_dt(struct device_node *node,
				struct nfcmrvl_platform_data *pdata)
{
	int ret;

	ret = nfcmrvl_parse_dt(node, pdata);
	if (ret < 0) {
		pr_err("Failed to get generic entries\n");
		return ret;
	}

	if (of_find_property(node, "i2c-int-falling", NULL))
		pdata->irq_polarity = IRQF_TRIGGER_FALLING;
	else
		pdata->irq_polarity = IRQF_TRIGGER_RISING;

	ret = irq_of_parse_and_map(node, 0);
	if (!ret) {
		pr_err("Unable to get irq\n");
		return -EINVAL;
	}
	pdata->irq = ret;

	return 0;
}

static int nfcmrvl_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct nfcmrvl_i2c_drv_data *drv_data;
	struct nfcmrvl_platform_data *pdata;
	struct nfcmrvl_platform_data config;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(&client->dev, "Need I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	drv_data = devm_kzalloc(&client->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->i2c = client;
	drv_data->dev = &client->dev;
	drv_data->priv = NULL;

	i2c_set_clientdata(client, drv_data);

	pdata = client->dev.platform_data;

	if (!pdata && client->dev.of_node)
		if (nfcmrvl_i2c_parse_dt(client->dev.of_node, &config) == 0)
			pdata = &config;

	if (!pdata)
		return -EINVAL;

	/* Request the read IRQ */
	ret = devm_request_threaded_irq(&drv_data->i2c->dev, pdata->irq,
					NULL, nfcmrvl_i2c_int_irq_thread_fn,
					pdata->irq_polarity | IRQF_ONESHOT,
					"nfcmrvl_i2c_int", drv_data);
	if (ret < 0) {
		nfc_err(&drv_data->i2c->dev,
			"Unable to register IRQ handler\n");
		return ret;
	}

	drv_data->priv = nfcmrvl_nci_register_dev(NFCMRVL_PHY_I2C,
						  drv_data, &i2c_ops,
						  &drv_data->i2c->dev, pdata);

	if (IS_ERR(drv_data->priv))
		return PTR_ERR(drv_data->priv);

	drv_data->priv->support_fw_dnld = true;

	return 0;
}

static int nfcmrvl_i2c_remove(struct i2c_client *client)
{
	struct nfcmrvl_i2c_drv_data *drv_data = i2c_get_clientdata(client);

	nfcmrvl_nci_unregister_dev(drv_data->priv);

	return 0;
}


static const struct of_device_id of_nfcmrvl_i2c_match[] = {
	{ .compatible = "marvell,nfc-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, of_nfcmrvl_i2c_match);

static const struct i2c_device_id nfcmrvl_i2c_id_table[] = {
	{ "nfcmrvl_i2c", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, nfcmrvl_i2c_id_table);

static struct i2c_driver nfcmrvl_i2c_driver = {
	.probe = nfcmrvl_i2c_probe,
	.id_table = nfcmrvl_i2c_id_table,
	.remove = nfcmrvl_i2c_remove,
	.driver = {
		.name		= "nfcmrvl_i2c",
		.of_match_table	= of_match_ptr(of_nfcmrvl_i2c_match),
	},
};

module_i2c_driver(nfcmrvl_i2c_driver);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell NFC-over-I2C driver");
MODULE_LICENSE("GPL v2");
