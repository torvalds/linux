/*
 * I2C link layer for the NXP NCI driver
 *
 * Copyright (C) 2014  NXP Semiconductors  All rights reserved.
 *
 * Authors: Clément Perrochaud <clement.perrochaud@nxp.com>
 *
 * Derived from PN544 device driver:
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_data/nxp-nci.h>
#include <linux/unaligned/access_ok.h>

#include <net/nfc/nfc.h>

#include "nxp-nci.h"

#define NXP_NCI_I2C_DRIVER_NAME	"nxp-nci_i2c"

#define NXP_NCI_I2C_MAX_PAYLOAD	32

struct nxp_nci_i2c_phy {
	struct i2c_client *i2c_dev;
	struct nci_dev *ndev;

	unsigned int gpio_en;
	unsigned int gpio_fw;

	int hard_fault; /*
			 * < 0 if hardware error occurred (e.g. i2c err)
			 * and prevents normal operation.
			 */
};

static int nxp_nci_i2c_set_mode(void *phy_id,
				    enum nxp_nci_mode mode)
{
	struct nxp_nci_i2c_phy *phy = (struct nxp_nci_i2c_phy *) phy_id;

	gpio_set_value(phy->gpio_fw, (mode == NXP_NCI_MODE_FW) ? 1 : 0);
	gpio_set_value(phy->gpio_en, (mode != NXP_NCI_MODE_COLD) ? 1 : 0);
	usleep_range(10000, 15000);

	if (mode == NXP_NCI_MODE_COLD)
		phy->hard_fault = 0;

	return 0;
}

static int nxp_nci_i2c_write(void *phy_id, struct sk_buff *skb)
{
	int r;
	struct nxp_nci_i2c_phy *phy = phy_id;
	struct i2c_client *client = phy->i2c_dev;

	if (phy->hard_fault != 0)
		return phy->hard_fault;

	r = i2c_master_send(client, skb->data, skb->len);
	if (r == -EREMOTEIO) {
		/* Retry, chip was in standby */
		usleep_range(110000, 120000);
		r = i2c_master_send(client, skb->data, skb->len);
	}

	if (r < 0) {
		nfc_err(&client->dev, "Error %d on I2C send\n", r);
	} else if (r != skb->len) {
		nfc_err(&client->dev,
			"Invalid length sent: %u (expected %u)\n",
			r, skb->len);
		r = -EREMOTEIO;
	} else {
		/* Success but return 0 and not number of bytes */
		r = 0;
	}

	return r;
}

static struct nxp_nci_phy_ops i2c_phy_ops = {
	.set_mode = nxp_nci_i2c_set_mode,
	.write = nxp_nci_i2c_write,
};

static int nxp_nci_i2c_fw_read(struct nxp_nci_i2c_phy *phy,
			       struct sk_buff **skb)
{
	struct i2c_client *client = phy->i2c_dev;
	u16 header;
	size_t frame_len;
	int r;

	r = i2c_master_recv(client, (u8 *) &header, NXP_NCI_FW_HDR_LEN);
	if (r < 0) {
		goto fw_read_exit;
	} else if (r != NXP_NCI_FW_HDR_LEN) {
		nfc_err(&client->dev, "Incorrect header length: %u\n", r);
		r = -EBADMSG;
		goto fw_read_exit;
	}

	frame_len = (get_unaligned_be16(&header) & NXP_NCI_FW_FRAME_LEN_MASK) +
		    NXP_NCI_FW_CRC_LEN;

	*skb = alloc_skb(NXP_NCI_FW_HDR_LEN + frame_len, GFP_KERNEL);
	if (*skb == NULL) {
		r = -ENOMEM;
		goto fw_read_exit;
	}

	memcpy(skb_put(*skb, NXP_NCI_FW_HDR_LEN), &header, NXP_NCI_FW_HDR_LEN);

	r = i2c_master_recv(client, skb_put(*skb, frame_len), frame_len);
	if (r != frame_len) {
		nfc_err(&client->dev,
			"Invalid frame length: %u (expected %zu)\n",
			r, frame_len);
		r = -EBADMSG;
		goto fw_read_exit_free_skb;
	}

