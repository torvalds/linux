// SPDX-License-Identifier: GPL-2.0-or-later
/* -------------------------------------------------------------------------
 * Copyright (C) 2014-2016, Intel Corporation
 *
 * -------------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/nfc.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <net/nfc/nfc.h>
#include <net/nfc/nci_core.h>

#include "fdp.h"

#define FDP_I2C_DRIVER_NAME	"fdp_nci_i2c"

#define FDP_DP_CLOCK_TYPE_NAME	"clock-type"
#define FDP_DP_CLOCK_FREQ_NAME	"clock-freq"
#define FDP_DP_FW_VSC_CFG_NAME	"fw-vsc-cfg"

#define FDP_FRAME_HEADROOM	2
#define FDP_FRAME_TAILROOM	1

#define FDP_NCI_I2C_MIN_PAYLOAD	5
#define FDP_NCI_I2C_MAX_PAYLOAD	261

#define FDP_POWER_OFF		0
#define FDP_POWER_ON		1

#define fdp_nci_i2c_dump_skb(dev, prefix, skb)				\
	print_hex_dump(KERN_DEBUG, prefix": ", DUMP_PREFIX_OFFSET,	\
		       16, 1, (skb)->data, (skb)->len, 0)

static void fdp_nci_i2c_reset(struct fdp_i2c_phy *phy)
{
	/* Reset RST/WakeUP for at least 100 micro-second */
	gpiod_set_value_cansleep(phy->power_gpio, FDP_POWER_OFF);
	usleep_range(1000, 4000);
	gpiod_set_value_cansleep(phy->power_gpio, FDP_POWER_ON);
	usleep_range(10000, 14000);
}

static int fdp_nci_i2c_enable(void *phy_id)
{
	struct fdp_i2c_phy *phy = phy_id;

	dev_dbg(&phy->i2c_dev->dev, "%s\n", __func__);
	fdp_nci_i2c_reset(phy);

	return 0;
}

static void fdp_nci_i2c_disable(void *phy_id)
{
	struct fdp_i2c_phy *phy = phy_id;

	dev_dbg(&phy->i2c_dev->dev, "%s\n", __func__);
	fdp_nci_i2c_reset(phy);
}

static void fdp_nci_i2c_add_len_lrc(struct sk_buff *skb)
{
	u8 lrc = 0;
	u16 len, i;

	/* Add length header */
	len = skb->len;
	*(u8 *)skb_push(skb, 1) = len & 0xff;
	*(u8 *)skb_push(skb, 1) = len >> 8;

	/* Compute and add lrc */
	for (i = 0; i < len + 2; i++)
		lrc ^= skb->data[i];

	skb_put_u8(skb, lrc);
}

static void fdp_nci_i2c_remove_len_lrc(struct sk_buff *skb)
{
	skb_pull(skb, FDP_FRAME_HEADROOM);
	skb_trim(skb, skb->len - FDP_FRAME_TAILROOM);
}

static int fdp_nci_i2c_write(void *phy_id, struct sk_buff *skb)
{
	struct fdp_i2c_phy *phy = phy_id;
	struct i2c_client *client = phy->i2c_dev;
	int r;

	if (phy->hard_fault != 0)
		return phy->hard_fault;

	fdp_nci_i2c_add_len_lrc(skb);
	fdp_nci_i2c_dump_skb(&client->dev, "fdp_wr", skb);

	r = i2c_master_send(client, skb->data, skb->len);
	if (r == -EREMOTEIO) {  /* Retry, chip was in standby */
		usleep_range(1000, 4000);
		r = i2c_master_send(client, skb->data, skb->len);
	}

	if (r < 0 || r != skb->len)
		dev_dbg(&client->dev, "%s: error err=%d len=%d\n",
			__func__, r, skb->len);

	if (r >= 0) {
		if (r != skb->len) {
			phy->hard_fault = r;
			r = -EREMOTEIO;
		} else {
			r = 0;
		}
	}

	fdp_nci_i2c_remove_len_lrc(skb);

	return r;
}

