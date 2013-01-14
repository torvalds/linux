/*
 * I2C Link Layer for PN544 HCI based Driver
 *
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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/crc-ccitt.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/platform_data/pn544.h>

#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "pn544.h"

#define PN544_I2C_FRAME_HEADROOM 1
#define PN544_I2C_FRAME_TAILROOM 2

/* framing in HCI mode */
#define PN544_HCI_I2C_LLC_LEN		1
#define PN544_HCI_I2C_LLC_CRC		2
#define PN544_HCI_I2C_LLC_LEN_CRC	(PN544_HCI_I2C_LLC_LEN + \
					 PN544_HCI_I2C_LLC_CRC)
#define PN544_HCI_I2C_LLC_MIN_SIZE	(1 + PN544_HCI_I2C_LLC_LEN_CRC)
#define PN544_HCI_I2C_LLC_MAX_PAYLOAD	29
#define PN544_HCI_I2C_LLC_MAX_SIZE	(PN544_HCI_I2C_LLC_LEN_CRC + 1 + \
					 PN544_HCI_I2C_LLC_MAX_PAYLOAD)

static struct i2c_device_id pn544_hci_i2c_id_table[] = {
	{"pn544", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, pn544_hci_i2c_id_table);

#define PN544_HCI_I2C_DRIVER_NAME "pn544_hci_i2c"

struct pn544_i2c_phy {
	struct i2c_client *i2c_dev;
	struct nfc_hci_dev *hdev;

	unsigned int gpio_en;
	unsigned int gpio_irq;
	unsigned int gpio_fw;
	unsigned int en_polarity;

	int powered;

