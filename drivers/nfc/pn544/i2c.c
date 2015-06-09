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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crc-ccitt.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/acpi.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/nfc.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_data/pn544.h>
#include <asm/unaligned.h>

#include <net/nfc/hci.h>
#include <net/nfc/llc.h>
#include <net/nfc/nfc.h>

#include "pn544.h"

#define PN544_I2C_FRAME_HEADROOM 1
#define PN544_I2C_FRAME_TAILROOM 2

/* GPIO names */
#define PN544_GPIO_NAME_IRQ "pn544_irq"
#define PN544_GPIO_NAME_FW  "pn544_fw"
#define PN544_GPIO_NAME_EN  "pn544_en"

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

static const struct acpi_device_id pn544_hci_i2c_acpi_match[] = {
	{"NXP5440", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, pn544_hci_i2c_acpi_match);

#define PN544_HCI_I2C_DRIVER_NAME "pn544_hci_i2c"

/*
 * Exposed through the 4 most significant bytes
 * from the HCI SW_VERSION first byte, a.k.a.
 * SW RomLib.
 */
#define PN544_HW_VARIANT_C2 0xa
#define PN544_HW_VARIANT_C3 0xb

#define PN544_FW_CMD_RESET 0x01
#define PN544_FW_CMD_WRITE 0x08
#define PN544_FW_CMD_CHECK 0x06
#define PN544_FW_CMD_SECURE_WRITE 0x0C
#define PN544_FW_CMD_SECURE_CHUNK_WRITE 0x0D

struct pn544_i2c_fw_frame_write {
	u8 cmd;
	u16 be_length;
	u8 be_dest_addr[3];
	u16 be_datalen;
	u8 data[];
} __packed;

struct pn544_i2c_fw_frame_check {
	u8 cmd;
	u16 be_length;
	u8 be_start_addr[3];
	u16 be_datalen;
	u16 be_crc;
} __packed;

struct pn544_i2c_fw_frame_response {
	u8 status;
	u16 be_length;
} __packed;

struct pn544_i2c_fw_blob {
	u32 be_size;
	u32 be_destaddr;
	u8 data[];
};

struct pn544_i2c_fw_secure_frame {
	u8 cmd;
	u16 be_datalen;
	u8 data[];
} __packed;

struct pn544_i2c_fw_secure_blob {
	u64 header;
	u8 data[];
};

#define PN544_FW_CMD_RESULT_TIMEOUT 0x01
#define PN544_FW_CMD_RESULT_BAD_CRC 0x02
#define PN544_FW_CMD_RESULT_ACCESS_DENIED 0x08
#define PN544_FW_CMD_RESULT_PROTOCOL_ERROR 0x0B
#define PN544_FW_CMD_RESULT_INVALID_PARAMETER 0x11
#define PN544_FW_CMD_RESULT_UNSUPPORTED_COMMAND 0x13
#define PN544_FW_CMD_RESULT_INVALID_LENGTH 0x18
#define PN544_FW_CMD_RESULT_CRYPTOGRAPHIC_ERROR 0x19
#define PN544_FW_CMD_RESULT_VERSION_CONDITIONS_ERROR 0x1D
#define PN544_FW_CMD_RESULT_MEMORY_ERROR 0x20
#define PN544_FW_CMD_RESULT_CHUNK_OK 0x21
#define PN544_FW_CMD_RESULT_WRITE_FAILED 0x74
#define PN544_FW_CMD_RESULT_COMMAND_REJECTED 0xE0
#define PN544_FW_CMD_RESULT_CHUNK_ERROR 0xE6

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

#define PN544_FW_WRITE_BUFFER_MAX_LEN 0x9f7
#define PN544_FW_I2C_MAX_PAYLOAD PN544_HCI_I2C_LLC_MAX_SIZE
#define PN544_FW_I2C_WRITE_FRAME_HEADER_LEN 8
#define PN544_FW_I2C_WRITE_DATA_MAX_LEN MIN((PN544_FW_I2C_MAX_PAYLOAD -\
					 PN544_FW_I2C_WRITE_FRAME_HEADER_LEN),\
					 PN544_FW_WRITE_BUFFER_MAX_LEN)
