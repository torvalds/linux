/*
 * I2C Link Layer for ST21NFCA HCI based Driver
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crc-ccitt.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/nfc.h>
#include <linux/firmware.h>
#include <linux/platform_data/st21nfca.h>
#include <asm/unaligned.h>

#include <net/nfc/hci.h>
#include <net/nfc/llc.h>
#include <net/nfc/nfc.h>

#include "st21nfca.h"

/*
 * Every frame starts with ST21NFCA_SOF_EOF and ends with ST21NFCA_SOF_EOF.
 * Because ST21NFCA_SOF_EOF is a possible data value, there is a mecanism
 * called byte stuffing has been introduced.
 *
 * if byte == ST21NFCA_SOF_EOF or ST21NFCA_ESCAPE_BYTE_STUFFING
 * - insert ST21NFCA_ESCAPE_BYTE_STUFFING (escape byte)
 * - xor byte with ST21NFCA_BYTE_STUFFING_MASK
 */
#define ST21NFCA_SOF_EOF		0x7e
#define ST21NFCA_BYTE_STUFFING_MASK	0x20
#define ST21NFCA_ESCAPE_BYTE_STUFFING	0x7d

/* SOF + 00 */
#define ST21NFCA_FRAME_HEADROOM			2

/* 2 bytes crc + EOF */
#define ST21NFCA_FRAME_TAILROOM 3
#define IS_START_OF_FRAME(buf) (buf[0] == ST21NFCA_SOF_EOF && \
				buf[1] == 0)

#define ST21NFCA_HCI_I2C_DRIVER_NAME "st21nfca_hci_i2c"

static struct i2c_device_id st21nfca_hci_i2c_id_table[] = {
	{ST21NFCA_HCI_DRIVER_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, st21nfca_hci_i2c_id_table);

struct st21nfca_i2c_phy {
	struct i2c_client *i2c_dev;
	struct nfc_hci_dev *hdev;

	unsigned int gpio_ena;
	unsigned int irq_polarity;

	struct st21nfca_se_status se_status;

	struct sk_buff *pending_skb;
	int current_read_len;
	/*
	 * crc might have fail because i2c macro
	 * is disable due to other interface activity
	 */
	int crc_trials;

	int powered;
	int run_mode;

