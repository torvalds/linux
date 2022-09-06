// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C link layer for the NXP NCI driver
 *
 * Copyright (C) 2014  NXP Semiconductors  All rights reserved.
 * Copyright (C) 2012-2015  Intel Corporation. All rights reserved.
 *
 * Authors: Clément Perrochaud <clement.perrochaud@nxp.com>
 * Authors: Oleg Zhurakivskyy <oleg.zhurakivskyy@intel.com>
 *
 * Derived from PN544 device driver:
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/gpio/consumer.h>
#include <asm/unaligned.h>

#include <net/nfc/nfc.h>

#include "nxp-nci.h"

#define NXP_NCI_I2C_DRIVER_NAME	"nxp-nci_i2c"

#define NXP_NCI_I2C_MAX_PAYLOAD	32

struct nxp_nci_i2c_phy {
	struct i2c_client *i2c_dev;
	struct nci_dev *ndev;

	struct gpio_desc *gpiod_en;
	struct gpio_desc *gpiod_fw;

	int hard_fault; /*
			 * < 0 if hardware error occurred (e.g. i2c err)
			 * and prevents normal operation.
			 */
};

static int nxp_nci_i2c_set_mode(void *phy_id,
				    enum nxp_nci_mode mode)
{
	struct nxp_nci_i2c_phy *phy = (struct nxp_nci_i2c_phy *) phy_id;

	gpiod_set_value(phy->gpiod_fw, (mode == NXP_NCI_MODE_FW) ? 1 : 0);
	gpiod_set_value(phy->gpiod_en, (mode != NXP_NCI_MODE_COLD) ? 1 : 0);
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
	if (r < 0) {
		/* Retry, chip was in standby */
		msleep(110);
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

static const struct nxp_nci_phy_ops i2c_phy_ops = {
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

	frame_len = (be16_to_cpu(header) & NXP_NCI_FW_FRAME_LEN_MASK) +
		    NXP_NCI_FW_CRC_LEN;

	*skb = alloc_skb(NXP_NCI_FW_HDR_LEN + frame_len, GFP_KERNEL);
	if (*skb == NULL) {
		r = -ENOMEM;
		goto fw_read_exit;
	}

	skb_put_data(*skb, &header, NXP_NCI_FW_HDR_LEN);

	r = i2c_master_recv(client, skb_put(*skb, frame_len), frame_len);
	if (r < 0) {
		goto fw_read_exit_free_skb;
	} else if (r != frame_len) {
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

	skb_put_data(*skb, (void *)&header, NCI_CTRL_HDR_SIZE);

	if (!header.plen)
		return 0;

	r = i2c_master_recv(client, skb_put(*skb, header.plen), header.plen);
	if (r < 0) {
		goto nci_read_exit_free_skb;
	} else if (r != header.plen) {
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
		if (info->mode == NXP_NCI_MODE_FW)
			nxp_nci_fw_recv_frame(phy->ndev, NULL);
	}
	if (r < 0) {
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

static const struct acpi_gpio_params firmware_gpios = { 1, 0, false };
static const struct acpi_gpio_params enable_gpios = { 2, 0, false };

static const struct acpi_gpio_mapping acpi_nxp_nci_gpios[] = {
	{ "enable-gpios", &enable_gpios, 1 },
	{ "firmware-gpios", &firmware_gpios, 1 },
	{ }
};

static int nxp_nci_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct nxp_nci_i2c_phy *phy;
	int r;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(&client->dev, "Need I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	phy = devm_kzalloc(&client->dev, sizeof(struct nxp_nci_i2c_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->i2c_dev = client;
	i2c_set_clientdata(client, phy);

	r = devm_acpi_dev_add_driver_gpios(dev, acpi_nxp_nci_gpios);
	if (r)
		dev_dbg(dev, "Unable to add GPIO mapping table\n");

	phy->gpiod_en = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(phy->gpiod_en)) {
		nfc_err(dev, "Failed to get EN gpio\n");
		return PTR_ERR(phy->gpiod_en);
	}

	phy->gpiod_fw = devm_gpiod_get_optional(dev, "firmware", GPIOD_OUT_LOW);
	if (IS_ERR(phy->gpiod_fw)) {
		nfc_err(dev, "Failed to get FW gpio\n");
		return PTR_ERR(phy->gpiod_fw);
	}

	r = nxp_nci_probe(phy, &client->dev, &i2c_phy_ops,
			  NXP_NCI_I2C_MAX_PAYLOAD, &phy->ndev);
	if (r < 0)
		return r;

	r = request_threaded_irq(client->irq, NULL,
				 nxp_nci_i2c_irq_thread_fn,
				 IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				 NXP_NCI_I2C_DRIVER_NAME, phy);
	if (r < 0)
		nfc_err(&client->dev, "Unable to register IRQ handler\n");

	return r;
}

static int nxp_nci_i2c_remove(struct i2c_client *client)
{
	struct nxp_nci_i2c_phy *phy = i2c_get_clientdata(client);

	nxp_nci_remove(phy->ndev);
	free_irq(client->irq, phy);

	return 0;
}

static const struct i2c_device_id nxp_nci_i2c_id_table[] = {
	{"nxp-nci_i2c", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, nxp_nci_i2c_id_table);

static const struct of_device_id of_nxp_nci_i2c_match[] = {
	{ .compatible = "nxp,nxp-nci-i2c", },
	{}
};
MODULE_DEVICE_TABLE(of, of_nxp_nci_i2c_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id acpi_id[] = {
	{ "NXP1001" },
	{ "NXP7471" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, acpi_id);
#endif

static struct i2c_driver nxp_nci_i2c_driver = {
	.driver = {
		   .name = NXP_NCI_I2C_DRIVER_NAME,
		   .acpi_match_table = ACPI_PTR(acpi_id),
		   .of_match_table = of_nxp_nci_i2c_match,
		  },
	.probe = nxp_nci_i2c_probe,
	.id_table = nxp_nci_i2c_id_table,
	.remove = nxp_nci_i2c_remove,
};

module_i2c_driver(nxp_nci_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C driver for NXP NCI NFC controllers");
MODULE_AUTHOR("Clément Perrochaud <clement.perrochaud@nxp.com>");
MODULE_AUTHOR("Oleg Zhurakivskyy <oleg.zhurakivskyy@intel.com>");
