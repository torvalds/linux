/*
 * HID over I2C protocol implementation
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 *
 * This code is partly based on "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/mutex.h>
#include <asm/unaligned.h>

#include "../hid-ids.h"
#include "i2c-hid.h"

/* quirks to control the device */
#define I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV	BIT(0)
#define I2C_HID_QUIRK_NO_IRQ_AFTER_RESET	BIT(1)
#define I2C_HID_QUIRK_BOGUS_IRQ			BIT(4)
#define I2C_HID_QUIRK_RESET_ON_RESUME		BIT(5)
#define I2C_HID_QUIRK_BAD_INPUT_SIZE		BIT(6)
#define I2C_HID_QUIRK_NO_WAKEUP_AFTER_RESET	BIT(7)

/* Command opcodes */
#define I2C_HID_OPCODE_RESET			0x01
#define I2C_HID_OPCODE_GET_REPORT		0x02
#define I2C_HID_OPCODE_SET_REPORT		0x03
#define I2C_HID_OPCODE_GET_IDLE			0x04
#define I2C_HID_OPCODE_SET_IDLE			0x05
#define I2C_HID_OPCODE_GET_PROTOCOL		0x06
#define I2C_HID_OPCODE_SET_PROTOCOL		0x07
#define I2C_HID_OPCODE_SET_POWER		0x08

/* flags */
#define I2C_HID_STARTED		0
#define I2C_HID_RESET_PENDING	1
#define I2C_HID_READ_PENDING	2

#define I2C_HID_PWR_ON		0x00
#define I2C_HID_PWR_SLEEP	0x01

/* debug option */
static bool debug;
module_param(debug, bool, 0444);
MODULE_PARM_DESC(debug, "print a lot of debug information");