	return 0;

fw_read_exit_free_skb:
	kfree_skb(*skb);
fw_read_exit:
	return r;
}

static int nxp_nci_i2c_nci_read(struct nxp_nci_i2c_phy *phy,
				struct sk_buff **skb)
{
	struct nci_ctrl_hdr header; /* May actually be a data header */
	struct i2c_client *client = phy->i2c_dev;
	int r;

	r = i2c_master_recv(client, (u8 *) &header, NCI_CTRL_HDR_SIZE);
	if (r < 0) {
		goto nci_read_exit;
	} else if (r != NCI_CTRL_HDR_SIZE) {
		nfc_err(&client->dev, "Incorrect header length: %u\n", r);
		r = -EBADMSG;
		goto nci_read_exit;
	}

	*skb = alloc_skb(NCI_CTRL_HDR_SIZE + header.plen, GFP_KERNEL);
	if (*skb == NULL) {
		r = -ENOMEM;
		goto nci_read_exit;
	}

	memcpy(skb_put(*skb, NCI_CTRL_HDR_SIZE), (void *) &header,
	       NCI_CTRL_HDR_SIZE);

	r = i2c_master_recv(client, skb_put(*skb, header.plen), header.plen);
	if (r != header.plen) {
		nfc_err(&client->dev,
			"Invalid frame payload length: %u (expected %u)\n",
			r, header.plen);
		r = -EBADMSG;
		goto nci_read_exit_free_skb;
	}

	return 0;

nci_read_exit_free_skb:
	kfree_skb(*skb);
nci_read_exit:
	return r;
}

static irqreturn_t nxp_nci_i2c_irq_thread_fn(int irq, void *phy_id)
{
	struct nxp_nci_i2c_phy *phy = phy_id;
	struct i2c_client *client;
	struct nxp_nci_info *info;

	struct sk_buff *skb = NULL;
	int r = 0;

	if (!phy || !phy->ndev)
		goto exit_irq_none;

	client = phy->i2c_dev;

	if (!client || irq != client->irq)
		goto exit_irq_none;

	info = nci_get_drvdata(phy->ndev);

	if (!info)
		goto exit_irq_none;

	mutex_lock(&info->info_lock);

	if (phy->hard_fault != 0)
		goto exit_irq_handled;

	switch (info->mode) {
	case NXP_NCI_MODE_NCI:
		r = nxp_nci_i2c_nci_read(phy, &skb);
		break;
	case NXP_NCI_MODE_FW:
		r = nxp_nci_i2c_fw_read(phy, &skb);
		break;
	case NXP_NCI_MODE_COLD:
		r = -EREMOTEIO;
		break;
	}

	if (r == -EREMOTEIO) {
		phy->hard_fault = r;
		skb = NULL;
	} else if (r < 0) {
		nfc_err(&client->dev, "Read failed with error %d\n", r);
		goto exit_irq_handled;
	}

	switch (info->mode) {
	case NXP_NCI_MODE_NCI:
		nci_recv_frame(phy->ndev, skb);
		break;
	case NXP_NCI_MODE_FW:
		nxp_nci_fw_recv_frame(phy->ndev, skb);
		break;
	case NXP_NCI_MODE_COLD:
		break;
	}

exit_irq_handled:
	mutex_unlock(&info->info_lock);
	return IRQ_HANDLED;
exit_irq_none:
	WARN_ON_ONCE(1);
	return IRQ_NONE;
}

#ifdef CONFIG_OF

static int nxp_nci_i2c_parse_devtree(struct i2c_client *client)
{
	struct nxp_nci_i2c_phy *phy = i2c_get_clientdata(client);
	struct device_node *pp;
	int r;

	pp = client->dev.of_node;
	if (!pp)
		return -ENODEV;

	r = of_get_named_gpio(pp, "enable-gpios", 0);
	if (r == -EPROBE_DEFER)
		r = of_get_named_gpio(pp, "enable-gpios", 0);
	if (r < 0) {
		nfc_err(&client->dev, "Failed to get EN gpio, error: %d\n", r);
		return r;
	}
	phy->gpio_en = r;

	r = of_get_named_gpio(pp, "firmware-gpios", 0);
	if (r == -EPROBE_DEFER)
		r = of_get_named_gpio(pp, "firmware-gpios", 0);
	if (r < 0) {
		nfc_err(&client->dev, "Failed to get FW gpio, error: %d\n", r);
		return r;
	}
	phy->gpio_fw = r;

	r = irq_of_parse_and_map(pp, 0);
	if (r < 0) {
		nfc_err(&client->dev, "Unable to get irq, error: %d\n", r);
		return r;
	}
	client->irq = r;

	return 0;
}