#define PN544_FW_SECURE_CHUNK_WRITE_HEADER_LEN 3
#define PN544_FW_SECURE_CHUNK_WRITE_DATA_MAX_LEN (PN544_FW_I2C_MAX_PAYLOAD -\
			PN544_FW_SECURE_CHUNK_WRITE_HEADER_LEN)
#define PN544_FW_SECURE_FRAME_HEADER_LEN 3
#define PN544_FW_SECURE_BLOB_HEADER_LEN 8

#define FW_WORK_STATE_IDLE 1
#define FW_WORK_STATE_START 2
#define FW_WORK_STATE_WAIT_WRITE_ANSWER 3
#define FW_WORK_STATE_WAIT_CHECK_ANSWER 4
#define FW_WORK_STATE_WAIT_SECURE_WRITE_ANSWER 5

struct pn544_i2c_phy {
	struct i2c_client *i2c_dev;
	struct nfc_hci_dev *hdev;

	unsigned int gpio_en;
	unsigned int gpio_irq;
	unsigned int gpio_fw;
	unsigned int en_polarity;

	u8 hw_variant;

	struct work_struct fw_work;
	int fw_work_state;
	char firmware_name[NFC_FIRMWARE_NAME_MAXSIZE + 1];
	const struct firmware *fw;
	u32 fw_blob_dest_addr;
	size_t fw_blob_size;
	const u8 *fw_blob_data;
	size_t fw_written;
	size_t fw_size;

	int fw_cmd_result;

	int powered;
	int run_mode;

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

	nfc_info(&phy->i2c_dev->dev, "Detecting nfc_en polarity\n");

	/* Disable fw download */
	gpio_set_value_cansleep(phy->gpio_fw, 0);

	for (polarity = 0; polarity < 2; polarity++) {
		phy->en_polarity = polarity;
		retry = 3;
		while (retry--) {
			/* power off */
			gpio_set_value_cansleep(phy->gpio_en,
						!phy->en_polarity);
			usleep_range(10000, 15000);

			/* power on */
			gpio_set_value_cansleep(phy->gpio_en, phy->en_polarity);
			usleep_range(10000, 15000);

			/* send reset */
			dev_dbg(&phy->i2c_dev->dev, "Sending reset cmd\n");
			ret = i2c_master_send(phy->i2c_dev, rset_cmd, count);
			if (ret == count) {
				nfc_info(&phy->i2c_dev->dev,
					 "nfc_en polarity : active %s\n",
					 (polarity == 0 ? "low" : "high"));
				goto out;
			}
		}
	}

	nfc_err(&phy->i2c_dev->dev,
		"Could not detect nfc_en polarity, fallback to active high\n");

out:
	gpio_set_value_cansleep(phy->gpio_en, !phy->en_polarity);
}

static void pn544_hci_i2c_enable_mode(struct pn544_i2c_phy *phy, int run_mode)
{
	gpio_set_value_cansleep(phy->gpio_fw,
				run_mode == PN544_FW_MODE ? 1 : 0);
	gpio_set_value_cansleep(phy->gpio_en, phy->en_polarity);
	usleep_range(10000, 15000);

	phy->run_mode = run_mode;
}

static int pn544_hci_i2c_enable(void *phy_id)
{
	struct pn544_i2c_phy *phy = phy_id;

	pr_info("%s\n", __func__);

	pn544_hci_i2c_enable_mode(phy, PN544_HCI_MODE);

	phy->powered = 1;

	return 0;
}