#define i2c_hid_dbg(ihid, fmt, arg...)					  \
do {									  \
	if (debug)							  \
		dev_printk(KERN_DEBUG, &(ihid)->client->dev, fmt, ##arg); \
} while (0)

struct i2c_hid_desc {
	__le16 wHIDDescLength;
	__le16 bcdVersion;
	__le16 wReportDescLength;
	__le16 wReportDescRegister;
	__le16 wInputRegister;
	__le16 wMaxInputLength;
	__le16 wOutputRegister;
	__le16 wMaxOutputLength;
	__le16 wCommandRegister;
	__le16 wDataRegister;
	__le16 wVendorID;
	__le16 wProductID;
	__le16 wVersionID;
	__le32 reserved;
} __packed;

struct i2c_hid_cmd {
	unsigned int registerIndex;
	__u8 opcode;
	unsigned int length;
};

#define I2C_HID_CMD(opcode_) \
	.opcode = opcode_, .length = 4, \
	.registerIndex = offsetof(struct i2c_hid_desc, wCommandRegister)

/* commands */
static const struct i2c_hid_cmd hid_get_report_cmd =	{ I2C_HID_CMD(0x02) };

/*
 * These definitions are not used here, but are defined by the spec.
 * Keeping them here for documentation purposes.
 *
 * static const struct i2c_hid_cmd hid_get_idle_cmd = { I2C_HID_CMD(0x04) };
 * static const struct i2c_hid_cmd hid_set_idle_cmd = { I2C_HID_CMD(0x05) };
 * static const struct i2c_hid_cmd hid_get_protocol_cmd = { I2C_HID_CMD(0x06) };
 * static const struct i2c_hid_cmd hid_set_protocol_cmd = { I2C_HID_CMD(0x07) };
 */

/* The main device structure */
struct i2c_hid {
	struct i2c_client	*client;	/* i2c client */
	struct hid_device	*hid;	/* pointer to corresponding HID dev */
	union {
		__u8 hdesc_buffer[sizeof(struct i2c_hid_desc)];
		struct i2c_hid_desc hdesc;	/* the HID Descriptor */
	};
	__le16			wHIDDescRegister; /* location of the i2c
						   * register of the HID
						   * descriptor. */
	unsigned int		bufsize;	/* i2c buffer size */
	u8			*inbuf;		/* Input buffer */
	u8			*rawbuf;	/* Raw Input buffer */
	u8			*cmdbuf;	/* Command buffer */

	unsigned long		flags;		/* device flags */
	unsigned long		quirks;		/* Various quirks */

	wait_queue_head_t	wait;		/* For waiting the interrupt */

	bool			irq_wake_enabled;
	struct mutex		reset_lock;

	struct i2chid_ops	*ops;
};

static const struct i2c_hid_quirks {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} i2c_hid_quirks[] = {
	{ USB_VENDOR_ID_WEIDA, HID_ANY_ID,
		I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV },
	{ I2C_VENDOR_ID_HANTICK, I2C_PRODUCT_ID_HANTICK_5288,
		I2C_HID_QUIRK_NO_IRQ_AFTER_RESET },
	{ I2C_VENDOR_ID_ITE, I2C_DEVICE_ID_ITE_VOYO_WINPAD_A15,
		I2C_HID_QUIRK_NO_IRQ_AFTER_RESET },
	{ I2C_VENDOR_ID_RAYDIUM, I2C_PRODUCT_ID_RAYDIUM_3118,
		I2C_HID_QUIRK_NO_IRQ_AFTER_RESET },
	{ USB_VENDOR_ID_ALPS_JP, HID_ANY_ID,
		 I2C_HID_QUIRK_RESET_ON_RESUME },
	{ I2C_VENDOR_ID_SYNAPTICS, I2C_PRODUCT_ID_SYNAPTICS_SYNA2393,
		 I2C_HID_QUIRK_RESET_ON_RESUME },
	{ USB_VENDOR_ID_ITE, I2C_DEVICE_ID_ITE_LENOVO_LEGION_Y720,
		I2C_HID_QUIRK_BAD_INPUT_SIZE },
	/*
	 * Sending the wakeup after reset actually break ELAN touchscreen controller
	 */
	{ USB_VENDOR_ID_ELAN, HID_ANY_ID,
		 I2C_HID_QUIRK_NO_WAKEUP_AFTER_RESET |
		 I2C_HID_QUIRK_BOGUS_IRQ },
	{ 0, 0 }
};

/*
 * i2c_hid_lookup_quirk: return any quirks associated with a I2C HID device
 * @idVendor: the 16-bit vendor ID
 * @idProduct: the 16-bit product ID
 *
 * Returns: a u32 quirks value.
 */
static u32 i2c_hid_lookup_quirk(const u16 idVendor, const u16 idProduct)
{
	u32 quirks = 0;
	int n;

	for (n = 0; i2c_hid_quirks[n].idVendor; n++)
		if (i2c_hid_quirks[n].idVendor == idVendor &&
		    (i2c_hid_quirks[n].idProduct == (__u16)HID_ANY_ID ||
		     i2c_hid_quirks[n].idProduct == idProduct))
			quirks = i2c_hid_quirks[n].quirks;

	return quirks;
}

static int i2c_hid_xfer(struct i2c_hid *ihid,
			u8 *send_buf, int send_len, u8 *recv_buf, int recv_len)
{
	struct i2c_client *client = ihid->client;
	struct i2c_msg msgs[2] = { 0 };
	int n = 0;
	int ret;

	if (send_len) {
		i2c_hid_dbg(ihid, "%s: cmd=%*ph\n",
			    __func__, send_len, send_buf);

		msgs[n].addr = client->addr;
		msgs[n].flags = client->flags & I2C_M_TEN;
		msgs[n].len = send_len;
		msgs[n].buf = send_buf;
		n++;
	}

	if (recv_len) {
		msgs[n].addr = client->addr;
		msgs[n].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
		msgs[n].len = recv_len;
		msgs[n].buf = recv_buf;
		n++;

		set_bit(I2C_HID_READ_PENDING, &ihid->flags);
	}

	ret = i2c_transfer(client->adapter, msgs, n);

	if (recv_len)
		clear_bit(I2C_HID_READ_PENDING, &ihid->flags);

	if (ret != n)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int i2c_hid_read_register(struct i2c_hid *ihid, __le16 reg,
				 void *buf, size_t len)
{
	*(__le16 *)ihid->cmdbuf = reg;

	return i2c_hid_xfer(ihid, ihid->cmdbuf, sizeof(__le16), buf, len);
}

static size_t i2c_hid_encode_command(u8 *buf, u8 opcode,
				     int report_type, int report_id)
{
	size_t length = 0;

	if (report_id < 0x0F) {
		buf[length++] = report_type << 4 | report_id;
		buf[length++] = opcode;
	} else {
		buf[length++] = report_type << 4 | 0x0F;
		buf[length++] = opcode;
		buf[length++] = report_id;
	}

	return length;
}

static int __i2c_hid_command(struct i2c_hid *ihid,
		const struct i2c_hid_cmd *command, u8 reportID,
		u8 reportType, u8 *args, int args_len,
		unsigned char *buf_recv, int data_len)
{
	int length = command->length;
	unsigned int registerIndex = command->registerIndex;

	ihid->cmdbuf[0] = ihid->hdesc_buffer[registerIndex];
	ihid->cmdbuf[1] = ihid->hdesc_buffer[registerIndex + 1];

	if (length > 2) {
		length = sizeof(__le16) + /* register */
			 i2c_hid_encode_command(ihid->cmdbuf + sizeof(__le16),
						command->opcode,
						reportType, reportID);
	}

	memcpy(ihid->cmdbuf + length, args, args_len);
	length += args_len;

	return i2c_hid_xfer(ihid, ihid->cmdbuf, length, buf_recv, data_len);
}

static int i2c_hid_get_report(struct i2c_hid *ihid, u8 reportType,
		u8 reportID, unsigned char *buf_recv, int data_len)
{
	u8 args[2];
	int ret;
	int args_len = 0;
	u16 readRegister = le16_to_cpu(ihid->hdesc.wDataRegister);

	i2c_hid_dbg(ihid, "%s\n", __func__);

	args[args_len++] = readRegister & 0xFF;
	args[args_len++] = readRegister >> 8;

	ret = __i2c_hid_command(ihid, &hid_get_report_cmd, reportID,
		reportType, args, args_len, buf_recv, data_len);
	if (ret) {
		dev_err(&ihid->client->dev,
			"failed to retrieve report from device.\n");
		return ret;
	}

	return 0;
}

static size_t i2c_hid_format_report(u8 *buf, int report_id,
				    const u8 *data, size_t size)
{
	size_t length = sizeof(__le16); /* reserve space to store size */

	if (report_id)
		buf[length++] = report_id;

	memcpy(buf + length, data, size);
	length += size;

	/* Store overall size in the beginning of the buffer */
	put_unaligned_le16(length, buf);

	return length;
}

/**
 * i2c_hid_set_or_send_report: forward an incoming report to the device
 * @ihid: the i2c hid device
 * @report_type: 0x03 for HID_FEATURE_REPORT ; 0x02 for HID_OUTPUT_REPORT
 * @report_id: the report ID
 * @buf: the actual data to transfer, without the report ID
 * @data_len: size of buf
 * @do_set: true: use SET_REPORT HID command, false: send plain OUTPUT report
 */
static int i2c_hid_set_or_send_report(struct i2c_hid *ihid,
				      u8 report_type, u8 report_id,
				      const u8 *buf, size_t data_len,
				      bool do_set)
{
	size_t length = 0;
	int error;

	i2c_hid_dbg(ihid, "%s\n", __func__);

	if (data_len > ihid->bufsize)
		return -EINVAL;

	if (!do_set && le16_to_cpu(ihid->hdesc.wMaxOutputLength) == 0)
		return -ENOSYS;

	if (do_set) {
		/* Command register goes first */
		*(__le16 *)ihid->cmdbuf = ihid->hdesc.wCommandRegister;
		length += sizeof(__le16);
		/* Next is SET_REPORT command */
		length += i2c_hid_encode_command(ihid->cmdbuf + length,
						 I2C_HID_OPCODE_SET_REPORT,
						 report_type, report_id);
		/*
		 * Report data will go into the data register. Because
		 * command can be either 2 or 3 bytes destination for
		 * the data register may be not aligned.
		*/
		put_unaligned_le16(le16_to_cpu(ihid->hdesc.wDataRegister),
				   ihid->cmdbuf + length);
		length += sizeof(__le16);
	} else {
		/*
		 * With simple "send report" all data goes into the output
		 * register.
		 */
		*(__le16 *)ihid->cmdbuf = ihid->hdesc.wOutputRegister;;
		length += sizeof(__le16);
	}

	length += i2c_hid_format_report(ihid->cmdbuf + length,
					report_id, buf, data_len);

	error = i2c_hid_xfer(ihid, ihid->cmdbuf, length, NULL, 0);
	if (error) {
		dev_err(&ihid->client->dev,
			"failed to set a report to device: %d\n", error);
		return error;
	}

	return data_len;
}

static int i2c_hid_set_power_command(struct i2c_hid *ihid, int power_state)
{
	size_t length;

	/* SET_POWER uses command register */
	*(__le16 *)ihid->cmdbuf = ihid->hdesc.wCommandRegister;
	length = sizeof(__le16);

	/* Now the command itself */
	length += i2c_hid_encode_command(ihid->cmdbuf + length,
					 I2C_HID_OPCODE_SET_POWER,
					 0, power_state);

	return i2c_hid_xfer(ihid, ihid->cmdbuf, length, NULL, 0);
}

static int i2c_hid_set_power(struct i2c_hid *ihid, int power_state)
{
	int ret;

	i2c_hid_dbg(ihid, "%s\n", __func__);

	/*
	 * Some devices require to send a command to wakeup before power on.
	 * The call will get a return value (EREMOTEIO) but device will be
	 * triggered and activated. After that, it goes like a normal device.
	 */
	if (power_state == I2C_HID_PWR_ON &&
	    ihid->quirks & I2C_HID_QUIRK_SET_PWR_WAKEUP_DEV) {
		ret = i2c_hid_set_power_command(ihid, I2C_HID_PWR_ON);

		/* Device was already activated */
		if (!ret)
			goto set_pwr_exit;
	}

	ret = i2c_hid_set_power_command(ihid, power_state);
	if (ret)
		dev_err(&ihid->client->dev,
			"failed to change power setting.\n");

set_pwr_exit:

	/*
	 * The HID over I2C specification states that if a DEVICE needs time
	 * after the PWR_ON request, it should utilise CLOCK stretching.
	 * However, it has been observered that the Windows driver provides a
	 * 1ms sleep between the PWR_ON and RESET requests.
	 * According to Goodix Windows even waits 60 ms after (other?)
	 * PWR_ON requests. Testing has confirmed that several devices
	 * will not work properly without a delay after a PWR_ON request.
	 */
	if (!ret && power_state == I2C_HID_PWR_ON)
		msleep(60);

	return ret;
}

static int i2c_hid_execute_reset(struct i2c_hid *ihid)
{
	size_t length = 0;
	int ret;

	i2c_hid_dbg(ihid, "resetting...\n");

	/* Prepare reset command. Command register goes first. */
	*(__le16 *)ihid->cmdbuf = ihid->hdesc.wCommandRegister;
	length += sizeof(__le16);
	/* Next is RESET command itself */
	length += i2c_hid_encode_command(ihid->cmdbuf + length,
					 I2C_HID_OPCODE_RESET, 0, 0);

	set_bit(I2C_HID_RESET_PENDING, &ihid->flags);

	ret = i2c_hid_xfer(ihid, ihid->cmdbuf, length, NULL, 0);
	if (ret) {
		dev_err(&ihid->client->dev, "failed to reset device.\n");
		goto out;
	}

	if (ihid->quirks & I2C_HID_QUIRK_NO_IRQ_AFTER_RESET) {
		msleep(100);
		goto out;
	}

	i2c_hid_dbg(ihid, "%s: waiting...\n", __func__);
	if (!wait_event_timeout(ihid->wait,
				!test_bit(I2C_HID_RESET_PENDING, &ihid->flags),
				msecs_to_jiffies(5000))) {
		ret = -ENODATA;
		goto out;
	}
	i2c_hid_dbg(ihid, "%s: finished.\n", __func__);

out:
	clear_bit(I2C_HID_RESET_PENDING, &ihid->flags);
	return ret;
}

static int i2c_hid_hwreset(struct i2c_hid *ihid)
{
	int ret;

	i2c_hid_dbg(ihid, "%s\n", __func__);

	/*
	 * This prevents sending feature reports while the device is
	 * being reset. Otherwise we may lose the reset complete
	 * interrupt.
	 */
	mutex_lock(&ihid->reset_lock);

	ret = i2c_hid_set_power(ihid, I2C_HID_PWR_ON);
	if (ret)
		goto out_unlock;

	ret = i2c_hid_execute_reset(ihid);
	if (ret) {
		dev_err(&ihid->client->dev,
			"failed to reset device: %d\n", ret);
		i2c_hid_set_power(ihid, I2C_HID_PWR_SLEEP);
		goto out_unlock;
	}

	/* At least some SIS devices need this after reset */
	if (!(ihid->quirks & I2C_HID_QUIRK_NO_WAKEUP_AFTER_RESET))
		ret = i2c_hid_set_power(ihid, I2C_HID_PWR_ON);

out_unlock:
	mutex_unlock(&ihid->reset_lock);
	return ret;
}

static void i2c_hid_get_input(struct i2c_hid *ihid)
{
	int ret;
	u32 ret_size;
	int size = le16_to_cpu(ihid->hdesc.wMaxInputLength);

	if (size > ihid->bufsize)
		size = ihid->bufsize;

	ret = i2c_master_recv(ihid->client, ihid->inbuf, size);
	if (ret != size) {
		if (ret < 0)
			return;

		dev_err(&ihid->client->dev, "%s: got %d data instead of %d\n",
			__func__, ret, size);
		return;
	}

	ret_size = ihid->inbuf[0] | ihid->inbuf[1] << 8;

	if (!ret_size) {
		/* host or device initiated RESET completed */
		if (test_and_clear_bit(I2C_HID_RESET_PENDING, &ihid->flags))
			wake_up(&ihid->wait);
		return;
	}

	if (ihid->quirks & I2C_HID_QUIRK_BOGUS_IRQ && ret_size == 0xffff) {
		dev_warn_once(&ihid->client->dev, "%s: IRQ triggered but "
			      "there's no data\n", __func__);
		return;
	}

	if ((ret_size > size) || (ret_size < 2)) {
		if (ihid->quirks & I2C_HID_QUIRK_BAD_INPUT_SIZE) {
			ihid->inbuf[0] = size & 0xff;
			ihid->inbuf[1] = size >> 8;
			ret_size = size;
		} else {
			dev_err(&ihid->client->dev, "%s: incomplete report (%d/%d)\n",
				__func__, size, ret_size);
			return;
		}
	}

	i2c_hid_dbg(ihid, "input: %*ph\n", ret_size, ihid->inbuf);

	if (test_bit(I2C_HID_STARTED, &ihid->flags)) {
		pm_wakeup_event(&ihid->client->dev, 0);

		hid_input_report(ihid->hid, HID_INPUT_REPORT, ihid->inbuf + 2,
				ret_size - 2, 1);
	}

	return;
}

static irqreturn_t i2c_hid_irq(int irq, void *dev_id)
{
	struct i2c_hid *ihid = dev_id;

	if (test_bit(I2C_HID_READ_PENDING, &ihid->flags))
		return IRQ_HANDLED;

	i2c_hid_get_input(ihid);

	return IRQ_HANDLED;
}

static int i2c_hid_get_report_length(struct hid_report *report)
{
	return ((report->size - 1) >> 3) + 1 +
		report->device->report_enum[report->type].numbered + 2;
}

/*
 * Traverse the supplied list of reports and find the longest
 */
static void i2c_hid_find_max_report(struct hid_device *hid, unsigned int type,
		unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	/* We should not rely on wMaxInputLength, as some devices may set it to
	 * a wrong length. */
	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = i2c_hid_get_report_length(report);
		if (*max < size)
			*max = size;
	}
}

static void i2c_hid_free_buffers(struct i2c_hid *ihid)
{
	kfree(ihid->inbuf);
	kfree(ihid->rawbuf);
	kfree(ihid->cmdbuf);
	ihid->inbuf = NULL;
	ihid->rawbuf = NULL;
	ihid->cmdbuf = NULL;
	ihid->bufsize = 0;
}

static int i2c_hid_alloc_buffers(struct i2c_hid *ihid, size_t report_size)
{
	/*
	 * The worst case is computed from the set_report command with a
	 * reportID > 15 and the maximum report length.
	 */
	int cmd_len = sizeof(__le16) +	/* command register */
		      sizeof(u8) +	/* encoded report type/ID */
		      sizeof(u8) +	/* opcode */
		      sizeof(u8) +	/* optional 3rd byte report ID */
		      sizeof(__le16) +	/* data register */
		      sizeof(__le16) +	/* report data size */
		      sizeof(u8) +	/* report ID if numbered report */
		      report_size;

	ihid->inbuf = kzalloc(report_size, GFP_KERNEL);
	ihid->rawbuf = kzalloc(report_size, GFP_KERNEL);
	ihid->cmdbuf = kzalloc(cmd_len, GFP_KERNEL);

	if (!ihid->inbuf || !ihid->rawbuf || !ihid->cmdbuf) {
		i2c_hid_free_buffers(ihid);
		return -ENOMEM;
	}

	ihid->bufsize = report_size;

	return 0;
}

static int i2c_hid_get_raw_report(struct hid_device *hid,
		unsigned char report_number, __u8 *buf, size_t count,
		unsigned char report_type)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	size_t ret_count, ask_count;
	int ret;

	if (report_type == HID_OUTPUT_REPORT)
		return -EINVAL;

	/*
	 * In case of unnumbered reports the response from the device will
	 * not have the report ID that the upper layers expect, so we need
	 * to stash it the buffer ourselves and adjust the data size.
	 */
	if (!report_number) {
		buf[0] = 0;
		buf++;
		count--;
	}

	/* +2 bytes to include the size of the reply in the query buffer */
	ask_count = min(count + 2, (size_t)ihid->bufsize);

	ret = i2c_hid_get_report(ihid,
			report_type == HID_FEATURE_REPORT ? 0x03 : 0x01,
			report_number, ihid->rawbuf, ask_count);

	if (ret < 0)
		return ret;

	ret_count = ihid->rawbuf[0] | (ihid->rawbuf[1] << 8);

	if (ret_count <= 2)
		return 0;

	ret_count = min(ret_count, ask_count);

	/* The query buffer contains the size, dropping it in the reply */
	count = min(count, ret_count - 2);
	memcpy(buf, ihid->rawbuf + 2, count);

	if (!report_number)
		count++;

	return count;
}