	int hard_fault;		/*
				 * < 0 if hardware error occured (e.g. i2c err)
				 * and prevents normal operation.
				 */
};

#define I2C_DUMP_SKB(info, skb)					\
do {								\
	pr_debug("%s:\n", info);				\
	print_hex_dump(KERN_DEBUG, "i2c: ", DUMP_PREFIX_OFFSET,	\
		       16, 1, (skb)->data, (skb)->len, 0);	\
} while (0)

static void pn544_hci_i2c_platform_init(struct pn544_i2c_phy *phy)
{
	int polarity, retry, ret;
	char rset_cmd[] = { 0x05, 0xF9, 0x04, 0x00, 0xC3, 0xE5 };
	int count = sizeof(rset_cmd);

	pr_info(DRIVER_DESC ": %s\n", __func__);
	dev_info(&phy->i2c_dev->dev, "Detecting nfc_en polarity\n");

	/* Disable fw download */
	gpio_set_value(phy->gpio_fw, 0);

	for (polarity = 0; polarity < 2; polarity++) {
		phy->en_polarity = polarity;
		retry = 3;
		while (retry--) {
			/* power off */
			gpio_set_value(phy->gpio_en, !phy->en_polarity);
			usleep_range(10000, 15000);

			/* power on */
			gpio_set_value(phy->gpio_en, phy->en_polarity);
			usleep_range(10000, 15000);

			/* send reset */
			dev_dbg(&phy->i2c_dev->dev, "Sending reset cmd\n");
			ret = i2c_master_send(phy->i2c_dev, rset_cmd, count);
			if (ret == count) {
				dev_info(&phy->i2c_dev->dev,
					 "nfc_en polarity : active %s\n",
					 (polarity == 0 ? "low" : "high"));
				goto out;
			}
		}
	}

	dev_err(&phy->i2c_dev->dev,
		"Could not detect nfc_en polarity, fallback to active high\n");

out:
	gpio_set_value(phy->gpio_en, !phy->en_polarity);
}

static int pn544_hci_i2c_enable(void *phy_id)
{
	struct pn544_i2c_phy *phy = phy_id;

	pr_info(DRIVER_DESC ": %s\n", __func__);

	gpio_set_value(phy->gpio_fw, 0);
	gpio_set_value(phy->gpio_en, phy->en_polarity);
	usleep_range(10000, 15000);

	phy->powered = 1;

	return 0;
}

static void pn544_hci_i2c_disable(void *phy_id)
{
	struct pn544_i2c_phy *phy = phy_id;

	pr_info(DRIVER_DESC ": %s\n", __func__);

	gpio_set_value(phy->gpio_fw, 0);
	gpio_set_value(phy->gpio_en, !phy->en_polarity);
	usleep_range(10000, 15000);

	gpio_set_value(phy->gpio_en, phy->en_polarity);
	usleep_range(10000, 15000);

	gpio_set_value(phy->gpio_en, !phy->en_polarity);
	usleep_range(10000, 15000);

	phy->powered = 0;
}

static void pn544_hci_i2c_add_len_crc(struct sk_buff *skb)
{
	u16 crc;
	int len;

	len = skb->len + 2;
	*skb_push(skb, 1) = len;

	crc = crc_ccitt(0xffff, skb->data, skb->len);
	crc = ~crc;
	*skb_put(skb, 1) = crc & 0xff;
	*skb_put(skb, 1) = crc >> 8;
}

static void pn544_hci_i2c_remove_len_crc(struct sk_buff *skb)
{
	skb_pull(skb, PN544_I2C_FRAME_HEADROOM);
	skb_trim(skb, PN544_I2C_FRAME_TAILROOM);
}

/*
 * Writing a frame must not return the number of written bytes.
 * It must return either zero for success, or <0 for error.
 * In addition, it must not alter the skb
 */
static int pn544_hci_i2c_write(void *phy_id, struct sk_buff *skb)
{
	int r;
	struct pn544_i2c_phy *phy = phy_id;
	struct i2c_client *client = phy->i2c_dev;

	if (phy->hard_fault != 0)
		return phy->hard_fault;

	usleep_range(3000, 6000);

	pn544_hci_i2c_add_len_crc(skb);

	I2C_DUMP_SKB("i2c frame written", skb);

	r = i2c_master_send(client, skb->data, skb->len);

	if (r == -EREMOTEIO) {	/* Retry, chip was in standby */
		usleep_range(6000, 10000);
		r = i2c_master_send(client, skb->data, skb->len);
	}

	if (r >= 0) {
		if (r != skb->len)
			r = -EREMOTEIO;
		else
			r = 0;
	}

	pn544_hci_i2c_remove_len_crc(skb);

	return r;
}

static int check_crc(u8 *buf, int buflen)
{
	int len;
	u16 crc;

	len = buf[0] + 1;
	crc = crc_ccitt(0xffff, buf, len - 2);
	crc = ~crc;

	if (buf[len - 2] != (crc & 0xff) || buf[len - 1] != (crc >> 8)) {
		pr_err(PN544_HCI_I2C_DRIVER_NAME
		       ": CRC error 0x%x != 0x%x 0x%x\n",
		       crc, buf[len - 1], buf[len - 2]);

		pr_info(DRIVER_DESC ": %s : BAD CRC\n", __func__);
		print_hex_dump(KERN_DEBUG, "crc: ", DUMP_PREFIX_NONE,
			       16, 2, buf, buflen, false);
		return -EPERM;
	}
	return 0;
}

/*
 * Reads an shdlc frame and returns it in a newly allocated sk_buff. Guarantees
 * that i2c bus will be flushed and that next read will start on a new frame.
 * returned skb contains only LLC header and payload.
 * returns:
 * -EREMOTEIO : i2c read error (fatal)
 * -EBADMSG : frame was incorrect and discarded
 * -ENOMEM : cannot allocate skb, frame dropped
 */
static int pn544_hci_i2c_read(struct pn544_i2c_phy *phy, struct sk_buff **skb)
{
	int r;
	u8 len;
	u8 tmp[PN544_HCI_I2C_LLC_MAX_SIZE - 1];
	struct i2c_client *client = phy->i2c_dev;

	r = i2c_master_recv(client, &len, 1);
	if (r != 1) {
		dev_err(&client->dev, "cannot read len byte\n");
		return -EREMOTEIO;
	}

	if ((len < (PN544_HCI_I2C_LLC_MIN_SIZE - 1)) ||
	    (len > (PN544_HCI_I2C_LLC_MAX_SIZE - 1))) {
		dev_err(&client->dev, "invalid len byte\n");
		r = -EBADMSG;
		goto flush;
	}

	*skb = alloc_skb(1 + len, GFP_KERNEL);
	if (*skb == NULL) {
		r = -ENOMEM;
		goto flush;
	}

	*skb_put(*skb, 1) = len;

	r = i2c_master_recv(client, skb_put(*skb, len), len);
	if (r != len) {
		kfree_skb(*skb);
		return -EREMOTEIO;
	}

	I2C_DUMP_SKB("i2c frame read", *skb);

	r = check_crc((*skb)->data, (*skb)->len);
	if (r != 0) {
		kfree_skb(*skb);
		r = -EBADMSG;
		goto flush;
	}

	skb_pull(*skb, 1);
	skb_trim(*skb, (*skb)->len - 2);

	usleep_range(3000, 6000);

	return 0;

flush:
	if (i2c_master_recv(client, tmp, sizeof(tmp)) < 0)
		r = -EREMOTEIO;

	usleep_range(3000, 6000);

	return r;
}

/*
 * Reads an shdlc frame from the chip. This is not as straightforward as it
 * seems. There are cases where we could loose the frame start synchronization.
 * The frame format is len-data-crc, and corruption can occur anywhere while
 * transiting on i2c bus, such that we could read an invalid len.
 * In order to recover synchronization with the next frame, we must be sure
 * to read the real amount of data without using the len byte. We do this by
 * assuming the following:
 * - the chip will always present only one single complete frame on the bus
 *   before triggering the interrupt
 * - the chip will not present a new frame until we have completely read
 *   the previous one (or until we have handled the interrupt).
 * The tricky case is when we read a corrupted len that is less than the real
 * len. We must detect this here in order to determine that we need to flush
 * the bus. This is the reason why we check the crc here.
 */
static irqreturn_t pn544_hci_i2c_irq_thread_fn(int irq, void *phy_id)
{
	struct pn544_i2c_phy *phy = phy_id;
	struct i2c_client *client;
	struct sk_buff *skb = NULL;
	int r;

	if (!phy || irq != phy->i2c_dev->irq) {
		WARN_ON_ONCE(1);
		return IRQ_NONE;
	}

	client = phy->i2c_dev;
	dev_dbg(&client->dev, "IRQ\n");

	if (phy->hard_fault != 0)
		return IRQ_HANDLED;

	r = pn544_hci_i2c_read(phy, &skb);
	if (r == -EREMOTEIO) {
		phy->hard_fault = r;

		nfc_hci_recv_frame(phy->hdev, NULL);

		return IRQ_HANDLED;
	} else if ((r == -ENOMEM) || (r == -EBADMSG)) {
		return IRQ_HANDLED;
	}

	nfc_hci_recv_frame(phy->hdev, skb);

	return IRQ_HANDLED;
}

static struct nfc_phy_ops i2c_phy_ops = {
	.write = pn544_hci_i2c_write,
	.enable = pn544_hci_i2c_enable,
	.disable = pn544_hci_i2c_disable,
};

static int pn544_hci_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct pn544_i2c_phy *phy;
	struct pn544_nfc_platform_data *pdata;
	int r = 0;