static void pn544_hci_i2c_disable(void *phy_id)
{
	struct pn544_i2c_phy *phy = phy_id;

	gpio_set_value_cansleep(phy->gpio_fw, 0);
	gpio_set_value_cansleep(phy->gpio_en, !phy->en_polarity);
	usleep_range(10000, 15000);

	gpio_set_value_cansleep(phy->gpio_en, phy->en_polarity);
	usleep_range(10000, 15000);

	gpio_set_value_cansleep(phy->gpio_en, !phy->en_polarity);
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
		pr_err("CRC error 0x%x != 0x%x 0x%x\n",
		       crc, buf[len - 1], buf[len - 2]);
		pr_info("%s: BAD CRC\n", __func__);
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
		nfc_err(&client->dev, "cannot read len byte\n");
		return -EREMOTEIO;
	}

	if ((len < (PN544_HCI_I2C_LLC_MIN_SIZE - 1)) ||
	    (len > (PN544_HCI_I2C_LLC_MAX_SIZE - 1))) {
		nfc_err(&client->dev, "invalid len byte\n");
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

static int pn544_hci_i2c_fw_read_status(struct pn544_i2c_phy *phy)
{
	int r;
	struct pn544_i2c_fw_frame_response response;
	struct i2c_client *client = phy->i2c_dev;

	r = i2c_master_recv(client, (char *) &response, sizeof(response));
	if (r != sizeof(response)) {
		nfc_err(&client->dev, "cannot read fw status\n");
		return -EIO;
	}

	usleep_range(3000, 6000);

	switch (response.status) {
	case 0:
		return 0;
	case PN544_FW_CMD_RESULT_CHUNK_OK:
		return response.status;
	case PN544_FW_CMD_RESULT_TIMEOUT:
		return -ETIMEDOUT;
	case PN544_FW_CMD_RESULT_BAD_CRC:
		return -ENODATA;
	case PN544_FW_CMD_RESULT_ACCESS_DENIED:
		return -EACCES;
	case PN544_FW_CMD_RESULT_PROTOCOL_ERROR:
		return -EPROTO;
	case PN544_FW_CMD_RESULT_INVALID_PARAMETER:
		return -EINVAL;
	case PN544_FW_CMD_RESULT_UNSUPPORTED_COMMAND:
		return -ENOTSUPP;
	case PN544_FW_CMD_RESULT_INVALID_LENGTH:
		return -EBADMSG;
	case PN544_FW_CMD_RESULT_CRYPTOGRAPHIC_ERROR:
		return -ENOKEY;
	case PN544_FW_CMD_RESULT_VERSION_CONDITIONS_ERROR:
		return -EINVAL;
	case PN544_FW_CMD_RESULT_MEMORY_ERROR:
		return -ENOMEM;
	case PN544_FW_CMD_RESULT_COMMAND_REJECTED:
		return -EACCES;
	case PN544_FW_CMD_RESULT_WRITE_FAILED:
	case PN544_FW_CMD_RESULT_CHUNK_ERROR:
		return -EIO;
	default:
		return -EIO;
	}
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

	if (phy->run_mode == PN544_FW_MODE) {
		phy->fw_cmd_result = pn544_hci_i2c_fw_read_status(phy);
		schedule_work(&phy->fw_work);
	} else {
		r = pn544_hci_i2c_read(phy, &skb);
		if (r == -EREMOTEIO) {
			phy->hard_fault = r;

			nfc_hci_recv_frame(phy->hdev, NULL);

			return IRQ_HANDLED;
		} else if ((r == -ENOMEM) || (r == -EBADMSG)) {
			return IRQ_HANDLED;
		}

		nfc_hci_recv_frame(phy->hdev, skb);
	}
	return IRQ_HANDLED;
}

static struct nfc_phy_ops i2c_phy_ops = {
	.write = pn544_hci_i2c_write,
	.enable = pn544_hci_i2c_enable,
	.disable = pn544_hci_i2c_disable,
};

static int pn544_hci_i2c_fw_download(void *phy_id, const char *firmware_name,
					u8 hw_variant)
{
	struct pn544_i2c_phy *phy = phy_id;

	pr_info("Starting Firmware Download (%s)\n", firmware_name);

	strcpy(phy->firmware_name, firmware_name);

	phy->hw_variant = hw_variant;
	phy->fw_work_state = FW_WORK_STATE_START;

	schedule_work(&phy->fw_work);

	return 0;
}

static void pn544_hci_i2c_fw_work_complete(struct pn544_i2c_phy *phy,
					   int result)
{
	pr_info("Firmware Download Complete, result=%d\n", result);

	pn544_hci_i2c_disable(phy);

	phy->fw_work_state = FW_WORK_STATE_IDLE;

	if (phy->fw) {
		release_firmware(phy->fw);
		phy->fw = NULL;
	}

	nfc_fw_download_done(phy->hdev->ndev, phy->firmware_name, (u32) -result);
}

static int pn544_hci_i2c_fw_write_cmd(struct i2c_client *client, u32 dest_addr,
				      const u8 *data, u16 datalen)
{
	u8 frame[PN544_FW_I2C_MAX_PAYLOAD];
	struct pn544_i2c_fw_frame_write *framep;
	u16 params_len;
	int framelen;
	int r;

	if (datalen > PN544_FW_I2C_WRITE_DATA_MAX_LEN)
		datalen = PN544_FW_I2C_WRITE_DATA_MAX_LEN;

	framep = (struct pn544_i2c_fw_frame_write *) frame;

	params_len = sizeof(framep->be_dest_addr) +
		     sizeof(framep->be_datalen) + datalen;
	framelen = params_len + sizeof(framep->cmd) +
			     sizeof(framep->be_length);

	framep->cmd = PN544_FW_CMD_WRITE;

	put_unaligned_be16(params_len, &framep->be_length);

	framep->be_dest_addr[0] = (dest_addr & 0xff0000) >> 16;
	framep->be_dest_addr[1] = (dest_addr & 0xff00) >> 8;
	framep->be_dest_addr[2] = dest_addr & 0xff;

	put_unaligned_be16(datalen, &framep->be_datalen);

	memcpy(framep->data, data, datalen);

	r = i2c_master_send(client, frame, framelen);

	if (r == framelen)
		return datalen;
	else if (r < 0)
		return r;
	else
		return -EIO;
}

static int pn544_hci_i2c_fw_check_cmd(struct i2c_client *client, u32 start_addr,
				      const u8 *data, u16 datalen)
{
	struct pn544_i2c_fw_frame_check frame;
	int r;
	u16 crc;

	/* calculate local crc for the data we want to check */
	crc = crc_ccitt(0xffff, data, datalen);

	frame.cmd = PN544_FW_CMD_CHECK;

	put_unaligned_be16(sizeof(frame.be_start_addr) +
			   sizeof(frame.be_datalen) + sizeof(frame.be_crc),
			   &frame.be_length);

	/* tell the chip the memory region to which our crc applies */
	frame.be_start_addr[0] = (start_addr & 0xff0000) >> 16;
	frame.be_start_addr[1] = (start_addr & 0xff00) >> 8;
	frame.be_start_addr[2] = start_addr & 0xff;

	put_unaligned_be16(datalen, &frame.be_datalen);

	/*
	 * and give our local crc. Chip will calculate its own crc for the
	 * region and compare with ours.
	 */
	put_unaligned_be16(crc, &frame.be_crc);

	r = i2c_master_send(client, (const char *) &frame, sizeof(frame));

	if (r == sizeof(frame))
		return 0;
	else if (r < 0)
		return r;
	else
		return -EIO;
}

static int pn544_hci_i2c_fw_write_chunk(struct pn544_i2c_phy *phy)
{
	int r;

	r = pn544_hci_i2c_fw_write_cmd(phy->i2c_dev,
				       phy->fw_blob_dest_addr + phy->fw_written,
				       phy->fw_blob_data + phy->fw_written,
				       phy->fw_blob_size - phy->fw_written);
	if (r < 0)
		return r;

	phy->fw_written += r;
	phy->fw_work_state = FW_WORK_STATE_WAIT_WRITE_ANSWER;

	return 0;
}

static int pn544_hci_i2c_fw_secure_write_frame_cmd(struct pn544_i2c_phy *phy,
					const u8 *data, u16 datalen)
{
	u8 buf[PN544_FW_I2C_MAX_PAYLOAD];
	struct pn544_i2c_fw_secure_frame *chunk;
	int chunklen;
	int r;

	if (datalen > PN544_FW_SECURE_CHUNK_WRITE_DATA_MAX_LEN)
		datalen = PN544_FW_SECURE_CHUNK_WRITE_DATA_MAX_LEN;

	chunk = (struct pn544_i2c_fw_secure_frame *) buf;

	chunk->cmd = PN544_FW_CMD_SECURE_CHUNK_WRITE;

	put_unaligned_be16(datalen, &chunk->be_datalen);

	memcpy(chunk->data, data, datalen);

	chunklen = sizeof(chunk->cmd) + sizeof(chunk->be_datalen) + datalen;

	r = i2c_master_send(phy->i2c_dev, buf, chunklen);

	if (r == chunklen)
		return datalen;
	else if (r < 0)
		return r;
	else
		return -EIO;

}

static int pn544_hci_i2c_fw_secure_write_frame(struct pn544_i2c_phy *phy)
{
	struct pn544_i2c_fw_secure_frame *framep;
	int r;

	framep = (struct pn544_i2c_fw_secure_frame *) phy->fw_blob_data;
	if (phy->fw_written == 0)
		phy->fw_blob_size = get_unaligned_be16(&framep->be_datalen)
				+ PN544_FW_SECURE_FRAME_HEADER_LEN;

	/* Only secure write command can be chunked*/
	if (phy->fw_blob_size > PN544_FW_I2C_MAX_PAYLOAD &&
			framep->cmd != PN544_FW_CMD_SECURE_WRITE)
		return -EINVAL;

	/* The firmware also have other commands, we just send them directly */
	if (phy->fw_blob_size < PN544_FW_I2C_MAX_PAYLOAD) {
		r = i2c_master_send(phy->i2c_dev,
			(const char *) phy->fw_blob_data, phy->fw_blob_size);

		if (r == phy->fw_blob_size)
			goto exit;
		else if (r < 0)
			return r;
		else
			return -EIO;
	}

	r = pn544_hci_i2c_fw_secure_write_frame_cmd(phy,
				       phy->fw_blob_data + phy->fw_written,
				       phy->fw_blob_size - phy->fw_written);
	if (r < 0)
		return r;

exit:
	phy->fw_written += r;
	phy->fw_work_state = FW_WORK_STATE_WAIT_SECURE_WRITE_ANSWER;

	/* SW reset command will not trig any response from PN544 */
	if (framep->cmd == PN544_FW_CMD_RESET) {
		pn544_hci_i2c_enable_mode(phy, PN544_FW_MODE);
		phy->fw_cmd_result = 0;
		schedule_work(&phy->fw_work);
	}

	return 0;
}

static void pn544_hci_i2c_fw_work(struct work_struct *work)
{
	struct pn544_i2c_phy *phy = container_of(work, struct pn544_i2c_phy,
						fw_work);
	int r;
	struct pn544_i2c_fw_blob *blob;
	struct pn544_i2c_fw_secure_blob *secure_blob;

	switch (phy->fw_work_state) {
	case FW_WORK_STATE_START:
		pn544_hci_i2c_enable_mode(phy, PN544_FW_MODE);

		r = request_firmware(&phy->fw, phy->firmware_name,
				     &phy->i2c_dev->dev);
		if (r < 0)
			goto exit_state_start;

		phy->fw_written = 0;

		switch (phy->hw_variant) {
		case PN544_HW_VARIANT_C2:
			blob = (struct pn544_i2c_fw_blob *) phy->fw->data;
			phy->fw_blob_size = get_unaligned_be32(&blob->be_size);
			phy->fw_blob_dest_addr = get_unaligned_be32(
							&blob->be_destaddr);
			phy->fw_blob_data = blob->data;

			r = pn544_hci_i2c_fw_write_chunk(phy);
			break;
		case PN544_HW_VARIANT_C3:
			secure_blob = (struct pn544_i2c_fw_secure_blob *)
								phy->fw->data;
			phy->fw_blob_data = secure_blob->data;
			phy->fw_size = phy->fw->size;
			r = pn544_hci_i2c_fw_secure_write_frame(phy);
			break;
		default:
			r = -ENOTSUPP;
			break;
		}

exit_state_start:
		if (r < 0)
			pn544_hci_i2c_fw_work_complete(phy, r);
		break;

	case FW_WORK_STATE_WAIT_WRITE_ANSWER:
		r = phy->fw_cmd_result;
		if (r < 0)
			goto exit_state_wait_write_answer;

		if (phy->fw_written == phy->fw_blob_size) {
			r = pn544_hci_i2c_fw_check_cmd(phy->i2c_dev,
						       phy->fw_blob_dest_addr,
						       phy->fw_blob_data,
						       phy->fw_blob_size);
			if (r < 0)
				goto exit_state_wait_write_answer;
			phy->fw_work_state = FW_WORK_STATE_WAIT_CHECK_ANSWER;
			break;
		}

		r = pn544_hci_i2c_fw_write_chunk(phy);

exit_state_wait_write_answer:
		if (r < 0)
			pn544_hci_i2c_fw_work_complete(phy, r);
		break;

	case FW_WORK_STATE_WAIT_CHECK_ANSWER:
		r = phy->fw_cmd_result;
		if (r < 0)
			goto exit_state_wait_check_answer;

		blob = (struct pn544_i2c_fw_blob *) (phy->fw_blob_data +
		       phy->fw_blob_size);
		phy->fw_blob_size = get_unaligned_be32(&blob->be_size);
		if (phy->fw_blob_size != 0) {
			phy->fw_blob_dest_addr =
					get_unaligned_be32(&blob->be_destaddr);
			phy->fw_blob_data = blob->data;

			phy->fw_written = 0;
			r = pn544_hci_i2c_fw_write_chunk(phy);
		}

exit_state_wait_check_answer:
		if (r < 0 || phy->fw_blob_size == 0)
			pn544_hci_i2c_fw_work_complete(phy, r);
		break;

	case FW_WORK_STATE_WAIT_SECURE_WRITE_ANSWER:
		r = phy->fw_cmd_result;
		if (r < 0)
			goto exit_state_wait_secure_write_answer;

		if (r == PN544_FW_CMD_RESULT_CHUNK_OK) {
			r = pn544_hci_i2c_fw_secure_write_frame(phy);
			goto exit_state_wait_secure_write_answer;
		}

		if (phy->fw_written == phy->fw_blob_size) {
			secure_blob = (struct pn544_i2c_fw_secure_blob *)
				(phy->fw_blob_data + phy->fw_blob_size);
			phy->fw_size -= phy->fw_blob_size +
				PN544_FW_SECURE_BLOB_HEADER_LEN;
			if (phy->fw_size >= PN544_FW_SECURE_BLOB_HEADER_LEN
					+ PN544_FW_SECURE_FRAME_HEADER_LEN) {
				phy->fw_blob_data = secure_blob->data;

				phy->fw_written = 0;
				r = pn544_hci_i2c_fw_secure_write_frame(phy);
			}
		}

exit_state_wait_secure_write_answer:
		if (r < 0 || phy->fw_size == 0)
			pn544_hci_i2c_fw_work_complete(phy, r);
		break;

	default:
		break;
	}
}

static int pn544_hci_i2c_acpi_request_resources(struct i2c_client *client)
{
	struct pn544_i2c_phy *phy = i2c_get_clientdata(client);
	const struct acpi_device_id *id;
	struct gpio_desc *gpiod_en, *gpiod_irq, *gpiod_fw;
	struct device *dev;
	int ret;

	if (!client)
		return -EINVAL;

	dev = &client->dev;

	/* Match the struct device against a given list of ACPI IDs */
	id = acpi_match_device(dev->driver->acpi_match_table, dev);

	if (!id)
		return -ENODEV;

	/* Get EN GPIO from ACPI */
	gpiod_en = devm_gpiod_get_index(dev, PN544_GPIO_NAME_EN, 1);
	if (IS_ERR(gpiod_en)) {
		nfc_err(dev,
			"Unable to get EN GPIO\n");
		return -ENODEV;
	}

	phy->gpio_en  = desc_to_gpio(gpiod_en);

	/* Configuration EN GPIO */
	ret = gpiod_direction_output(gpiod_en, 0);
	if (ret) {
		nfc_err(dev, "Fail EN pin direction\n");
		return ret;
	}

	/* Get FW GPIO from ACPI */
	gpiod_fw = devm_gpiod_get_index(dev, PN544_GPIO_NAME_FW, 2);
	if (IS_ERR(gpiod_fw)) {
		nfc_err(dev,
			"Unable to get FW GPIO\n");
		return -ENODEV;
	}

	phy->gpio_fw  = desc_to_gpio(gpiod_fw);

	/* Configuration FW GPIO */
	ret = gpiod_direction_output(gpiod_fw, 0);
	if (ret) {
		nfc_err(dev, "Fail FW pin direction\n");
		return ret;
	}

	/* Get IRQ GPIO */
	gpiod_irq = devm_gpiod_get_index(dev, PN544_GPIO_NAME_IRQ, 0);
	if (IS_ERR(gpiod_irq)) {
		nfc_err(dev,
			"Unable to get IRQ GPIO\n");
		return -ENODEV;
	}

	phy->gpio_irq = desc_to_gpio(gpiod_irq);

	/* Configure IRQ GPIO */
	ret = gpiod_direction_input(gpiod_irq);
	if (ret) {
		nfc_err(dev, "Fail IRQ pin direction\n");
		return ret;
	}

	/* Map the pin to an IRQ */
	ret = gpiod_to_irq(gpiod_irq);
	if (ret < 0) {
		nfc_err(dev, "Fail pin IRQ mapping\n");
		return ret;
	}

	nfc_info(dev, "GPIO resource, no:%d irq:%d\n",
		 desc_to_gpio(gpiod_irq), ret);
	client->irq = ret;

	return 0;
}

#ifdef CONFIG_OF

static int pn544_hci_i2c_of_request_resources(struct i2c_client *client)
{
	struct pn544_i2c_phy *phy = i2c_get_clientdata(client);
	struct device_node *pp;
	int ret;

	pp = client->dev.of_node;
	if (!pp) {
		ret = -ENODEV;
		goto err_dt;
	}

	/* Obtention of EN GPIO from device tree */
	ret = of_get_named_gpio(pp, "enable-gpios", 0);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			nfc_err(&client->dev,
				"Failed to get EN gpio, error: %d\n", ret);
		goto err_dt;
	}
	phy->gpio_en = ret;

	/* Configuration of EN GPIO */
	ret = gpio_request(phy->gpio_en, PN544_GPIO_NAME_EN);
	if (ret) {
		nfc_err(&client->dev, "Fail EN pin\n");
		goto err_dt;
	}
	ret = gpio_direction_output(phy->gpio_en, 0);
	if (ret) {
		nfc_err(&client->dev, "Fail EN pin direction\n");
		goto err_gpio_en;
	}

	/* Obtention of FW GPIO from device tree */
	ret = of_get_named_gpio(pp, "firmware-gpios", 0);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			nfc_err(&client->dev,
				"Failed to get FW gpio, error: %d\n", ret);
		goto err_gpio_en;
	}
	phy->gpio_fw = ret;

	/* Configuration of FW GPIO */
	ret = gpio_request(phy->gpio_fw, PN544_GPIO_NAME_FW);
	if (ret) {
		nfc_err(&client->dev, "Fail FW pin\n");
		goto err_gpio_en;
	}
	ret = gpio_direction_output(phy->gpio_fw, 0);
	if (ret) {
		nfc_err(&client->dev, "Fail FW pin direction\n");
		goto err_gpio_fw;
	}

	/* IRQ */
	ret = irq_of_parse_and_map(pp, 0);
	if (ret < 0) {
		nfc_err(&client->dev,
			"Unable to get irq, error: %d\n", ret);
		goto err_gpio_fw;
	}
	client->irq = ret;

	return 0;