static int i2c_hid_output_raw_report(struct hid_device *hid,
				     const u8 *buf, size_t count,
				     u8 report_type, bool do_set)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int report_id = buf[0];
	int ret;

	if (report_type == HID_INPUT_REPORT)
		return -EINVAL;

	mutex_lock(&ihid->reset_lock);

	/*
	 * Note that both numbered and unnumbered reports passed here
	 * are supposed to have report ID stored in the 1st byte of the
	 * buffer, so we strip it off unconditionally before passing payload
	 * to i2c_hid_set_or_send_report which takes care of encoding
	 * everything properly.
	 */
	ret = i2c_hid_set_or_send_report(ihid,
				report_type == HID_FEATURE_REPORT ? 0x03 : 0x02,
				report_id, buf + 1, count - 1, do_set);

	if (ret >= 0)
		ret++; /* add report_id to the number of transferred bytes */

	mutex_unlock(&ihid->reset_lock);

	return ret;
}

static int i2c_hid_output_report(struct hid_device *hid, u8 *buf, size_t count)
{
	return i2c_hid_output_raw_report(hid, buf, count, HID_OUTPUT_REPORT,
					 false);
}

static int i2c_hid_raw_request(struct hid_device *hid, unsigned char reportnum,
			       __u8 *buf, size_t len, unsigned char rtype,
			       int reqtype)
{
	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		return i2c_hid_get_raw_report(hid, reportnum, buf, len, rtype);
	case HID_REQ_SET_REPORT:
		if (buf[0] != reportnum)
			return -EINVAL;
		return i2c_hid_output_raw_report(hid, buf, len, rtype, true);
	default:
		return -EIO;
	}
}