#else

static int nxp_nci_i2c_parse_devtree(struct i2c_client *client)
{
	return -ENODEV;
}

#endif

static int nxp_nci_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct nxp_nci_i2c_phy *phy;
	struct nxp_nci_nfc_platform_data *pdata;
	int r;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(&client->dev, "Need I2C_FUNC_I2C\n");
		r = -ENODEV;
		goto probe_exit;
	}

	phy = devm_kzalloc(&client->dev, sizeof(struct nxp_nci_i2c_phy),
			   GFP_KERNEL);
	if (!phy) {
		r = -ENOMEM;
		goto probe_exit;
	}

	phy->i2c_dev = client;
	i2c_set_clientdata(client, phy);

	pdata = client->dev.platform_data;

	if (!pdata && client->dev.of_node) {
		r = nxp_nci_i2c_parse_devtree(client);
		if (r < 0) {
			nfc_err(&client->dev, "Failed to get DT data\n");
			goto probe_exit;
		}
	} else if (pdata) {
		phy->gpio_en = pdata->gpio_en;
		phy->gpio_fw = pdata->gpio_fw;
		client->irq = pdata->irq;
	} else {
		nfc_err(&client->dev, "No platform data\n");
		r = -EINVAL;
		goto probe_exit;
	}

	r = devm_gpio_request_one(&phy->i2c_dev->dev, phy->gpio_en,
				  GPIOF_OUT_INIT_LOW, "nxp_nci_en");
	if (r < 0)
		goto probe_exit;

	r = devm_gpio_request_one(&phy->i2c_dev->dev, phy->gpio_fw,
				  GPIOF_OUT_INIT_LOW, "nxp_nci_fw");
	if (r < 0)
		goto probe_exit;

	r = nxp_nci_probe(phy, &client->dev, &i2c_phy_ops,
			  NXP_NCI_I2C_MAX_PAYLOAD, &phy->ndev);
	if (r < 0)
		goto probe_exit;

	r = request_threaded_irq(client->irq, NULL,
				 nxp_nci_i2c_irq_thread_fn,
				 IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				 NXP_NCI_I2C_DRIVER_NAME, phy);
	if (r < 0)
		nfc_err(&client->dev, "Unable to register IRQ handler\n");

probe_exit:
	return r;
}

static int nxp_nci_i2c_remove(struct i2c_client *client)
{
	struct nxp_nci_i2c_phy *phy = i2c_get_clientdata(client);

	nxp_nci_remove(phy->ndev);
	free_irq(client->irq, phy);

	return 0;
}

static struct i2c_device_id nxp_nci_i2c_id_table[] = {
	{"nxp-nci_i2c", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, nxp_nci_i2c_id_table);

static const struct of_device_id of_nxp_nci_i2c_match[] = {
	{ .compatible = "nxp,nxp-nci-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, of_nxp_nci_i2c_match);

static struct i2c_driver nxp_nci_i2c_driver = {
	.driver = {
		   .name = NXP_NCI_I2C_DRIVER_NAME,
		   .owner  = THIS_MODULE,
		   .of_match_table = of_match_ptr(of_nxp_nci_i2c_match),
		  },
	.probe = nxp_nci_i2c_probe,
	.id_table = nxp_nci_i2c_id_table,
	.remove = nxp_nci_i2c_remove,
};

module_i2c_driver(nxp_nci_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C driver for NXP NCI NFC controllers");
MODULE_AUTHOR("Clément Perrochaud <clement.perrochaud@nxp.com>");