static struct nfc_phy_ops i2c_phy_ops = {
	.write = fdp_nci_i2c_write,
	.enable = fdp_nci_i2c_enable,
	.disable = fdp_nci_i2c_disable,
};

static int fdp_nci_i2c_read(struct fdp_i2c_phy *phy, struct sk_buff **skb)
{
	int r, len;
	u8 tmp[FDP_NCI_I2C_MAX_PAYLOAD], lrc, k;
	u16 i;
	struct i2c_client *client = phy->i2c_dev;

	*skb = NULL;

	/* Read the length packet and the data packet */
	for (k = 0; k < 2; k++) {

		len = phy->next_read_size;

		r = i2c_master_recv(client, tmp, len);
		if (r != len) {
			dev_dbg(&client->dev, "%s: i2c recv err: %d\n",
				__func__, r);
			goto flush;
		}

		/* Check packet integruty */
		for (lrc = i = 0; i < r; i++)
			lrc ^= tmp[i];

		/*
		 * LRC check failed. This may due to transmission error or
		 * desynchronization between driver and FDP. Drop the paquet
		 * and force resynchronization
		 */
		if (lrc) {
			dev_dbg(&client->dev, "%s: corrupted packet\n",
				__func__);
			phy->next_read_size = 5;
			goto flush;
		}

		/* Packet that contains a length */
		if (tmp[0] == 0 && tmp[1] == 0) {
			phy->next_read_size = (tmp[2] << 8) + tmp[3] + 3;
		} else {
			phy->next_read_size = FDP_NCI_I2C_MIN_PAYLOAD;

			*skb = alloc_skb(len, GFP_KERNEL);
			if (*skb == NULL) {
				r = -ENOMEM;
				goto flush;
			}

			skb_put_data(*skb, tmp, len);
			fdp_nci_i2c_dump_skb(&client->dev, "fdp_rd", *skb);

			fdp_nci_i2c_remove_len_lrc(*skb);
		}
	}

	return 0;

flush:
	/* Flush the remaining data */
	if (i2c_master_recv(client, tmp, sizeof(tmp)) < 0)
		r = -EREMOTEIO;

	return r;
}

static irqreturn_t fdp_nci_i2c_irq_thread_fn(int irq, void *phy_id)
{
	struct fdp_i2c_phy *phy = phy_id;
	struct i2c_client *client;
	struct sk_buff *skb;
	int r;

	if (!phy || irq != phy->i2c_dev->irq) {
		WARN_ON_ONCE(1);
		return IRQ_NONE;
	}

	client = phy->i2c_dev;
	dev_dbg(&client->dev, "%s\n", __func__);

	r = fdp_nci_i2c_read(phy, &skb);

	if (r == -EREMOTEIO)
		return IRQ_HANDLED;
	else if (r == -ENOMEM || r == -EBADMSG)
		return IRQ_HANDLED;

	if (skb != NULL)
		fdp_nci_recv_frame(phy->ndev, skb);

	return IRQ_HANDLED;
}

static void fdp_nci_i2c_read_device_properties(struct device *dev,
					       u8 *clock_type, u32 *clock_freq,
					       u8 **fw_vsc_cfg)
{
	int r;
	u8 len;

	r = device_property_read_u8(dev, FDP_DP_CLOCK_TYPE_NAME, clock_type);
	if (r) {
		dev_dbg(dev, "Using default clock type");
		*clock_type = 0;
	}

	r = device_property_read_u32(dev, FDP_DP_CLOCK_FREQ_NAME, clock_freq);
	if (r) {
		dev_dbg(dev, "Using default clock frequency\n");
		*clock_freq = 26000;
	}

	if (device_property_present(dev, FDP_DP_FW_VSC_CFG_NAME)) {
		r = device_property_read_u8(dev, FDP_DP_FW_VSC_CFG_NAME,
					    &len);

		if (r || len <= 0)
			goto vsc_read_err;

		/* Add 1 to the length to inclue the length byte itself */
		len++;

		*fw_vsc_cfg = devm_kmalloc_array(dev,
					   len, sizeof(**fw_vsc_cfg),
					   GFP_KERNEL);

		r = device_property_read_u8_array(dev, FDP_DP_FW_VSC_CFG_NAME,
						  *fw_vsc_cfg, len);

		if (r) {
			devm_kfree(dev, fw_vsc_cfg);
			goto vsc_read_err;
		}
	} else {
vsc_read_err:
		dev_dbg(dev, "FW vendor specific commands not present\n");
		*fw_vsc_cfg = NULL;
	}

	dev_dbg(dev, "Clock type: %d, clock frequency: %d, VSC: %s",
		*clock_type, *clock_freq, *fw_vsc_cfg != NULL ? "yes" : "no");
}