static int i2c_hid_parse(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct i2c_hid_desc *hdesc = &ihid->hdesc;
	unsigned int rsize;
	char *rdesc;
	int ret;
	int tries = 3;
	char *use_override;

	i2c_hid_dbg(ihid, "entering %s\n", __func__);

	rsize = le16_to_cpu(hdesc->wReportDescLength);
	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dbg_hid("weird size of report descriptor (%u)\n", rsize);
		return -EINVAL;
	}

	do {
		ret = i2c_hid_hwreset(ihid);
		if (ret)
			msleep(1000);
	} while (tries-- > 0 && ret);

	if (ret)
		return ret;

	use_override = i2c_hid_get_dmi_hid_report_desc_override(client->name,
								&rsize);

	if (use_override) {
		rdesc = use_override;
		i2c_hid_dbg(ihid, "Using a HID report descriptor override\n");
	} else {
		rdesc = kzalloc(rsize, GFP_KERNEL);

		if (!rdesc) {
			dbg_hid("couldn't allocate rdesc memory\n");
			return -ENOMEM;
		}

		i2c_hid_dbg(ihid, "asking HID report descriptor\n");

		ret = i2c_hid_read_register(ihid,
					    ihid->hdesc.wReportDescRegister,
					    rdesc, rsize);
		if (ret) {
			hid_err(hid, "reading report descriptor failed\n");
			kfree(rdesc);
			return -EIO;
		}
	}

	i2c_hid_dbg(ihid, "Report Descriptor: %*ph\n", rsize, rdesc);

	ret = hid_parse_report(hid, rdesc, rsize);
	if (!use_override)
		kfree(rdesc);

	if (ret) {
		dbg_hid("parsing report descriptor failed\n");
		return ret;
	}

	return 0;
}