	dev_dbg(&client->dev, "%s\n", __func__);
	dev_dbg(&client->dev, "IRQ: %d\n", client->irq);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Need I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	phy = kzalloc(sizeof(struct pn544_i2c_phy), GFP_KERNEL);
	if (!phy) {
		dev_err(&client->dev,
			"Cannot allocate memory for pn544 i2c phy.\n");
		r = -ENOMEM;
		goto err_phy_alloc;
	}

	phy->i2c_dev = client;
	i2c_set_clientdata(client, phy);

	pdata = client->dev.platform_data;
	if (pdata == NULL) {
		dev_err(&client->dev, "No platform data\n");
		r = -EINVAL;
		goto err_pdata;
	}

	if (pdata->request_resources == NULL) {
		dev_err(&client->dev, "request_resources() missing\n");
		r = -EINVAL;
		goto err_pdata;
	}

	r = pdata->request_resources(client);
	if (r) {
		dev_err(&client->dev, "Cannot get platform resources\n");
		goto err_pdata;
	}

	phy->gpio_en = pdata->get_gpio(NFC_GPIO_ENABLE);
	phy->gpio_fw = pdata->get_gpio(NFC_GPIO_FW_RESET);
	phy->gpio_irq = pdata->get_gpio(NFC_GPIO_IRQ);

	pn544_hci_i2c_platform_init(phy);

	r = request_threaded_irq(client->irq, NULL, pn544_hci_i2c_irq_thread_fn,
				 IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				 PN544_HCI_I2C_DRIVER_NAME, phy);
	if (r < 0) {
		dev_err(&client->dev, "Unable to register IRQ handler\n");
		goto err_rti;
	}

	r = pn544_hci_probe(phy, &i2c_phy_ops, LLC_SHDLC_NAME,
			    PN544_I2C_FRAME_HEADROOM, PN544_I2C_FRAME_TAILROOM,
			    PN544_HCI_I2C_LLC_MAX_PAYLOAD, &phy->hdev);
	if (r < 0)
		goto err_hci;

	return 0;

err_hci:
	free_irq(client->irq, phy);

err_rti:
	if (pdata->free_resources != NULL)
		pdata->free_resources();

err_pdata:
	kfree(phy);

err_phy_alloc:
	return r;
}

static int pn544_hci_i2c_remove(struct i2c_client *client)
{
	struct pn544_i2c_phy *phy = i2c_get_clientdata(client);
	struct pn544_nfc_platform_data *pdata = client->dev.platform_data;

	dev_dbg(&client->dev, "%s\n", __func__);

	pn544_hci_remove(phy->hdev);

	if (phy->powered)
		pn544_hci_i2c_disable(phy);

	free_irq(client->irq, phy);
	if (pdata->free_resources)
		pdata->free_resources();

	kfree(phy);

	return 0;
}

static struct i2c_driver pn544_hci_i2c_driver = {
	.driver = {
		   .name = PN544_HCI_I2C_DRIVER_NAME,
		  },
	.probe = pn544_hci_i2c_probe,
	.id_table = pn544_hci_i2c_id_table,
	.remove = pn544_hci_i2c_remove,
};

static int __init pn544_hci_i2c_init(void)
{
	int r;

	pr_debug(DRIVER_DESC ": %s\n", __func__);

	r = i2c_add_driver(&pn544_hci_i2c_driver);
	if (r) {
		pr_err(PN544_HCI_I2C_DRIVER_NAME
		       ": driver registration failed\n");
		return r;
	}

	return 0;
}

static void __exit pn544_hci_i2c_exit(void)
{
	i2c_del_driver(&pn544_hci_i2c_driver);
}

module_init(pn544_hci_i2c_init);
module_exit(pn544_hci_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
