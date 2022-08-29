// SPDX-License-Identifier: GPL-2.0-only
/*
 * hid-cp2112.c - Silicon Labs HID USB to SMBus master bridge
 * Copyright (c) 2013,2014 Uplogix, Inc.
 * David Barksdale <dbarksdale@uplogix.com>
 */

/*
 * The Silicon Labs CP2112 chip is a USB HID device which provides an
 * SMBus controller for talking to slave devices and 8 GPIO pins. The
 * host communicates with the CP2112 via raw HID reports.
 *
 * Data Sheet:
 *   https://www.silabs.com/Support%20Documents/TechnicalDocs/CP2112.pdf
 * Programming Interface Specification:
 *   https://www.silabs.com/documents/public/application-notes/an495-cp2112-interface-specification.pdf
 */

#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/driver.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/nls.h>
#include <linux/usb/ch9.h>
#include "hid-ids.h"

#define CP2112_REPORT_MAX_LENGTH		64
#define CP2112_GPIO_CONFIG_LENGTH		5
#define CP2112_GPIO_GET_LENGTH			2
#define CP2112_GPIO_SET_LENGTH			3

enum {
	CP2112_GPIO_CONFIG		= 0x02,
	CP2112_GPIO_GET			= 0x03,
	CP2112_GPIO_SET			= 0x04,
	CP2112_GET_VERSION_INFO		= 0x05,
	CP2112_SMBUS_CONFIG		= 0x06,
	CP2112_DATA_READ_REQUEST	= 0x10,
	CP2112_DATA_WRITE_READ_REQUEST	= 0x11,
	CP2112_DATA_READ_FORCE_SEND	= 0x12,
	CP2112_DATA_READ_RESPONSE	= 0x13,
	CP2112_DATA_WRITE_REQUEST	= 0x14,
	CP2112_TRANSFER_STATUS_REQUEST	= 0x15,
	CP2112_TRANSFER_STATUS_RESPONSE	= 0x16,
	CP2112_CANCEL_TRANSFER		= 0x17,
	CP2112_LOCK_BYTE		= 0x20,
	CP2112_USB_CONFIG		= 0x21,
	CP2112_MANUFACTURER_STRING	= 0x22,
	CP2112_PRODUCT_STRING		= 0x23,
	CP2112_SERIAL_STRING		= 0x24,
};

enum {
	STATUS0_IDLE		= 0x00,
	STATUS0_BUSY		= 0x01,
	STATUS0_COMPLETE	= 0x02,
	STATUS0_ERROR		= 0x03,
};

enum {
	STATUS1_TIMEOUT_NACK		= 0x00,
	STATUS1_TIMEOUT_BUS		= 0x01,
	STATUS1_ARBITRATION_LOST	= 0x02,
	STATUS1_READ_INCOMPLETE		= 0x03,
	STATUS1_WRITE_INCOMPLETE	= 0x04,
	STATUS1_SUCCESS			= 0x05,
};

struct cp2112_smbus_config_report {
	u8 report;		/* CP2112_SMBUS_CONFIG */
	__be32 clock_speed;	/* Hz */
	u8 device_address;	/* Stored in the upper 7 bits */
	u8 auto_send_read;	/* 1 = enabled, 0 = disabled */
	__be16 write_timeout;	/* ms, 0 = no timeout */
	__be16 read_timeout;	/* ms, 0 = no timeout */
	u8 scl_low_timeout;	/* 1 = enabled, 0 = disabled */
	__be16 retry_time;	/* # of retries, 0 = no limit */
} __packed;

struct cp2112_usb_config_report {
	u8 report;	/* CP2112_USB_CONFIG */
	__le16 vid;	/* Vendor ID */
	__le16 pid;	/* Product ID */
	u8 max_power;	/* Power requested in 2mA units */
	u8 power_mode;	/* 0x00 = bus powered
			   0x01 = self powered & regulator off
			   0x02 = self powered & regulator on */
	u8 release_major;
	u8 release_minor;
	u8 mask;	/* What fields to program */
} __packed;

struct cp2112_read_req_report {
	u8 report;	/* CP2112_DATA_READ_REQUEST */
	u8 slave_address;
	__be16 length;
} __packed;

struct cp2112_write_read_req_report {
	u8 report;	/* CP2112_DATA_WRITE_READ_REQUEST */
	u8 slave_address;
	__be16 length;
	u8 target_address_length;
	u8 target_address[16];
} __packed;

struct cp2112_write_req_report {
	u8 report;	/* CP2112_DATA_WRITE_REQUEST */
	u8 slave_address;
	u8 length;
	u8 data[61];
} __packed;

struct cp2112_force_read_report {
	u8 report;	/* CP2112_DATA_READ_FORCE_SEND */
	__be16 length;
} __packed;

struct cp2112_xfer_status_report {
	u8 report;	/* CP2112_TRANSFER_STATUS_RESPONSE */
	u8 status0;	/* STATUS0_* */
	u8 status1;	/* STATUS1_* */
	__be16 retries;
	__be16 length;
} __packed;

struct cp2112_string_report {
	u8 dummy;		/* force .string to be aligned */
	u8 report;		/* CP2112_*_STRING */
	u8 length;		/* length in bytes of everyting after .report */
	u8 type;		/* USB_DT_STRING */
	wchar_t string[30];	/* UTF16_LITTLE_ENDIAN string */
} __packed;

/* Number of times to request transfer status before giving up waiting for a
   transfer to complete. This may need to be changed if SMBUS clock, retries,
   or read/write/scl_low timeout settings are changed. */
static const int XFER_STATUS_RETRIES = 10;

/* Time in ms to wait for a CP2112_DATA_READ_RESPONSE or
   CP2112_TRANSFER_STATUS_RESPONSE. */
static const int RESPONSE_TIMEOUT = 50;