static int i2c_hid_start(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	int ret;
	unsigned int bufsize = HID_MIN_BUFFER_SIZE;

	i2c_hid_find_max_report(hid, HID_INPUT_REPORT, &bufsize);
	i2c_hid_find_max_report(hid, HID_OUTPUT_REPORT, &bufsize);
	i2c_hid_find_max_report(hid, HID_FEATURE_REPORT, &bufsize);

	if (bufsize > ihid->bufsize) {
		disable_irq(client->irq);
		i2c_hid_free_buffers(ihid);

		ret = i2c_hid_alloc_buffers(ihid, bufsize);
		enable_irq(client->irq);

		if (ret)
			return ret;
	}

	return 0;
}

static void i2c_hid_stop(struct hid_device *hid)
{
	hid->claimed = 0;
}

static int i2c_hid_open(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);

	set_bit(I2C_HID_STARTED, &ihid->flags);
	return 0;
}

static void i2c_hid_close(struct hid_device *hid)
{
	struct i2c_client *client = hid->driver_data;
	struct i2c_hid *ihid = i2c_get_clientdata(client);

	clear_bit(I2C_HID_STARTED, &ihid->flags);
}

struct hid_ll_driver i2c_hid_ll_driver = {
	.parse = i2c_hid_parse,
	.start = i2c_hid_start,
	.stop = i2c_hid_stop,
	.open = i2c_hid_open,
	.close = i2c_hid_close,
	.output_report = i2c_hid_output_report,
	.raw_request = i2c_hid_raw_request,
};
EXPORT_SYMBOL_GPL(i2c_hid_ll_driver);