	/*
	 * < 0 if hardware error occured (e.g. i2c err)
	 * and prevents normal operation.
	 */
	int hard_fault;
	struct mutex phy_lock;
};
static u8 len_seq[] = { 16, 24, 12, 29 };
static u16 wait_tab[] = { 2, 3, 5, 15, 20, 40};

#define I2C_DUMP_SKB(info, skb)					\
do {								\
	pr_debug("%s:\n", info);				\
	print_hex_dump(KERN_DEBUG, "i2c: ", DUMP_PREFIX_OFFSET,	\
		       16, 1, (skb)->data, (skb)->len, 0);	\
} while (0)

/*
 * In order to get the CLF in a known state we generate an internal reboot
 * using a proprietary command.
 * Once the reboot is completed, we expect to receive a ST21NFCA_SOF_EOF
 * fill buffer.
 */
static int st21nfca_hci_platform_init(struct st21nfca_i2c_phy *phy)
{
	u16 wait_reboot[] = { 50, 300, 1000 };
	char reboot_cmd[] = { 0x7E, 0x66, 0x48, 0xF6, 0x7E };
	u8 tmp[ST21NFCA_HCI_LLC_MAX_SIZE];
	int i, r = -1;

	for (i = 0; i < ARRAY_SIZE(wait_reboot) && r < 0; i++) {
		r = i2c_master_send(phy->i2c_dev, reboot_cmd,
				    sizeof(reboot_cmd));
		if (r < 0)
			msleep(wait_reboot[i]);
	}
	if (r < 0)
		return r;

	/* CLF is spending about 20ms to do an internal reboot */
	msleep(20);
	r = -1;
	for (i = 0; i < ARRAY_SIZE(wait_reboot) && r < 0; i++) {
		r = i2c_master_recv(phy->i2c_dev, tmp,
				    ST21NFCA_HCI_LLC_MAX_SIZE);
		if (r < 0)
			msleep(wait_reboot[i]);
	}
	if (r < 0)
		return r;

	for (i = 0; i < ST21NFCA_HCI_LLC_MAX_SIZE &&
		tmp[i] == ST21NFCA_SOF_EOF; i++)
		;

	if (r != ST21NFCA_HCI_LLC_MAX_SIZE)
		return -ENODEV;

	usleep_range(1000, 1500);
	return 0;
}

static int st21nfca_hci_i2c_enable(void *phy_id)
{
	struct st21nfca_i2c_phy *phy = phy_id;

	gpio_set_value(phy->gpio_ena, 1);
	phy->powered = 1;
	phy->run_mode = ST21NFCA_HCI_MODE;

	usleep_range(10000, 15000);

	return 0;
}

static void st21nfca_hci_i2c_disable(void *phy_id)
{
	struct st21nfca_i2c_phy *phy = phy_id;

	pr_info("\n");
	gpio_set_value(phy->gpio_ena, 0);

	phy->powered = 0;
}

static void st21nfca_hci_add_len_crc(struct sk_buff *skb)
{
	u16 crc;
	u8 tmp;

	*skb_push(skb, 1) = 0;

	crc = crc_ccitt(0xffff, skb->data, skb->len);
	crc = ~crc;

	tmp = crc & 0x00ff;
	*skb_put(skb, 1) = tmp;

	tmp = (crc >> 8) & 0x00ff;
	*skb_put(skb, 1) = tmp;
}

static void st21nfca_hci_remove_len_crc(struct sk_buff *skb)
{
	skb_pull(skb, ST21NFCA_FRAME_HEADROOM);
	skb_trim(skb, skb->len - ST21NFCA_FRAME_TAILROOM);
}

/*
 * Writing a frame must not return the number of written bytes.
 * It must return either zero for success, or <0 for error.
 * In addition, it must not alter the skb
 */
static int st21nfca_hci_i2c_write(void *phy_id, struct sk_buff *skb)
{
	int r = -1, i, j;
	struct st21nfca_i2c_phy *phy = phy_id;
	struct i2c_client *client = phy->i2c_dev;
	u8 tmp[ST21NFCA_HCI_LLC_MAX_SIZE * 2];

	I2C_DUMP_SKB("st21nfca_hci_i2c_write", skb);


	if (phy->hard_fault != 0)
		return phy->hard_fault;

	/*
	 * Compute CRC before byte stuffing computation on frame
	 * Note st21nfca_hci_add_len_crc is doing a byte stuffing
	 * on its own value
	 */
	st21nfca_hci_add_len_crc(skb);

	/* add ST21NFCA_SOF_EOF on tail */
	*skb_put(skb, 1) = ST21NFCA_SOF_EOF;
	/* add ST21NFCA_SOF_EOF on head */
	*skb_push(skb, 1) = ST21NFCA_SOF_EOF;

	/*
	 * Compute byte stuffing
	 * if byte == ST21NFCA_SOF_EOF or ST21NFCA_ESCAPE_BYTE_STUFFING
	 * insert ST21NFCA_ESCAPE_BYTE_STUFFING (escape byte)
	 * xor byte with ST21NFCA_BYTE_STUFFING_MASK
	 */
	tmp[0] = skb->data[0];
	for (i = 1, j = 1; i < skb->len - 1; i++, j++) {
		if (skb->data[i] == ST21NFCA_SOF_EOF
		    || skb->data[i] == ST21NFCA_ESCAPE_BYTE_STUFFING) {
			tmp[j] = ST21NFCA_ESCAPE_BYTE_STUFFING;
			j++;
			tmp[j] = skb->data[i] ^ ST21NFCA_BYTE_STUFFING_MASK;
		} else {
			tmp[j] = skb->data[i];
		}
	}
	tmp[j] = skb->data[i];
	j++;

	/*
	 * Manage sleep mode
	 * Try 3 times to send data with delay between each
	 */
	mutex_lock(&phy->phy_lock);
	for (i = 0; i < ARRAY_SIZE(wait_tab) && r < 0; i++) {
		r = i2c_master_send(client, tmp, j);
		if (r < 0)
			msleep(wait_tab[i]);
	}
	mutex_unlock(&phy->phy_lock);

	if (r >= 0) {
		if (r != j)
			r = -EREMOTEIO;
		else
			r = 0;
	}

	st21nfca_hci_remove_len_crc(skb);

	return r;
}

static int get_frame_size(u8 *buf, int buflen)
{
	int len = 0;

	if (buf[len + 1] == ST21NFCA_SOF_EOF)
		return 0;

	for (len = 1; len < buflen && buf[len] != ST21NFCA_SOF_EOF; len++)
		;

	return len;
}

static int check_crc(u8 *buf, int buflen)
{
	u16 crc;

	crc = crc_ccitt(0xffff, buf, buflen - 2);
	crc = ~crc;

	if (buf[buflen - 2] != (crc & 0xff) || buf[buflen - 1] != (crc >> 8)) {
		pr_err(ST21NFCA_HCI_DRIVER_NAME
		       ": CRC error 0x%x != 0x%x 0x%x\n", crc, buf[buflen - 1],
		       buf[buflen - 2]);

		pr_info(DRIVER_DESC ": %s : BAD CRC\n", __func__);
		print_hex_dump(KERN_DEBUG, "crc: ", DUMP_PREFIX_NONE,
			       16, 2, buf, buflen, false);
		return -EPERM;
	}
	return 0;
}

/*
 * Prepare received data for upper layer.
 * Received data include byte stuffing, crc and sof/eof
 * which is not usable by hci part.
 * returns:
 * frame size without sof/eof, header and byte stuffing
 * -EBADMSG : frame was incorrect and discarded
 */
static int st21nfca_hci_i2c_repack(struct sk_buff *skb)
{
	int i, j, r, size;

	if (skb->len < 1 || (skb->len > 1 && skb->data[1] != 0))
		return -EBADMSG;

	size = get_frame_size(skb->data, skb->len);
	if (size > 0) {
		skb_trim(skb, size);
		/* remove ST21NFCA byte stuffing for upper layer */
		for (i = 1, j = 0; i < skb->len; i++) {
			if (skb->data[i + j] ==
					(u8) ST21NFCA_ESCAPE_BYTE_STUFFING) {
				skb->data[i] = skb->data[i + j + 1]
						| ST21NFCA_BYTE_STUFFING_MASK;
				i++;
				j++;
			}
			skb->data[i] = skb->data[i + j];
		}
		/* remove byte stuffing useless byte */
		skb_trim(skb, i - j);
		/* remove ST21NFCA_SOF_EOF from head */
		skb_pull(skb, 1);

		r = check_crc(skb->data, skb->len);
		if (r != 0) {
			i = 0;
			return -EBADMSG;
		}

		/* remove headbyte */
		skb_pull(skb, 1);
		/* remove crc. Byte Stuffing is already removed here */
		skb_trim(skb, skb->len - 2);
		return skb->len;
	}
	return 0;
}

/*
 * Reads an shdlc frame and returns it in a newly allocated sk_buff. Guarantees
 * that i2c bus will be flushed and that next read will start on a new frame.
 * returned skb contains only LLC header and payload.
 * returns:
 * frame size : if received frame is complete (find ST21NFCA_SOF_EOF at
 * end of read)
 * -EAGAIN : if received frame is incomplete (not find ST21NFCA_SOF_EOF
 * at end of read)
 * -EREMOTEIO : i2c read error (fatal)
 * -EBADMSG : frame was incorrect and discarded
 * (value returned from st21nfca_hci_i2c_repack)
 * -EIO : if no ST21NFCA_SOF_EOF is found after reaching
 * the read length end sequence
 */
static int st21nfca_hci_i2c_read(struct st21nfca_i2c_phy *phy,
				 struct sk_buff *skb)
{
	int r, i;
	u8 len;
	u8 buf[ST21NFCA_HCI_LLC_MAX_PAYLOAD];
	struct i2c_client *client = phy->i2c_dev;