static const struct hid_device_id cp2112_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CYGNAL, USB_DEVICE_ID_CYGNAL_CP2112) },
	{ }
};
MODULE_DEVICE_TABLE(hid, cp2112_devices);

struct cp2112_device {
	struct i2c_adapter adap;
	struct hid_device *hdev;
	wait_queue_head_t wait;
	u8 read_data[61];
	u8 read_length;
	u8 hwversion;
	int xfer_status;
	atomic_t read_avail;
	atomic_t xfer_avail;
	struct gpio_chip gc;
	struct irq_chip irq;
	u8 *in_out_buffer;
	struct mutex lock;

	struct gpio_desc *desc[8];
	bool gpio_poll;
	struct delayed_work gpio_poll_worker;
	unsigned long irq_mask;
	u8 gpio_prev_state;
};

static int gpio_push_pull = 0xFF;
module_param(gpio_push_pull, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gpio_push_pull, "GPIO push-pull configuration bitmask");

static int cp2112_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct cp2112_device *dev = gpiochip_get_data(chip);
	struct hid_device *hdev = dev->hdev;
	u8 *buf = dev->in_out_buffer;
	int ret;

	mutex_lock(&dev->lock);

	ret = hid_hw_raw_request(hdev, CP2112_GPIO_CONFIG, buf,
				 CP2112_GPIO_CONFIG_LENGTH, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret != CP2112_GPIO_CONFIG_LENGTH) {
		hid_err(hdev, "error requesting GPIO config: %d\n", ret);
		if (ret >= 0)
			ret = -EIO;
		goto exit;
	}

	buf[1] &= ~(1 << offset);
	buf[2] = gpio_push_pull;

	ret = hid_hw_raw_request(hdev, CP2112_GPIO_CONFIG, buf,
				 CP2112_GPIO_CONFIG_LENGTH, HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret != CP2112_GPIO_CONFIG_LENGTH) {
		hid_err(hdev, "error setting GPIO config: %d\n", ret);
		if (ret >= 0)
			ret = -EIO;
		goto exit;
	}

	ret = 0;

exit:
	mutex_unlock(&dev->lock);
	return ret;
}

static void cp2112_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct cp2112_device *dev = gpiochip_get_data(chip);
	struct hid_device *hdev = dev->hdev;
	u8 *buf = dev->in_out_buffer;
	int ret;

	mutex_lock(&dev->lock);

	buf[0] = CP2112_GPIO_SET;
	buf[1] = value ? 0xff : 0;
	buf[2] = 1 << offset;

	ret = hid_hw_raw_request(hdev, CP2112_GPIO_SET, buf,
				 CP2112_GPIO_SET_LENGTH, HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret < 0)
		hid_err(hdev, "error setting GPIO values: %d\n", ret);

	mutex_unlock(&dev->lock);
}

static int cp2112_gpio_get_all(struct gpio_chip *chip)
{
	struct cp2112_device *dev = gpiochip_get_data(chip);
	struct hid_device *hdev = dev->hdev;
	u8 *buf = dev->in_out_buffer;
	int ret;

	mutex_lock(&dev->lock);

	ret = hid_hw_raw_request(hdev, CP2112_GPIO_GET, buf,
				 CP2112_GPIO_GET_LENGTH, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret != CP2112_GPIO_GET_LENGTH) {
		hid_err(hdev, "error requesting GPIO values: %d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto exit;
	}

	ret = buf[1];

exit:
	mutex_unlock(&dev->lock);

	return ret;
}

static int cp2112_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	int ret;

	ret = cp2112_gpio_get_all(chip);
	if (ret < 0)
		return ret;

	return (ret >> offset) & 1;
}

static int cp2112_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct cp2112_device *dev = gpiochip_get_data(chip);
	struct hid_device *hdev = dev->hdev;
	u8 *buf = dev->in_out_buffer;
	int ret;

	mutex_lock(&dev->lock);

	ret = hid_hw_raw_request(hdev, CP2112_GPIO_CONFIG, buf,
				 CP2112_GPIO_CONFIG_LENGTH, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret != CP2112_GPIO_CONFIG_LENGTH) {
		hid_err(hdev, "error requesting GPIO config: %d\n", ret);
		goto fail;
	}

	buf[1] |= 1 << offset;
	buf[2] = gpio_push_pull;

	ret = hid_hw_raw_request(hdev, CP2112_GPIO_CONFIG, buf,
				 CP2112_GPIO_CONFIG_LENGTH, HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret < 0) {
		hid_err(hdev, "error setting GPIO config: %d\n", ret);
		goto fail;
	}

	mutex_unlock(&dev->lock);

	/*
	 * Set gpio value when output direction is already set,
	 * as specified in AN495, Rev. 0.2, cpt. 4.4
	 */
	cp2112_gpio_set(chip, offset, value);

	return 0;

fail:
	mutex_unlock(&dev->lock);
	return ret < 0 ? ret : -EIO;
}

static int cp2112_hid_get(struct hid_device *hdev, unsigned char report_number,
			  u8 *data, size_t count, unsigned char report_type)
{
	u8 *buf;
	int ret;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, report_number, buf, count,
				       report_type, HID_REQ_GET_REPORT);
	memcpy(data, buf, count);
	kfree(buf);
	return ret;
}