err_gpio_fw:
	gpio_free(phy->gpio_fw);
err_gpio_en:
	gpio_free(phy->gpio_en);
err_dt:
	return ret;
}

#else

static int pn544_hci_i2c_of_request_resources(struct i2c_client *client)
{
	return -ENODEV;
}

#endif

static int pn544_hci_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct pn544_i2c_phy *phy;
	struct pn544_nfc_platform_data *pdata;
	int r = 0;

	dev_dbg(&client->dev, "%s\n", __func__);
	dev_dbg(&client->dev, "IRQ: %d\n", client->irq);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(&client->dev, "Need I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	phy = devm_kzalloc(&client->dev, sizeof(struct pn544_i2c_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	INIT_WORK(&phy->fw_work, pn544_hci_i2c_fw_work);
	phy->fw_work_state = FW_WORK_STATE_IDLE;

	phy->i2c_dev = client;
	i2c_set_clientdata(client, phy);

	pdata = client->dev.platform_data;

	/* No platform data, using device tree. */
	if (!pdata && client->dev.of_node) {
		r = pn544_hci_i2c_of_request_resources(client);
		if (r) {
			nfc_err(&client->dev, "No DT data\n");
			return r;
		}
	/* Using platform data. */
	} else if (pdata) {

		if (pdata->request_resources == NULL) {
			nfc_err(&client->dev, "request_resources() missing\n");
			return -EINVAL;
		}

		r = pdata->request_resources(client);
		if (r) {
			nfc_err(&client->dev,
				"Cannot get platform resources\n");
			return r;
		}

		phy->gpio_en = pdata->get_gpio(NFC_GPIO_ENABLE);
		phy->gpio_fw = pdata->get_gpio(NFC_GPIO_FW_RESET);
		phy->gpio_irq = pdata->get_gpio(NFC_GPIO_IRQ);
	/* Using ACPI */
	} else if (ACPI_HANDLE(&client->dev)) {
		r = pn544_hci_i2c_acpi_request_resources(client);
		if (r) {
			nfc_err(&client->dev,
				"Cannot get ACPI data\n");
			return r;
		}
	} else {
		nfc_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	pn544_hci_i2c_platform_init(phy);

	r = request_threaded_irq(client->irq, NULL, pn544_hci_i2c_irq_thread_fn,
				 IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				 PN544_HCI_I2C_DRIVER_NAME, phy);
	if (r < 0) {
		nfc_err(&client->dev, "Unable to register IRQ handler\n");
		goto err_rti;
	}

	r = pn544_hci_probe(phy, &i2c_phy_ops, LLC_SHDLC_NAME,
			    PN544_I2C_FRAME_HEADROOM, PN544_I2C_FRAME_TAILROOM,
			    PN544_HCI_I2C_LLC_MAX_PAYLOAD,
			    pn544_hci_i2c_fw_download, &phy->hdev);
	if (r < 0)
		goto err_hci;

	return 0;

err_hci:
	free_irq(client->irq, phy);

err_rti:
	if (!pdata) {
		gpio_free(phy->gpio_en);
		gpio_free(phy->gpio_fw);
	} else if (pdata->free_resources) {
		pdata->free_resources();
	}

	return r;
}

static int pn544_hci_i2c_remove(struct i2c_client *client)
{
	struct pn544_i2c_phy *phy = i2c_get_clientdata(client);
	struct pn544_nfc_platform_data *pdata = client->dev.platform_data;

	dev_dbg(&client->dev, "%s\n", __func__);

	cancel_work_sync(&phy->fw_work);
	if (phy->fw_work_state != FW_WORK_STATE_IDLE)
		pn544_hci_i2c_fw_work_complete(phy, -ENODEV);

	pn544_hci_remove(phy->hdev);

	if (phy->powered)
		pn544_hci_i2c_disable(phy);

	free_irq(client->irq, phy);

	/* No platform data, GPIOs have been requested by this driver */
	if (!pdata) {
		gpio_free(phy->gpio_en);
		gpio_free(phy->gpio_fw);
	/* Using platform data */
	} else if (pdata->free_resources) {
		pdata->free_resources();
	}

	return 0;
}

static const struct of_device_id of_pn544_i2c_match[] = {
	{ .compatible = "nxp,pn544-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pn544_i2c_match);

static struct i2c_driver pn544_hci_i2c_driver = {
	.driver = {
		   .name = PN544_HCI_I2C_DRIVER_NAME,
		   .owner  = THIS_MODULE,
		   .of_match_table = of_match_ptr(of_pn544_i2c_match),
		   .acpi_match_table = ACPI_PTR(pn544_hci_i2c_acpi_match),
		  },
	.probe = pn544_hci_i2c_probe,
	.id_table = pn544_hci_i2c_id_table,
	.remove = pn544_hci_i2c_remove,
};

module_i2c_driver(pn544_hci_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