static int i2c_hid_init_irq(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	unsigned long irqflags = 0;
	int ret;

	dev_dbg(&client->dev, "Requesting IRQ: %d\n", client->irq);

	if (!irq_get_trigger_type(client->irq))
		irqflags = IRQF_TRIGGER_LOW;

	ret = request_threaded_irq(client->irq, NULL, i2c_hid_irq,
				   irqflags | IRQF_ONESHOT, client->name, ihid);
	if (ret < 0) {
		dev_warn(&client->dev,
			"Could not register for %s interrupt, irq = %d,"
			" ret = %d\n",
			client->name, client->irq, ret);

		return ret;
	}

	return 0;
}

static int i2c_hid_fetch_hid_descriptor(struct i2c_hid *ihid)
{
	struct i2c_client *client = ihid->client;
	struct i2c_hid_desc *hdesc = &ihid->hdesc;
	unsigned int dsize;
	int error;

	/* i2c hid fetch using a fixed descriptor size (30 bytes) */
	if (i2c_hid_get_dmi_i2c_hid_desc_override(client->name)) {
		i2c_hid_dbg(ihid, "Using a HID descriptor override\n");
		ihid->hdesc =
			*i2c_hid_get_dmi_i2c_hid_desc_override(client->name);
	} else {
		i2c_hid_dbg(ihid, "Fetching the HID descriptor\n");
		error = i2c_hid_read_register(ihid,
					      ihid->wHIDDescRegister,
					      &ihid->hdesc,
					      sizeof(ihid->hdesc));
		if (error) {
			dev_err(&ihid->client->dev,
				"failed to fetch HID descriptor: %d\n",
				error);
			return -ENODEV;
		}
	}

	/* Validate the length of HID descriptor, the 4 first bytes:
	 * bytes 0-1 -> length
	 * bytes 2-3 -> bcdVersion (has to be 1.00) */
	/* check bcdVersion == 1.0 */
	if (le16_to_cpu(hdesc->bcdVersion) != 0x0100) {
		dev_err(&ihid->client->dev,
			"unexpected HID descriptor bcdVersion (0x%04hx)\n",
			le16_to_cpu(hdesc->bcdVersion));
		return -ENODEV;
	}

	/* Descriptor length should be 30 bytes as per the specification */
	dsize = le16_to_cpu(hdesc->wHIDDescLength);
	if (dsize != sizeof(struct i2c_hid_desc)) {
		dev_err(&ihid->client->dev,
			"weird size of HID descriptor (%u)\n", dsize);
		return -ENODEV;
	}
	i2c_hid_dbg(ihid, "HID Descriptor: %*ph\n", dsize, ihid->hdesc_buffer);
	return 0;
}