static int cp2112_hid_output(struct hid_device *hdev, u8 *data, size_t count,
			     unsigned char report_type)
{
	u8 *buf;
	int ret;

	buf = kmemdup(data, count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (report_type == HID_OUTPUT_REPORT)
		ret = hid_hw_output_report(hdev, buf, count);
	else
		ret = hid_hw_raw_request(hdev, buf[0], buf, count, report_type,
				HID_REQ_SET_REPORT);

	kfree(buf);
	return ret;
}

static int cp2112_wait(struct cp2112_device *dev, atomic_t *avail)
{
	int ret = 0;

	/* We have sent either a CP2112_TRANSFER_STATUS_REQUEST or a
	 * CP2112_DATA_READ_FORCE_SEND and we are waiting for the response to
	 * come in cp2112_raw_event or timeout. There will only be one of these
	 * in flight at any one time. The timeout is extremely large and is a
	 * last resort if the CP2112 has died. If we do timeout we don't expect
	 * to receive the response which would cause data races, it's not like
	 * we can do anything about it anyway.
	 */
	ret = wait_event_interruptible_timeout(dev->wait,
		atomic_read(avail), msecs_to_jiffies(RESPONSE_TIMEOUT));
	if (-ERESTARTSYS == ret)
		return ret;
	if (!ret)
		return -ETIMEDOUT;

	atomic_set(avail, 0);
	return 0;
}

static int cp2112_xfer_status(struct cp2112_device *dev)
{
	struct hid_device *hdev = dev->hdev;
	u8 buf[2];
	int ret;

	buf[0] = CP2112_TRANSFER_STATUS_REQUEST;
	buf[1] = 0x01;
	atomic_set(&dev->xfer_avail, 0);

	ret = cp2112_hid_output(hdev, buf, 2, HID_OUTPUT_REPORT);
	if (ret < 0) {
		hid_warn(hdev, "Error requesting status: %d\n", ret);
		return ret;
	}

	ret = cp2112_wait(dev, &dev->xfer_avail);
	if (ret)
		return ret;

	return dev->xfer_status;
}

static int cp2112_read(struct cp2112_device *dev, u8 *data, size_t size)
{
	struct hid_device *hdev = dev->hdev;
	struct cp2112_force_read_report report;
	int ret;

	if (size > sizeof(dev->read_data))
		size = sizeof(dev->read_data);
	report.report = CP2112_DATA_READ_FORCE_SEND;
	report.length = cpu_to_be16(size);

	atomic_set(&dev->read_avail, 0);

	ret = cp2112_hid_output(hdev, &report.report, sizeof(report),
				HID_OUTPUT_REPORT);
	if (ret < 0) {
		hid_warn(hdev, "Error requesting data: %d\n", ret);
		return ret;
	}

	ret = cp2112_wait(dev, &dev->read_avail);
	if (ret)
		return ret;

	hid_dbg(hdev, "read %d of %zd bytes requested\n",
		dev->read_length, size);

	if (size > dev->read_length)
		size = dev->read_length;

	memcpy(data, dev->read_data, size);
	return dev->read_length;
}

static int cp2112_read_req(void *buf, u8 slave_address, u16 length)
{
	struct cp2112_read_req_report *report = buf;

	if (length < 1 || length > 512)
		return -EINVAL;

	report->report = CP2112_DATA_READ_REQUEST;
	report->slave_address = slave_address << 1;
	report->length = cpu_to_be16(length);
	return sizeof(*report);
}

static int cp2112_write_read_req(void *buf, u8 slave_address, u16 length,
				 u8 command, u8 *data, u8 data_length)
{
	struct cp2112_write_read_req_report *report = buf;

	if (length < 1 || length > 512
	    || data_length > sizeof(report->target_address) - 1)
		return -EINVAL;

	report->report = CP2112_DATA_WRITE_READ_REQUEST;
	report->slave_address = slave_address << 1;
	report->length = cpu_to_be16(length);
	report->target_address_length = data_length + 1;
	report->target_address[0] = command;
	memcpy(&report->target_address[1], data, data_length);
	return data_length + 6;
}

static int cp2112_write_req(void *buf, u8 slave_address, u8 command, u8 *data,
			    u8 data_length)
{
	struct cp2112_write_req_report *report = buf;

	if (data_length > sizeof(report->data) - 1)
		return -EINVAL;

	report->report = CP2112_DATA_WRITE_REQUEST;
	report->slave_address = slave_address << 1;
	report->length = data_length + 1;
	report->data[0] = command;
	memcpy(&report->data[1], data, data_length);
	return data_length + 4;
}

static int cp2112_i2c_write_req(void *buf, u8 slave_address, u8 *data,
				u8 data_length)
{
	struct cp2112_write_req_report *report = buf;

	if (data_length > sizeof(report->data))
		return -EINVAL;

	report->report = CP2112_DATA_WRITE_REQUEST;
	report->slave_address = slave_address << 1;
	report->length = data_length;
	memcpy(report->data, data, data_length);
	return data_length + 3;
}

static int cp2112_i2c_write_read_req(void *buf, u8 slave_address,
				     u8 *addr, int addr_length,
				     int read_length)
{
	struct cp2112_write_read_req_report *report = buf;

	if (read_length < 1 || read_length > 512 ||
	    addr_length > sizeof(report->target_address))
		return -EINVAL;

	report->report = CP2112_DATA_WRITE_READ_REQUEST;
	report->slave_address = slave_address << 1;
	report->length = cpu_to_be16(read_length);
	report->target_address_length = addr_length;
	memcpy(report->target_address, addr, addr_length);
	return addr_length + 5;
}

static int cp2112_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct cp2112_device *dev = (struct cp2112_device *)adap->algo_data;
	struct hid_device *hdev = dev->hdev;
	u8 buf[64];
	ssize_t count;
	ssize_t read_length = 0;
	u8 *read_buf = NULL;
	unsigned int retries;
	int ret;

	hid_dbg(hdev, "I2C %d messages\n", num);

	if (num == 1) {
		if (msgs->flags & I2C_M_RD) {
			hid_dbg(hdev, "I2C read %#04x len %d\n",
				msgs->addr, msgs->len);
			read_length = msgs->len;
			read_buf = msgs->buf;
			count = cp2112_read_req(buf, msgs->addr, msgs->len);
		} else {
			hid_dbg(hdev, "I2C write %#04x len %d\n",
				msgs->addr, msgs->len);
			count = cp2112_i2c_write_req(buf, msgs->addr,
						     msgs->buf, msgs->len);
		}
		if (count < 0)
			return count;
	} else if (dev->hwversion > 1 &&  /* no repeated start in rev 1 */
		   num == 2 &&
		   msgs[0].addr == msgs[1].addr &&
		   !(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
		hid_dbg(hdev, "I2C write-read %#04x wlen %d rlen %d\n",
			msgs[0].addr, msgs[0].len, msgs[1].len);
		read_length = msgs[1].len;
		read_buf = msgs[1].buf;
		count = cp2112_i2c_write_read_req(buf, msgs[0].addr,
				msgs[0].buf, msgs[0].len, msgs[1].len);
		if (count < 0)
			return count;
	} else {
		hid_err(hdev,
			"Multi-message I2C transactions not supported\n");
		return -EOPNOTSUPP;
	}

	ret = hid_hw_power(hdev, PM_HINT_FULLON);
	if (ret < 0) {
		hid_err(hdev, "power management error: %d\n", ret);
		return ret;
	}

	ret = cp2112_hid_output(hdev, buf, count, HID_OUTPUT_REPORT);
	if (ret < 0) {
		hid_warn(hdev, "Error starting transaction: %d\n", ret);
		goto power_normal;
	}

	for (retries = 0; retries < XFER_STATUS_RETRIES; ++retries) {
		ret = cp2112_xfer_status(dev);
		if (-EBUSY == ret)
			continue;
		if (ret < 0)
			goto power_normal;
		break;
	}

	if (XFER_STATUS_RETRIES <= retries) {
		hid_warn(hdev, "Transfer timed out, cancelling.\n");
		buf[0] = CP2112_CANCEL_TRANSFER;
		buf[1] = 0x01;

		ret = cp2112_hid_output(hdev, buf, 2, HID_OUTPUT_REPORT);
		if (ret < 0)
			hid_warn(hdev, "Error cancelling transaction: %d\n",
				 ret);

		ret = -ETIMEDOUT;
		goto power_normal;
	}

	for (count = 0; count < read_length;) {
		ret = cp2112_read(dev, read_buf + count, read_length - count);
		if (ret < 0)
			goto power_normal;
		if (ret == 0) {
			hid_err(hdev, "read returned 0\n");
			ret = -EIO;
			goto power_normal;
		}
		count += ret;
		if (count > read_length) {
			/*
			 * The hardware returned too much data.
			 * This is mostly harmless because cp2112_read()
			 * has a limit check so didn't overrun our
			 * buffer.  Nevertheless, we return an error
			 * because something is seriously wrong and
			 * it shouldn't go unnoticed.
			 */
			hid_err(hdev, "long read: %d > %zd\n",
				ret, read_length - count + ret);
			ret = -EIO;
			goto power_normal;
		}
	}

	/* return the number of transferred messages */
	ret = num;

power_normal:
	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_dbg(hdev, "I2C transfer finished: %d\n", ret);
	return ret;
}

static int cp2112_xfer(struct i2c_adapter *adap, u16 addr,
		       unsigned short flags, char read_write, u8 command,
		       int size, union i2c_smbus_data *data)
{
	struct cp2112_device *dev = (struct cp2112_device *)adap->algo_data;
	struct hid_device *hdev = dev->hdev;
	u8 buf[64];
	__le16 word;
	ssize_t count;
	size_t read_length = 0;
	unsigned int retries;
	int ret;

	hid_dbg(hdev, "%s addr 0x%x flags 0x%x cmd 0x%x size %d\n",
		read_write == I2C_SMBUS_WRITE ? "write" : "read",
		addr, flags, command, size);

	switch (size) {
	case I2C_SMBUS_BYTE:
		read_length = 1;

		if (I2C_SMBUS_READ == read_write)
			count = cp2112_read_req(buf, addr, read_length);
		else
			count = cp2112_write_req(buf, addr, command, NULL,
						 0);
		break;
	case I2C_SMBUS_BYTE_DATA:
		read_length = 1;

		if (I2C_SMBUS_READ == read_write)
			count = cp2112_write_read_req(buf, addr, read_length,
						      command, NULL, 0);
		else
			count = cp2112_write_req(buf, addr, command,
						 &data->byte, 1);
		break;
	case I2C_SMBUS_WORD_DATA:
		read_length = 2;
		word = cpu_to_le16(data->word);

		if (I2C_SMBUS_READ == read_write)
			count = cp2112_write_read_req(buf, addr, read_length,
						      command, NULL, 0);
		else
			count = cp2112_write_req(buf, addr, command,
						 (u8 *)&word, 2);
		break;
	case I2C_SMBUS_PROC_CALL:
		size = I2C_SMBUS_WORD_DATA;
		read_write = I2C_SMBUS_READ;
		read_length = 2;
		word = cpu_to_le16(data->word);

		count = cp2112_write_read_req(buf, addr, read_length, command,
					      (u8 *)&word, 2);
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			read_length = data->block[0];
			count = cp2112_write_read_req(buf, addr, read_length,
						      command, NULL, 0);
		} else {
			count = cp2112_write_req(buf, addr, command,
						 data->block + 1,
						 data->block[0]);
		}
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (I2C_SMBUS_READ == read_write) {
			count = cp2112_write_read_req(buf, addr,
						      I2C_SMBUS_BLOCK_MAX,
						      command, NULL, 0);
		} else {
			count = cp2112_write_req(buf, addr, command,
						 data->block,
						 data->block[0] + 1);
		}
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		size = I2C_SMBUS_BLOCK_DATA;
		read_write = I2C_SMBUS_READ;

		count = cp2112_write_read_req(buf, addr, I2C_SMBUS_BLOCK_MAX,
					      command, data->block,
					      data->block[0] + 1);
		break;
	default:
		hid_warn(hdev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	if (count < 0)
		return count;

	ret = hid_hw_power(hdev, PM_HINT_FULLON);
	if (ret < 0) {
		hid_err(hdev, "power management error: %d\n", ret);
		return ret;
	}

	ret = cp2112_hid_output(hdev, buf, count, HID_OUTPUT_REPORT);
	if (ret < 0) {
		hid_warn(hdev, "Error starting transaction: %d\n", ret);
		goto power_normal;
	}

	for (retries = 0; retries < XFER_STATUS_RETRIES; ++retries) {
		ret = cp2112_xfer_status(dev);
		if (-EBUSY == ret)
			continue;
		if (ret < 0)
			goto power_normal;
		break;
	}

	if (XFER_STATUS_RETRIES <= retries) {
		hid_warn(hdev, "Transfer timed out, cancelling.\n");
		buf[0] = CP2112_CANCEL_TRANSFER;
		buf[1] = 0x01;

		ret = cp2112_hid_output(hdev, buf, 2, HID_OUTPUT_REPORT);
		if (ret < 0)
			hid_warn(hdev, "Error cancelling transaction: %d\n",
				 ret);

		ret = -ETIMEDOUT;
		goto power_normal;
	}

	if (I2C_SMBUS_WRITE == read_write) {
		ret = 0;
		goto power_normal;
	}

	if (I2C_SMBUS_BLOCK_DATA == size)
		read_length = ret;

	ret = cp2112_read(dev, buf, read_length);
	if (ret < 0)
		goto power_normal;
	if (ret != read_length) {
		hid_warn(hdev, "short read: %d < %zd\n", ret, read_length);
		ret = -EIO;
		goto power_normal;
	}

	switch (size) {
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
		data->byte = buf[0];
		break;
	case I2C_SMBUS_WORD_DATA:
		data->word = le16_to_cpup((__le16 *)buf);
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_length > I2C_SMBUS_BLOCK_MAX) {
			ret = -EINVAL;
			goto power_normal;
		}

		memcpy(data->block + 1, buf, read_length);
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_length > I2C_SMBUS_BLOCK_MAX) {
			ret = -EPROTO;
			goto power_normal;
		}

		memcpy(data->block, buf, read_length);
		break;
	}

	ret = 0;
power_normal:
	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_dbg(hdev, "transfer finished: %d\n", ret);
	return ret;
}