	if (phy->current_read_len < ARRAY_SIZE(len_seq)) {
		len = len_seq[phy->current_read_len];

		/*
		 * Add retry mecanism
		 * Operation on I2C interface may fail in case of operation on
		 * RF or SWP interface
		 */
		r = 0;
		mutex_lock(&phy->phy_lock);
		for (i = 0; i < ARRAY_SIZE(wait_tab) && r <= 0; i++) {
			r = i2c_master_recv(client, buf, len);
			if (r < 0)
				msleep(wait_tab[i]);
		}
		mutex_unlock(&phy->phy_lock);

		if (r != len) {
			phy->current_read_len = 0;
			return -EREMOTEIO;
		}

		/*
		 * The first read sequence does not start with SOF.
		 * Data is corrupeted so we drop it.
		 */
		if (!phy->current_read_len && !IS_START_OF_FRAME(buf)) {
			skb_trim(skb, 0);
			phy->current_read_len = 0;
			return -EIO;
		} else if (phy->current_read_len && IS_START_OF_FRAME(buf)) {
			/*
			 * Previous frame transmission was interrupted and
			 * the frame got repeated.
			 * Received frame start with ST21NFCA_SOF_EOF + 00.
			 */
			skb_trim(skb, 0);
			phy->current_read_len = 0;
		}

		memcpy(skb_put(skb, len), buf, len);

		if (skb->data[skb->len - 1] == ST21NFCA_SOF_EOF) {
			phy->current_read_len = 0;
			return st21nfca_hci_i2c_repack(skb);
		}
		phy->current_read_len++;
		return -EAGAIN;
	}
	return -EIO;
}

/*
 * Reads an shdlc frame from the chip. This is not as straightforward as it
 * seems. The frame format is data-crc, and corruption can occur anywhere
 * while transiting on i2c bus, such that we could read an invalid data.
 * The tricky case is when we read a corrupted data or crc. We must detect
 * this here in order to determine that data can be transmitted to the hci
 * core. This is the reason why we check the crc here.
 * The CLF will repeat a frame until we send a RR on that frame.
 *
 * On ST21NFCA, IRQ goes in idle when read starts. As no size information are
 * available in the incoming data, other IRQ might come. Every IRQ will trigger
 * a read sequence with different length and will fill the current frame.
 * The reception is complete once we reach a ST21NFCA_SOF_EOF.
 */
static irqreturn_t st21nfca_hci_irq_thread_fn(int irq, void *phy_id)
{
	struct st21nfca_i2c_phy *phy = phy_id;
	struct i2c_client *client;

	int r;

	if (!phy || irq != phy->i2c_dev->irq) {
		WARN_ON_ONCE(1);
		return IRQ_NONE;
	}

	client = phy->i2c_dev;
	dev_dbg(&client->dev, "IRQ\n");

	if (phy->hard_fault != 0)
		return IRQ_HANDLED;

	r = st21nfca_hci_i2c_read(phy, phy->pending_skb);
	if (r == -EREMOTEIO) {
		phy->hard_fault = r;

		nfc_hci_recv_frame(phy->hdev, NULL);

		return IRQ_HANDLED;
	} else if (r == -EAGAIN || r == -EIO) {
		return IRQ_HANDLED;
	} else if (r == -EBADMSG && phy->crc_trials < ARRAY_SIZE(wait_tab)) {
		/*
		 * With ST21NFCA, only one interface (I2C, RF or SWP)
		 * may be active at a time.
		 * Having incorrect crc is usually due to i2c macrocell
		 * deactivation in the middle of a transmission.
		 * It may generate corrupted data on i2c.
		 * We give sometime to get i2c back.
		 * The complete frame will be repeated.
		 */
		msleep(wait_tab[phy->crc_trials]);
		phy->crc_trials++;
		phy->current_read_len = 0;
		kfree_skb(phy->pending_skb);
	} else if (r > 0) {
		/*
		 * We succeeded to read data from the CLF and
		 * data is valid.
		 * Reset counter.
		 */
		nfc_hci_recv_frame(phy->hdev, phy->pending_skb);
		phy->crc_trials = 0;
	} else {
		kfree_skb(phy->pending_skb);
	}

	phy->pending_skb = alloc_skb(ST21NFCA_HCI_LLC_MAX_SIZE * 2, GFP_KERNEL);
	if (phy->pending_skb == NULL) {
		phy->hard_fault = -ENOMEM;
		nfc_hci_recv_frame(phy->hdev, NULL);
	}

	return IRQ_HANDLED;
}

static struct nfc_phy_ops i2c_phy_ops = {
	.write = st21nfca_hci_i2c_write,
	.enable = st21nfca_hci_i2c_enable,
	.disable = st21nfca_hci_i2c_disable,
};

#ifdef CONFIG_OF
static int st21nfca_hci_i2c_of_request_resources(struct i2c_client *client)
{
	struct st21nfca_i2c_phy *phy = i2c_get_clientdata(client);
	struct device_node *pp;
	int gpio;
	int r;

	pp = client->dev.of_node;
	if (!pp)
		return -ENODEV;

	/* Get GPIO from device tree */
	gpio = of_get_named_gpio(pp, "enable-gpios", 0);
	if (gpio < 0) {
		nfc_err(&client->dev, "Failed to retrieve enable-gpios from device tree\n");
		return gpio;
	}

	/* GPIO request and configuration */
	r = devm_gpio_request_one(&client->dev, gpio, GPIOF_OUT_INIT_HIGH,
				  "clf_enable");
	if (r) {
		nfc_err(&client->dev, "Failed to request enable pin\n");
		return r;
	}

	phy->gpio_ena = gpio;

	phy->irq_polarity = irq_get_trigger_type(client->irq);

	phy->se_status.is_ese_present =
				of_property_read_bool(pp, "ese-present");
	phy->se_status.is_uicc_present =
				of_property_read_bool(pp, "uicc-present");

	return 0;
}
#else
static int st21nfca_hci_i2c_of_request_resources(struct i2c_client *client)
{
	return -ENODEV;
}
#endif

static int st21nfca_hci_i2c_request_resources(struct i2c_client *client)
{
	struct st21nfca_nfc_platform_data *pdata;
	struct st21nfca_i2c_phy *phy = i2c_get_clientdata(client);
	int r;

	pdata = client->dev.platform_data;
	if (pdata == NULL) {
		nfc_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	/* store for later use */
	phy->gpio_ena = pdata->gpio_ena;
	phy->irq_polarity = pdata->irq_polarity;

	if (phy->gpio_ena > 0) {
		r = devm_gpio_request_one(&client->dev, phy->gpio_ena,
					  GPIOF_OUT_INIT_HIGH, "clf_enable");
		if (r) {
			pr_err("%s : ena gpio_request failed\n", __FILE__);
			return r;
		}
	}

	phy->se_status.is_ese_present = pdata->is_ese_present;
	phy->se_status.is_uicc_present = pdata->is_uicc_present;

	return 0;
}

static int st21nfca_hci_i2c_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct st21nfca_i2c_phy *phy;
	struct st21nfca_nfc_platform_data *pdata;
	int r;

	dev_dbg(&client->dev, "%s\n", __func__);
	dev_dbg(&client->dev, "IRQ: %d\n", client->irq);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(&client->dev, "Need I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	phy = devm_kzalloc(&client->dev, sizeof(struct st21nfca_i2c_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->i2c_dev = client;
	phy->pending_skb = alloc_skb(ST21NFCA_HCI_LLC_MAX_SIZE * 2, GFP_KERNEL);
	if (phy->pending_skb == NULL)
		return -ENOMEM;

	phy->current_read_len = 0;
	phy->crc_trials = 0;
	mutex_init(&phy->phy_lock);
	i2c_set_clientdata(client, phy);

	pdata = client->dev.platform_data;
	if (!pdata && client->dev.of_node) {
		r = st21nfca_hci_i2c_of_request_resources(client);
		if (r) {
			nfc_err(&client->dev, "No platform data\n");
			return r;
		}
	} else if (pdata) {
		r = st21nfca_hci_i2c_request_resources(client);
		if (r) {
			nfc_err(&client->dev, "Cannot get platform resources\n");
			return r;
		}
	} else {
		nfc_err(&client->dev, "st21nfca platform resources not available\n");
		return -ENODEV;
	}

	r = st21nfca_hci_platform_init(phy);
	if (r < 0) {
		nfc_err(&client->dev, "Unable to reboot st21nfca\n");
		return r;
	}

	r = devm_request_threaded_irq(&client->dev, client->irq, NULL,
				st21nfca_hci_irq_thread_fn,
				phy->irq_polarity | IRQF_ONESHOT,
				ST21NFCA_HCI_DRIVER_NAME, phy);
	if (r < 0) {
		nfc_err(&client->dev, "Unable to register IRQ handler\n");
		return r;
	}

	return st21nfca_hci_probe(phy, &i2c_phy_ops, LLC_SHDLC_NAME,
					ST21NFCA_FRAME_HEADROOM,
					ST21NFCA_FRAME_TAILROOM,
					ST21NFCA_HCI_LLC_MAX_PAYLOAD,
					&phy->hdev,
					&phy->se_status);
}

static int st21nfca_hci_i2c_remove(struct i2c_client *client)
{
	struct st21nfca_i2c_phy *phy = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s\n", __func__);

	st21nfca_hci_remove(phy->hdev);

	if (phy->powered)
		st21nfca_hci_i2c_disable(phy);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id of_st21nfca_i2c_match[] = {
	{ .compatible = "st,st21nfca-i2c", },
	{ .compatible = "st,st21nfca_i2c", },
	{}
};
MODULE_DEVICE_TABLE(of, of_st21nfca_i2c_match);
#endif

static struct i2c_driver st21nfca_hci_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = ST21NFCA_HCI_I2C_DRIVER_NAME,
		.of_match_table = of_match_ptr(of_st21nfca_i2c_match),
	},
	.probe = st21nfca_hci_i2c_probe,
	.id_table = st21nfca_hci_i2c_id_table,
	.remove = st21nfca_hci_i2c_remove,
};

module_i2c_driver(st21nfca_hci_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