static int i2c_hid_core_power_up(struct i2c_hid *ihid)
{
	if (!ihid->ops->power_up)
		return 0;

	return ihid->ops->power_up(ihid->ops);
}

static void i2c_hid_core_power_down(struct i2c_hid *ihid)
{
	if (!ihid->ops->power_down)
		return;

	ihid->ops->power_down(ihid->ops);
}

static void i2c_hid_core_shutdown_tail(struct i2c_hid *ihid)
{
	if (!ihid->ops->shutdown_tail)
		return;

	ihid->ops->shutdown_tail(ihid->ops);
}

int i2c_hid_core_probe(struct i2c_client *client, struct i2chid_ops *ops,
		       u16 hid_descriptor_address, u32 quirks)
{
	int ret;
	struct i2c_hid *ihid;
	struct hid_device *hid;

	dbg_hid("HID probe called for i2c 0x%02x\n", client->addr);

	if (!client->irq) {
		dev_err(&client->dev,
			"HID over i2c has not been provided an Int IRQ\n");
		return -EINVAL;
	}

	if (client->irq < 0) {
		if (client->irq != -EPROBE_DEFER)
			dev_err(&client->dev,
				"HID over i2c doesn't have a valid IRQ\n");
		return client->irq;
	}

	ihid = devm_kzalloc(&client->dev, sizeof(*ihid), GFP_KERNEL);
	if (!ihid)
		return -ENOMEM;

	ihid->ops = ops;

	ret = i2c_hid_core_power_up(ihid);
	if (ret)
		return ret;

	i2c_set_clientdata(client, ihid);

	ihid->client = client;

	ihid->wHIDDescRegister = cpu_to_le16(hid_descriptor_address);

	init_waitqueue_head(&ihid->wait);
	mutex_init(&ihid->reset_lock);

	/* we need to allocate the command buffer without knowing the maximum
	 * size of the reports. Let's use HID_MIN_BUFFER_SIZE, then we do the
	 * real computation later. */
	ret = i2c_hid_alloc_buffers(ihid, HID_MIN_BUFFER_SIZE);
	if (ret < 0)
		goto err_powered;

	device_enable_async_suspend(&client->dev);

	/* Make sure there is something at this address */
	ret = i2c_smbus_read_byte(client);
	if (ret < 0) {
		dev_dbg(&client->dev, "nothing at this address: %d\n", ret);
		ret = -ENXIO;
		goto err_powered;
	}

	ret = i2c_hid_fetch_hid_descriptor(ihid);
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to fetch the HID Descriptor\n");
		goto err_powered;
	}

	ret = i2c_hid_init_irq(client);
	if (ret < 0)
		goto err_powered;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto err_irq;
	}

	ihid->hid = hid;

	hid->driver_data = client;
	hid->ll_driver = &i2c_hid_ll_driver;
	hid->dev.parent = &client->dev;
	hid->bus = BUS_I2C;
	hid->version = le16_to_cpu(ihid->hdesc.bcdVersion);
	hid->vendor = le16_to_cpu(ihid->hdesc.wVendorID);
	hid->product = le16_to_cpu(ihid->hdesc.wProductID);

	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X",
		 client->name, (u16)hid->vendor, (u16)hid->product);
	strlcpy(hid->phys, dev_name(&client->dev), sizeof(hid->phys));

	ihid->quirks = i2c_hid_lookup_quirk(hid->vendor, hid->product);

	ret = hid_add_device(hid);
	if (ret) {
		if (ret != -ENODEV)
			hid_err(client, "can't add hid device: %d\n", ret);
		goto err_mem_free;
	}

	hid->quirks |= quirks;

	return 0;