static u32 cp2112_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C |
		I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK |
		I2C_FUNC_SMBUS_PROC_CALL |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL;
}

static const struct i2c_algorithm smbus_algorithm = {
	.master_xfer	= cp2112_i2c_xfer,
	.smbus_xfer	= cp2112_xfer,
	.functionality	= cp2112_functionality,
};

static int cp2112_get_usb_config(struct hid_device *hdev,
				 struct cp2112_usb_config_report *cfg)
{
	int ret;

	ret = cp2112_hid_get(hdev, CP2112_USB_CONFIG, (u8 *)cfg, sizeof(*cfg),
			     HID_FEATURE_REPORT);
	if (ret != sizeof(*cfg)) {
		hid_err(hdev, "error reading usb config: %d\n", ret);
		if (ret < 0)
			return ret;
		return -EIO;
	}

	return 0;
}

static int cp2112_set_usb_config(struct hid_device *hdev,
				 struct cp2112_usb_config_report *cfg)
{
	int ret;

	BUG_ON(cfg->report != CP2112_USB_CONFIG);

	ret = cp2112_hid_output(hdev, (u8 *)cfg, sizeof(*cfg),
				HID_FEATURE_REPORT);
	if (ret != sizeof(*cfg)) {
		hid_err(hdev, "error writing usb config: %d\n", ret);
		if (ret < 0)
			return ret;
		return -EIO;
	}