static const struct acpi_gpio_params power_gpios = { 0, 0, false };

static const struct acpi_gpio_mapping acpi_fdp_gpios[] = {
	{ "power-gpios", &power_gpios, 1 },
	{},
};

static int fdp_nci_i2c_probe(struct i2c_client *client)
{
	struct fdp_i2c_phy *phy;
	struct device *dev = &client->dev;
	u8 *fw_vsc_cfg;
	u8 clock_type;
	u32 clock_freq;
	int r = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(dev, "No I2C_FUNC_I2C support\n");
		return -ENODEV;
	}

	/* Checking if we have an irq */
	if (client->irq <= 0) {
		nfc_err(dev, "IRQ not present\n");
		return -ENODEV;
	}

	phy = devm_kzalloc(dev, sizeof(struct fdp_i2c_phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->i2c_dev = client;
	phy->next_read_size = FDP_NCI_I2C_MIN_PAYLOAD;
	i2c_set_clientdata(client, phy);

	r = devm_request_threaded_irq(dev, client->irq,
				      NULL, fdp_nci_i2c_irq_thread_fn,
				      IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				      FDP_I2C_DRIVER_NAME, phy);

	if (r < 0) {
		nfc_err(&client->dev, "Unable to register IRQ handler\n");
		return r;
	}

	r = devm_acpi_dev_add_driver_gpios(dev, acpi_fdp_gpios);
	if (r)
		dev_dbg(dev, "Unable to add GPIO mapping table\n");

	/* Requesting the power gpio */
	phy->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(phy->power_gpio)) {
		nfc_err(dev, "Power GPIO request failed\n");
		return PTR_ERR(phy->power_gpio);
	}

	/* read device properties to get the clock and production settings */
	fdp_nci_i2c_read_device_properties(dev, &clock_type, &clock_freq,
					   &fw_vsc_cfg);

	/* Call the NFC specific probe function */
	r = fdp_nci_probe(phy, &i2c_phy_ops, &phy->ndev,
			  FDP_FRAME_HEADROOM, FDP_FRAME_TAILROOM,
			  clock_type, clock_freq, fw_vsc_cfg);
	if (r < 0) {
		nfc_err(dev, "NCI probing error\n");
		return r;
	}

	dev_dbg(dev, "I2C driver loaded\n");
	return 0;
}

static int fdp_nci_i2c_remove(struct i2c_client *client)
{
	struct fdp_i2c_phy *phy = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s\n", __func__);

	fdp_nci_remove(phy->ndev);
	fdp_nci_i2c_disable(phy);

	return 0;
}

static const struct acpi_device_id fdp_nci_i2c_acpi_match[] = {
	{"INT339A", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, fdp_nci_i2c_acpi_match);

static struct i2c_driver fdp_nci_i2c_driver = {
	.driver = {
		   .name = FDP_I2C_DRIVER_NAME,
		   .acpi_match_table = ACPI_PTR(fdp_nci_i2c_acpi_match),
		  },
	.probe_new = fdp_nci_i2c_probe,
	.remove = fdp_nci_i2c_remove,
};
module_i2c_driver(fdp_nci_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C driver for Intel Fields Peak NFC controller");
MODULE_AUTHOR("Robert Dolca <robert.dolca@intel.com>");