err_mem_free:
	hid_destroy_device(hid);

err_irq:
	free_irq(client->irq, ihid);

err_powered:
	i2c_hid_core_power_down(ihid);
	i2c_hid_free_buffers(ihid);
	return ret;
}
EXPORT_SYMBOL_GPL(i2c_hid_core_probe);

int i2c_hid_core_remove(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct hid_device *hid;

	hid = ihid->hid;
	hid_destroy_device(hid);

	free_irq(client->irq, ihid);

	if (ihid->bufsize)
		i2c_hid_free_buffers(ihid);

	i2c_hid_core_power_down(ihid);

	return 0;
}
EXPORT_SYMBOL_GPL(i2c_hid_core_remove);

void i2c_hid_core_shutdown(struct i2c_client *client)
{
	struct i2c_hid *ihid = i2c_get_clientdata(client);

	i2c_hid_set_power(ihid, I2C_HID_PWR_SLEEP);
	free_irq(client->irq, ihid);

	i2c_hid_core_shutdown_tail(ihid);
}
EXPORT_SYMBOL_GPL(i2c_hid_core_shutdown);

#ifdef CONFIG_PM_SLEEP
static int i2c_hid_core_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct hid_device *hid = ihid->hid;
	int ret;
	int wake_status;

	ret = hid_driver_suspend(hid, PMSG_SUSPEND);
	if (ret < 0)
		return ret;

	/* Save some power */
	i2c_hid_set_power(ihid, I2C_HID_PWR_SLEEP);

	disable_irq(client->irq);

	if (device_may_wakeup(&client->dev)) {
		wake_status = enable_irq_wake(client->irq);
		if (!wake_status)
			ihid->irq_wake_enabled = true;
		else
			hid_warn(hid, "Failed to enable irq wake: %d\n",
				wake_status);
	} else {
		i2c_hid_core_power_down(ihid);
	}

	return 0;
}

static int i2c_hid_core_resume(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_hid *ihid = i2c_get_clientdata(client);
	struct hid_device *hid = ihid->hid;
	int wake_status;

	if (!device_may_wakeup(&client->dev)) {
		i2c_hid_core_power_up(ihid);
	} else if (ihid->irq_wake_enabled) {
		wake_status = disable_irq_wake(client->irq);
		if (!wake_status)
			ihid->irq_wake_enabled = false;
		else
			hid_warn(hid, "Failed to disable irq wake: %d\n",
				wake_status);
	}

	enable_irq(client->irq);

	/* Instead of resetting device, simply powers the device on. This
	 * solves "incomplete reports" on Raydium devices 2386:3118 and
	 * 2386:4B33 and fixes various SIS touchscreens no longer sending
	 * data after a suspend/resume.
	 *
	 * However some ALPS touchpads generate IRQ storm without reset, so
	 * let's still reset them here.
	 */
	if (ihid->quirks & I2C_HID_QUIRK_RESET_ON_RESUME)
		ret = i2c_hid_hwreset(ihid);
	else
		ret = i2c_hid_set_power(ihid, I2C_HID_PWR_ON);

	if (ret)
		return ret;

	return hid_driver_reset_resume(hid);
}
#endif

const struct dev_pm_ops i2c_hid_core_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(i2c_hid_core_suspend, i2c_hid_core_resume)
};
EXPORT_SYMBOL_GPL(i2c_hid_core_pm);

MODULE_DESCRIPTION("HID over I2C core driver");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_LICENSE("GPL");