	return 0;
}

static void chmod_sysfs_attrs(struct hid_device *hdev);

#define CP2112_CONFIG_ATTR(name, store, format, ...) \
static ssize_t name##_store(struct device *kdev, \
			    struct device_attribute *attr, const char *buf, \
			    size_t count) \
{ \
	struct hid_device *hdev = to_hid_device(kdev); \
	struct cp2112_usb_config_report cfg; \
	int ret = cp2112_get_usb_config(hdev, &cfg); \
	if (ret) \
		return ret; \
	store; \
	ret = cp2112_set_usb_config(hdev, &cfg); \
	if (ret) \
		return ret; \
	chmod_sysfs_attrs(hdev); \
	return count; \
} \
static ssize_t name##_show(struct device *kdev, \
			   struct device_attribute *attr, char *buf) \
{ \
	struct hid_device *hdev = to_hid_device(kdev); \
	struct cp2112_usb_config_report cfg; \
	int ret = cp2112_get_usb_config(hdev, &cfg); \
	if (ret) \
		return ret; \
	return scnprintf(buf, PAGE_SIZE, format, ##__VA_ARGS__); \
} \
static DEVICE_ATTR_RW(name);

CP2112_CONFIG_ATTR(vendor_id, ({
	u16 vid;

	if (sscanf(buf, "%hi", &vid) != 1)
		return -EINVAL;

	cfg.vid = cpu_to_le16(vid);
	cfg.mask = 0x01;
}), "0x%04x\n", le16_to_cpu(cfg.vid));

CP2112_CONFIG_ATTR(product_id, ({
	u16 pid;

	if (sscanf(buf, "%hi", &pid) != 1)
		return -EINVAL;

	cfg.pid = cpu_to_le16(pid);
	cfg.mask = 0x02;
}), "0x%04x\n", le16_to_cpu(cfg.pid));

CP2112_CONFIG_ATTR(max_power, ({
	int mA;

	if (sscanf(buf, "%i", &mA) != 1)
		return -EINVAL;

	cfg.max_power = (mA + 1) / 2;
	cfg.mask = 0x04;
}), "%u mA\n", cfg.max_power * 2);

CP2112_CONFIG_ATTR(power_mode, ({
	if (sscanf(buf, "%hhi", &cfg.power_mode) != 1)
		return -EINVAL;

	cfg.mask = 0x08;
}), "%u\n", cfg.power_mode);

CP2112_CONFIG_ATTR(release_version, ({
	if (sscanf(buf, "%hhi.%hhi", &cfg.release_major, &cfg.release_minor)
	    != 2)
		return -EINVAL;

	cfg.mask = 0x10;
}), "%u.%u\n", cfg.release_major, cfg.release_minor);

#undef CP2112_CONFIG_ATTR

struct cp2112_pstring_attribute {
	struct device_attribute attr;
	unsigned char report;
};

static ssize_t pstr_store(struct device *kdev,
			  struct device_attribute *kattr, const char *buf,
			  size_t count)
{
	struct hid_device *hdev = to_hid_device(kdev);
	struct cp2112_pstring_attribute *attr =
		container_of(kattr, struct cp2112_pstring_attribute, attr);
	struct cp2112_string_report report;
	int ret;

	memset(&report, 0, sizeof(report));

	ret = utf8s_to_utf16s(buf, count, UTF16_LITTLE_ENDIAN,
			      report.string, ARRAY_SIZE(report.string));
	report.report = attr->report;
	report.length = ret * sizeof(report.string[0]) + 2;
	report.type = USB_DT_STRING;

	ret = cp2112_hid_output(hdev, &report.report, report.length + 1,
				HID_FEATURE_REPORT);
	if (ret != report.length + 1) {
		hid_err(hdev, "error writing %s string: %d\n", kattr->attr.name,
			ret);
		if (ret < 0)
			return ret;
		return -EIO;
	}

	chmod_sysfs_attrs(hdev);
	return count;
}

static ssize_t pstr_show(struct device *kdev,
			 struct device_attribute *kattr, char *buf)
{
	struct hid_device *hdev = to_hid_device(kdev);
	struct cp2112_pstring_attribute *attr =
		container_of(kattr, struct cp2112_pstring_attribute, attr);
	struct cp2112_string_report report;
	u8 length;
	int ret;

	ret = cp2112_hid_get(hdev, attr->report, &report.report,
			     sizeof(report) - 1, HID_FEATURE_REPORT);
	if (ret < 3) {
		hid_err(hdev, "error reading %s string: %d\n", kattr->attr.name,
			ret);
		if (ret < 0)
			return ret;
		return -EIO;
	}

	if (report.length < 2) {
		hid_err(hdev, "invalid %s string length: %d\n",
			kattr->attr.name, report.length);
		return -EIO;
	}

	length = report.length > ret - 1 ? ret - 1 : report.length;
	length = (length - 2) / sizeof(report.string[0]);
	ret = utf16s_to_utf8s(report.string, length, UTF16_LITTLE_ENDIAN, buf,
			      PAGE_SIZE - 1);
	buf[ret++] = '\n';
	return ret;
}

#define CP2112_PSTR_ATTR(name, _report) \
static struct cp2112_pstring_attribute dev_attr_##name = { \
	.attr = __ATTR(name, (S_IWUSR | S_IRUGO), pstr_show, pstr_store), \
	.report = _report, \
};

CP2112_PSTR_ATTR(manufacturer,	CP2112_MANUFACTURER_STRING);
CP2112_PSTR_ATTR(product,	CP2112_PRODUCT_STRING);
CP2112_PSTR_ATTR(serial,	CP2112_SERIAL_STRING);

#undef CP2112_PSTR_ATTR

static const struct attribute_group cp2112_attr_group = {
	.attrs = (struct attribute *[]){
		&dev_attr_vendor_id.attr,
		&dev_attr_product_id.attr,
		&dev_attr_max_power.attr,
		&dev_attr_power_mode.attr,
		&dev_attr_release_version.attr,
		&dev_attr_manufacturer.attr.attr,
		&dev_attr_product.attr.attr,
		&dev_attr_serial.attr.attr,
		NULL
	}
};

/* Chmoding our sysfs attributes is simply a way to expose which fields in the
 * PROM have already been programmed. We do not depend on this preventing
 * writing to these attributes since the CP2112 will simply ignore writes to
 * already-programmed fields. This is why there is no sense in fixing this
 * racy behaviour.
 */
static void chmod_sysfs_attrs(struct hid_device *hdev)
{
	struct attribute **attr;
	u8 buf[2];
	int ret;

	ret = cp2112_hid_get(hdev, CP2112_LOCK_BYTE, buf, sizeof(buf),
			     HID_FEATURE_REPORT);
	if (ret != sizeof(buf)) {
		hid_err(hdev, "error reading lock byte: %d\n", ret);
		return;
	}

	for (attr = cp2112_attr_group.attrs; *attr; ++attr) {
		umode_t mode = (buf[1] & 1) ? S_IWUSR | S_IRUGO : S_IRUGO;
		ret = sysfs_chmod_file(&hdev->dev.kobj, *attr, mode);
		if (ret < 0)
			hid_err(hdev, "error chmoding sysfs file %s\n",
				(*attr)->name);
		buf[1] >>= 1;
	}
}

static void cp2112_gpio_irq_ack(struct irq_data *d)
{
}

static void cp2112_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cp2112_device *dev = gpiochip_get_data(gc);

	__clear_bit(d->hwirq, &dev->irq_mask);
}

static void cp2112_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cp2112_device *dev = gpiochip_get_data(gc);

	__set_bit(d->hwirq, &dev->irq_mask);
}

static void cp2112_gpio_poll_callback(struct work_struct *work)
{
	struct cp2112_device *dev = container_of(work, struct cp2112_device,
						 gpio_poll_worker.work);
	struct irq_data *d;
	u8 gpio_mask;
	u8 virqs = (u8)dev->irq_mask;
	u32 irq_type;
	int irq, virq, ret;

	ret = cp2112_gpio_get_all(&dev->gc);
	if (ret == -ENODEV) /* the hardware has been disconnected */
		return;
	if (ret < 0)
		goto exit;

	gpio_mask = ret;

	while (virqs) {
		virq = ffs(virqs) - 1;
		virqs &= ~BIT(virq);

		if (!dev->gc.to_irq)
			break;

		irq = dev->gc.to_irq(&dev->gc, virq);

		d = irq_get_irq_data(irq);
		if (!d)
			continue;

		irq_type = irqd_get_trigger_type(d);

		if (gpio_mask & BIT(virq)) {
			/* Level High */

			if (irq_type & IRQ_TYPE_LEVEL_HIGH)
				handle_nested_irq(irq);

			if ((irq_type & IRQ_TYPE_EDGE_RISING) &&
			    !(dev->gpio_prev_state & BIT(virq)))
				handle_nested_irq(irq);
		} else {
			/* Level Low */

			if (irq_type & IRQ_TYPE_LEVEL_LOW)
				handle_nested_irq(irq);

			if ((irq_type & IRQ_TYPE_EDGE_FALLING) &&
			    (dev->gpio_prev_state & BIT(virq)))
				handle_nested_irq(irq);
		}
	}

	dev->gpio_prev_state = gpio_mask;

exit:
	if (dev->gpio_poll)
		schedule_delayed_work(&dev->gpio_poll_worker, 10);
}


static unsigned int cp2112_gpio_irq_startup(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cp2112_device *dev = gpiochip_get_data(gc);

	INIT_DELAYED_WORK(&dev->gpio_poll_worker, cp2112_gpio_poll_callback);

	if (!dev->gpio_poll) {
		dev->gpio_poll = true;
		schedule_delayed_work(&dev->gpio_poll_worker, 0);
	}

	cp2112_gpio_irq_unmask(d);
	return 0;
}

static void cp2112_gpio_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cp2112_device *dev = gpiochip_get_data(gc);

	cancel_delayed_work_sync(&dev->gpio_poll_worker);
}

static int cp2112_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	return 0;
}

static int __maybe_unused cp2112_allocate_irq(struct cp2112_device *dev,
					      int pin)
{
	int ret;

	if (dev->desc[pin])
		return -EINVAL;

	dev->desc[pin] = gpiochip_request_own_desc(&dev->gc, pin,
						   "HID/I2C:Event",
						   GPIO_ACTIVE_HIGH,
						   GPIOD_IN);
	if (IS_ERR(dev->desc[pin])) {
		dev_err(dev->gc.parent, "Failed to request GPIO\n");
		return PTR_ERR(dev->desc[pin]);
	}

	ret = cp2112_gpio_direction_input(&dev->gc, pin);
	if (ret < 0) {
		dev_err(dev->gc.parent, "Failed to set GPIO to input dir\n");
		goto err_desc;
	}

	ret = gpiochip_lock_as_irq(&dev->gc, pin);
	if (ret) {
		dev_err(dev->gc.parent, "Failed to lock GPIO as interrupt\n");
		goto err_desc;
	}

	ret = gpiod_to_irq(dev->desc[pin]);
	if (ret < 0) {
		dev_err(dev->gc.parent, "Failed to translate GPIO to IRQ\n");
		goto err_lock;
	}

	return ret;

err_lock:
	gpiochip_unlock_as_irq(&dev->gc, pin);
err_desc:
	gpiochip_free_own_desc(dev->desc[pin]);
	dev->desc[pin] = NULL;
	return ret;
}

static int cp2112_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct cp2112_device *dev;
	u8 buf[3];
	struct cp2112_smbus_config_report config;
	struct gpio_irq_chip *girq;
	int ret;

	dev = devm_kzalloc(&hdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->in_out_buffer = devm_kzalloc(&hdev->dev, CP2112_REPORT_MAX_LENGTH,
					  GFP_KERNEL);
	if (!dev->in_out_buffer)
		return -ENOMEM;

	mutex_init(&dev->lock);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "hw open failed\n");
		goto err_hid_stop;
	}

	ret = hid_hw_power(hdev, PM_HINT_FULLON);
	if (ret < 0) {
		hid_err(hdev, "power management error: %d\n", ret);
		goto err_hid_close;
	}

	ret = cp2112_hid_get(hdev, CP2112_GET_VERSION_INFO, buf, sizeof(buf),
			     HID_FEATURE_REPORT);
	if (ret != sizeof(buf)) {
		hid_err(hdev, "error requesting version\n");
		if (ret >= 0)
			ret = -EIO;
		goto err_power_normal;
	}

	hid_info(hdev, "Part Number: 0x%02X Device Version: 0x%02X\n",
		 buf[1], buf[2]);

	ret = cp2112_hid_get(hdev, CP2112_SMBUS_CONFIG, (u8 *)&config,
			     sizeof(config), HID_FEATURE_REPORT);
	if (ret != sizeof(config)) {
		hid_err(hdev, "error requesting SMBus config\n");
		if (ret >= 0)
			ret = -EIO;
		goto err_power_normal;
	}

	config.retry_time = cpu_to_be16(1);

	ret = cp2112_hid_output(hdev, (u8 *)&config, sizeof(config),
				HID_FEATURE_REPORT);
	if (ret != sizeof(config)) {
		hid_err(hdev, "error setting SMBus config\n");
		if (ret >= 0)
			ret = -EIO;
		goto err_power_normal;
	}

	hid_set_drvdata(hdev, (void *)dev);
	dev->hdev		= hdev;
	dev->adap.owner		= THIS_MODULE;
	dev->adap.class		= I2C_CLASS_HWMON;
	dev->adap.algo		= &smbus_algorithm;
	dev->adap.algo_data	= dev;
	dev->adap.dev.parent	= &hdev->dev;
	snprintf(dev->adap.name, sizeof(dev->adap.name),
		 "CP2112 SMBus Bridge on hidraw%d",
		 ((struct hidraw *)hdev->hidraw)->minor);
	dev->hwversion = buf[2];
	init_waitqueue_head(&dev->wait);

	hid_device_io_start(hdev);
	ret = i2c_add_adapter(&dev->adap);
	hid_device_io_stop(hdev);

	if (ret) {
		hid_err(hdev, "error registering i2c adapter\n");
		goto err_power_normal;
	}

	hid_dbg(hdev, "adapter registered\n");

	dev->gc.label			= "cp2112_gpio";
	dev->gc.direction_input		= cp2112_gpio_direction_input;
	dev->gc.direction_output	= cp2112_gpio_direction_output;
	dev->gc.set			= cp2112_gpio_set;
	dev->gc.get			= cp2112_gpio_get;
	dev->gc.base			= -1;
	dev->gc.ngpio			= 8;
	dev->gc.can_sleep		= 1;
	dev->gc.parent			= &hdev->dev;

	dev->irq.name = "cp2112-gpio";
	dev->irq.irq_startup = cp2112_gpio_irq_startup;
	dev->irq.irq_shutdown = cp2112_gpio_irq_shutdown;
	dev->irq.irq_ack = cp2112_gpio_irq_ack;
	dev->irq.irq_mask = cp2112_gpio_irq_mask;
	dev->irq.irq_unmask = cp2112_gpio_irq_unmask;
	dev->irq.irq_set_type = cp2112_gpio_irq_type;
	dev->irq.flags = IRQCHIP_MASK_ON_SUSPEND;

	girq = &dev->gc.irq;
	girq->chip = &dev->irq;
	/* The event comes from the outside so no parent handler */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;

	ret = gpiochip_add_data(&dev->gc, dev);
	if (ret < 0) {
		hid_err(hdev, "error registering gpio chip\n");
		goto err_free_i2c;
	}

	ret = sysfs_create_group(&hdev->dev.kobj, &cp2112_attr_group);
	if (ret < 0) {
		hid_err(hdev, "error creating sysfs attrs\n");
		goto err_gpiochip_remove;
	}

	chmod_sysfs_attrs(hdev);
	hid_hw_power(hdev, PM_HINT_NORMAL);

	return ret;

err_gpiochip_remove:
	gpiochip_remove(&dev->gc);
err_free_i2c:
	i2c_del_adapter(&dev->adap);
err_power_normal:
	hid_hw_power(hdev, PM_HINT_NORMAL);
err_hid_close:
	hid_hw_close(hdev);
err_hid_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void cp2112_remove(struct hid_device *hdev)
{
	struct cp2112_device *dev = hid_get_drvdata(hdev);
	int i;

	sysfs_remove_group(&hdev->dev.kobj, &cp2112_attr_group);
	i2c_del_adapter(&dev->adap);

	if (dev->gpio_poll) {
		dev->gpio_poll = false;
		cancel_delayed_work_sync(&dev->gpio_poll_worker);
	}

	for (i = 0; i < ARRAY_SIZE(dev->desc); i++) {
		gpiochip_unlock_as_irq(&dev->gc, i);
		gpiochip_free_own_desc(dev->desc[i]);
	}

	gpiochip_remove(&dev->gc);
	/* i2c_del_adapter has finished removing all i2c devices from our
	 * adapter. Well behaved devices should no longer call our cp2112_xfer
	 * and should have waited for any pending calls to finish. It has also
	 * waited for device_unregister(&adap->dev) to complete. Therefore we
	 * can safely free our struct cp2112_device.
	 */
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int cp2112_raw_event(struct hid_device *hdev, struct hid_report *report,
			    u8 *data, int size)
{
	struct cp2112_device *dev = hid_get_drvdata(hdev);
	struct cp2112_xfer_status_report *xfer = (void *)data;

	switch (data[0]) {
	case CP2112_TRANSFER_STATUS_RESPONSE:
		hid_dbg(hdev, "xfer status: %02x %02x %04x %04x\n",
			xfer->status0, xfer->status1,
			be16_to_cpu(xfer->retries), be16_to_cpu(xfer->length));

		switch (xfer->status0) {
		case STATUS0_IDLE:
			dev->xfer_status = -EAGAIN;
			break;
		case STATUS0_BUSY:
			dev->xfer_status = -EBUSY;
			break;
		case STATUS0_COMPLETE:
			dev->xfer_status = be16_to_cpu(xfer->length);
			break;
		case STATUS0_ERROR:
			switch (xfer->status1) {
			case STATUS1_TIMEOUT_NACK:
			case STATUS1_TIMEOUT_BUS:
				dev->xfer_status = -ETIMEDOUT;
				break;
			default:
				dev->xfer_status = -EIO;
				break;
			}
			break;
		default:
			dev->xfer_status = -EINVAL;
			break;
		}

		atomic_set(&dev->xfer_avail, 1);
		break;
	case CP2112_DATA_READ_RESPONSE:
		hid_dbg(hdev, "read response: %02x %02x\n", data[1], data[2]);

		dev->read_length = data[2];
		if (dev->read_length > sizeof(dev->read_data))
			dev->read_length = sizeof(dev->read_data);

		memcpy(dev->read_data, &data[3], dev->read_length);
		atomic_set(&dev->read_avail, 1);
		break;
	default:
		hid_err(hdev, "unknown report\n");

		return 0;
	}

	wake_up_interruptible(&dev->wait);
	return 1;
}

static struct hid_driver cp2112_driver = {
	.name		= "cp2112",
	.id_table	= cp2112_devices,
	.probe		= cp2112_probe,
	.remove		= cp2112_remove,
	.raw_event	= cp2112_raw_event,
};

module_hid_driver(cp2112_driver);
MODULE_DESCRIPTION("Silicon Labs HID USB to SMBus master bridge");
MODULE_AUTHOR("David Barksdale <dbarksdale@uplogix.com>");
MODULE_LICENSE("GPL");

