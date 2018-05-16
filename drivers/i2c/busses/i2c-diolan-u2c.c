/*
 * Driver for the Diolan u2c-12 USB-I2C adapter
 *
 * Copyright (c) 2010-2011 Ericsson AB
 *
 * Derived from:
 *  i2c-tiny-usb.c
 *  Copyright (C) 2006-2007 Till Harbaum (Till@Harbaum.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/i2c.h>

#define DRIVER_NAME		"i2c-diolan-u2c"

#define USB_VENDOR_ID_DIOLAN		0x0abf
#define USB_DEVICE_ID_DIOLAN_U2C	0x3370


/* commands via USB, must match command ids in the firmware */
#define CMD_I2C_READ		0x01
#define CMD_I2C_WRITE		0x02
#define CMD_I2C_SCAN		0x03	/* Returns list of detected devices */
#define CMD_I2C_RELEASE_SDA	0x04
#define CMD_I2C_RELEASE_SCL	0x05
#define CMD_I2C_DROP_SDA	0x06
#define CMD_I2C_DROP_SCL	0x07
#define CMD_I2C_READ_SDA	0x08
#define CMD_I2C_READ_SCL	0x09
#define CMD_GET_FW_VERSION	0x0a
#define CMD_GET_SERIAL		0x0b
#define CMD_I2C_START		0x0c
#define CMD_I2C_STOP		0x0d
#define CMD_I2C_REPEATED_START	0x0e
#define CMD_I2C_PUT_BYTE	0x0f
#define CMD_I2C_GET_BYTE	0x10
#define CMD_I2C_PUT_ACK		0x11
#define CMD_I2C_GET_ACK		0x12
#define CMD_I2C_PUT_BYTE_ACK	0x13
#define CMD_I2C_GET_BYTE_ACK	0x14
#define CMD_I2C_SET_SPEED	0x1b
#define CMD_I2C_GET_SPEED	0x1c
#define CMD_I2C_SET_CLK_SYNC	0x24
#define CMD_I2C_GET_CLK_SYNC	0x25
#define CMD_I2C_SET_CLK_SYNC_TO	0x26
#define CMD_I2C_GET_CLK_SYNC_TO	0x27

#define RESP_OK			0x00
#define RESP_FAILED		0x01
#define RESP_BAD_MEMADDR	0x04
#define RESP_DATA_ERR		0x05
#define RESP_NOT_IMPLEMENTED	0x06
#define RESP_NACK		0x07
#define RESP_TIMEOUT		0x09

#define U2C_I2C_SPEED_FAST	0	/* 400 kHz */
#define U2C_I2C_SPEED_STD	1	/* 100 kHz */
#define U2C_I2C_SPEED_2KHZ	242	/* 2 kHz, minimum speed */
#define U2C_I2C_SPEED(f)	((DIV_ROUND_UP(1000000, (f)) - 10) / 2 + 1)

#define U2C_I2C_FREQ_FAST	400000
#define U2C_I2C_FREQ_STD	100000
#define U2C_I2C_FREQ(s)		(1000000 / (2 * (s - 1) + 10))

#define DIOLAN_USB_TIMEOUT	100	/* in ms */
#define DIOLAN_SYNC_TIMEOUT	20	/* in ms */

#define DIOLAN_OUTBUF_LEN	128
#define DIOLAN_FLUSH_LEN	(DIOLAN_OUTBUF_LEN - 4)
#define DIOLAN_INBUF_LEN	256	/* Maximum supported receive length */

/* Structure to hold all of our device specific stuff */
struct i2c_diolan_u2c {
	u8 obuffer[DIOLAN_OUTBUF_LEN];	/* output buffer */
	u8 ibuffer[DIOLAN_INBUF_LEN];	/* input buffer */
	int ep_in, ep_out;              /* Endpoints    */
	struct usb_device *usb_dev;	/* the usb device for this device */
	struct usb_interface *interface;/* the interface for this device */
	struct i2c_adapter adapter;	/* i2c related things */
	int olen;			/* Output buffer length */
	int ocount;			/* Number of enqueued messages */
};

static uint frequency = U2C_I2C_FREQ_STD;	/* I2C clock frequency in Hz */

module_param(frequency, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(frequency, "I2C clock frequency in hertz");

/* usb layer */

/* Send command to device, and get response. */
static int diolan_usb_transfer(struct i2c_diolan_u2c *dev)
{
	int ret = 0;
	int actual;
	int i;

	if (!dev->olen || !dev->ocount)
		return -EINVAL;

	ret = usb_bulk_msg(dev->usb_dev,
			   usb_sndbulkpipe(dev->usb_dev, dev->ep_out),
			   dev->obuffer, dev->olen, &actual,
			   DIOLAN_USB_TIMEOUT);
	if (!ret) {
		for (i = 0; i < dev->ocount; i++) {
			int tmpret;

			tmpret = usb_bulk_msg(dev->usb_dev,
					      usb_rcvbulkpipe(dev->usb_dev,
							      dev->ep_in),
					      dev->ibuffer,
					      sizeof(dev->ibuffer), &actual,
					      DIOLAN_USB_TIMEOUT);
			/*
			 * Stop command processing if a previous command
			 * returned an error.
			 * Note that we still need to retrieve all messages.
			 */
			if (ret < 0)
				continue;
			ret = tmpret;
			if (ret == 0 && actual > 0) {
				switch (dev->ibuffer[actual - 1]) {
				case RESP_NACK:
					/*
					 * Return ENXIO if NACK was received as
					 * response to the address phase,
					 * EIO otherwise
					 */
					ret = i == 1 ? -ENXIO : -EIO;
					break;
				case RESP_TIMEOUT:
					ret = -ETIMEDOUT;
					break;
				case RESP_OK:
					/* strip off return code */
					ret = actual - 1;
					break;
				default:
					ret = -EIO;
					break;
				}
			}
		}
	}
	dev->olen = 0;
	dev->ocount = 0;
	return ret;
}

static int diolan_write_cmd(struct i2c_diolan_u2c *dev, bool flush)
{
	if (flush || dev->olen >= DIOLAN_FLUSH_LEN)
		return diolan_usb_transfer(dev);
	return 0;
}

/* Send command (no data) */
static int diolan_usb_cmd(struct i2c_diolan_u2c *dev, u8 command, bool flush)
{
	dev->obuffer[dev->olen++] = command;
	dev->ocount++;
	return diolan_write_cmd(dev, flush);
}

/* Send command with one byte of data */
static int diolan_usb_cmd_data(struct i2c_diolan_u2c *dev, u8 command, u8 data,
			       bool flush)
{
	dev->obuffer[dev->olen++] = command;
	dev->obuffer[dev->olen++] = data;
	dev->ocount++;
	return diolan_write_cmd(dev, flush);
}

/* Send command with two bytes of data */
static int diolan_usb_cmd_data2(struct i2c_diolan_u2c *dev, u8 command, u8 d1,
				u8 d2, bool flush)
{
	dev->obuffer[dev->olen++] = command;
	dev->obuffer[dev->olen++] = d1;
	dev->obuffer[dev->olen++] = d2;
	dev->ocount++;
	return diolan_write_cmd(dev, flush);
}

/*
 * Flush input queue.
 * If we don't do this at startup and the controller has queued up
 * messages which were not retrieved, it will stop responding
 * at some point.
 */
static void diolan_flush_input(struct i2c_diolan_u2c *dev)
{
	int i;

	for (i = 0; i < 10; i++) {
		int actual = 0;
		int ret;

		ret = usb_bulk_msg(dev->usb_dev,
				   usb_rcvbulkpipe(dev->usb_dev, dev->ep_in),
				   dev->ibuffer, sizeof(dev->ibuffer), &actual,
				   DIOLAN_USB_TIMEOUT);
		if (ret < 0 || actual == 0)
			break;
	}
	if (i == 10)
		dev_err(&dev->interface->dev, "Failed to flush input buffer\n");
}

static int diolan_i2c_start(struct i2c_diolan_u2c *dev)
{
	return diolan_usb_cmd(dev, CMD_I2C_START, false);
}

static int diolan_i2c_repeated_start(struct i2c_diolan_u2c *dev)
{
	return diolan_usb_cmd(dev, CMD_I2C_REPEATED_START, false);
}

static int diolan_i2c_stop(struct i2c_diolan_u2c *dev)
{
	return diolan_usb_cmd(dev, CMD_I2C_STOP, true);
}

static int diolan_i2c_get_byte_ack(struct i2c_diolan_u2c *dev, bool ack,
				   u8 *byte)
{
	int ret;

	ret = diolan_usb_cmd_data(dev, CMD_I2C_GET_BYTE_ACK, ack, true);
	if (ret > 0)
		*byte = dev->ibuffer[0];
	else if (ret == 0)
		ret = -EIO;

	return ret;
}

static int diolan_i2c_put_byte_ack(struct i2c_diolan_u2c *dev, u8 byte)
{
	return diolan_usb_cmd_data(dev, CMD_I2C_PUT_BYTE_ACK, byte, false);
}

static int diolan_set_speed(struct i2c_diolan_u2c *dev, u8 speed)
{
	return diolan_usb_cmd_data(dev, CMD_I2C_SET_SPEED, speed, true);
}

/* Enable or disable clock synchronization (stretching) */
static int diolan_set_clock_synch(struct i2c_diolan_u2c *dev, bool enable)
{
	return diolan_usb_cmd_data(dev, CMD_I2C_SET_CLK_SYNC, enable, true);
}

/* Set clock synchronization timeout in ms */
static int diolan_set_clock_synch_timeout(struct i2c_diolan_u2c *dev, int ms)
{
	int to_val = ms * 10;

	return diolan_usb_cmd_data2(dev, CMD_I2C_SET_CLK_SYNC_TO,
				    to_val & 0xff, (to_val >> 8) & 0xff, true);
}

static void diolan_fw_version(struct i2c_diolan_u2c *dev)
{
	int ret;

	ret = diolan_usb_cmd(dev, CMD_GET_FW_VERSION, true);
	if (ret >= 2)
		dev_info(&dev->interface->dev,
			 "Diolan U2C firmware version %u.%u\n",
			 (unsigned int)dev->ibuffer[0],
			 (unsigned int)dev->ibuffer[1]);
}

static void diolan_get_serial(struct i2c_diolan_u2c *dev)
{
	int ret;
	u32 serial;

	ret = diolan_usb_cmd(dev, CMD_GET_SERIAL, true);
	if (ret >= 4) {
		serial = le32_to_cpu(*(u32 *)dev->ibuffer);
		dev_info(&dev->interface->dev,
			 "Diolan U2C serial number %u\n", serial);
	}
}

static int diolan_init(struct i2c_diolan_u2c *dev)
{
	int speed, ret;

	if (frequency >= 200000) {
		speed = U2C_I2C_SPEED_FAST;
		frequency = U2C_I2C_FREQ_FAST;
	} else if (frequency >= 100000 || frequency == 0) {
		speed = U2C_I2C_SPEED_STD;
		frequency = U2C_I2C_FREQ_STD;
	} else {
		speed = U2C_I2C_SPEED(frequency);
		if (speed > U2C_I2C_SPEED_2KHZ)
			speed = U2C_I2C_SPEED_2KHZ;
		frequency = U2C_I2C_FREQ(speed);
	}

	dev_info(&dev->interface->dev,
		 "Diolan U2C at USB bus %03d address %03d speed %d Hz\n",
		 dev->usb_dev->bus->busnum, dev->usb_dev->devnum, frequency);

	diolan_flush_input(dev);
	diolan_fw_version(dev);
	diolan_get_serial(dev);

	/* Set I2C speed */
	ret = diolan_set_speed(dev, speed);
	if (ret < 0)
		return ret;

	/* Configure I2C clock synchronization */
	ret = diolan_set_clock_synch(dev, speed != U2C_I2C_SPEED_FAST);
	if (ret < 0)
		return ret;

	if (speed != U2C_I2C_SPEED_FAST)
		ret = diolan_set_clock_synch_timeout(dev, DIOLAN_SYNC_TIMEOUT);

	return ret;
}

/* i2c layer */

static int diolan_usb_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			   int num)
{
	struct i2c_diolan_u2c *dev = i2c_get_adapdata(adapter);
	struct i2c_msg *pmsg;
	int i, j;
	int ret, sret;

	ret = diolan_i2c_start(dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		if (i) {
			ret = diolan_i2c_repeated_start(dev);
			if (ret < 0)
				goto abort;
		}
		ret = diolan_i2c_put_byte_ack(dev,
					      i2c_8bit_addr_from_msg(pmsg));
		if (ret < 0)
			goto abort;
		if (pmsg->flags & I2C_M_RD) {
			for (j = 0; j < pmsg->len; j++) {
				u8 byte;
				bool ack = j < pmsg->len - 1;

				/*
				 * Don't send NACK if this is the first byte
				 * of a SMBUS_BLOCK message.
				 */
				if (j == 0 && (pmsg->flags & I2C_M_RECV_LEN))
					ack = true;

				ret = diolan_i2c_get_byte_ack(dev, ack, &byte);
				if (ret < 0)
					goto abort;
				/*
				 * Adjust count if first received byte is length
				 */
				if (j == 0 && (pmsg->flags & I2C_M_RECV_LEN)) {
					if (byte == 0
					    || byte > I2C_SMBUS_BLOCK_MAX) {
						ret = -EPROTO;
						goto abort;
					}
					pmsg->len += byte;
				}
				pmsg->buf[j] = byte;
			}
		} else {
			for (j = 0; j < pmsg->len; j++) {
				ret = diolan_i2c_put_byte_ack(dev,
							      pmsg->buf[j]);
				if (ret < 0)
					goto abort;
			}
		}
	}
	ret = num;
abort:
	sret = diolan_i2c_stop(dev);
	if (sret < 0 && ret >= 0)
		ret = sret;
	return ret;
}

/*
 * Return list of supported functionality.
 */
static u32 diolan_usb_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL;
}

static const struct i2c_algorithm diolan_usb_algorithm = {
	.master_xfer = diolan_usb_xfer,
	.functionality = diolan_usb_func,
};

/* device layer */

static const struct usb_device_id diolan_u2c_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_DIOLAN, USB_DEVICE_ID_DIOLAN_U2C) },
	{ }
};

MODULE_DEVICE_TABLE(usb, diolan_u2c_table);

static void diolan_u2c_free(struct i2c_diolan_u2c *dev)
{
	usb_put_dev(dev->usb_dev);
	kfree(dev);
}

static int diolan_u2c_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct usb_host_interface *hostif = interface->cur_altsetting;
	struct i2c_diolan_u2c *dev;
	int ret;

	if (hostif->desc.bInterfaceNumber != 0
	    || hostif->desc.bNumEndpoints < 2)
		return -ENODEV;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	dev->ep_out = hostif->endpoint[0].desc.bEndpointAddress;
	dev->ep_in = hostif->endpoint[1].desc.bEndpointAddress;

	dev->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* setup i2c adapter description */
	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_HWMON;
	dev->adapter.algo = &diolan_usb_algorithm;
	i2c_set_adapdata(&dev->adapter, dev);
	snprintf(dev->adapter.name, sizeof(dev->adapter.name),
		 DRIVER_NAME " at bus %03d device %03d",
		 dev->usb_dev->bus->busnum, dev->usb_dev->devnum);

	dev->adapter.dev.parent = &dev->interface->dev;

	/* initialize diolan i2c interface */
	ret = diolan_init(dev);
	if (ret < 0) {
		dev_err(&interface->dev, "failed to initialize adapter\n");
		goto error_free;
	}

	/* and finally attach to i2c layer */
	ret = i2c_add_adapter(&dev->adapter);
	if (ret < 0)
		goto error_free;

	dev_dbg(&interface->dev, "connected " DRIVER_NAME "\n");

	return 0;

error_free:
	usb_set_intfdata(interface, NULL);
	diolan_u2c_free(dev);
error:
	return ret;
}

static void diolan_u2c_disconnect(struct usb_interface *interface)
{
	struct i2c_diolan_u2c *dev = usb_get_intfdata(interface);

	i2c_del_adapter(&dev->adapter);
	usb_set_intfdata(interface, NULL);
	diolan_u2c_free(dev);

	dev_dbg(&interface->dev, "disconnected\n");
}

static struct usb_driver diolan_u2c_driver = {
	.name = DRIVER_NAME,
	.probe = diolan_u2c_probe,
	.disconnect = diolan_u2c_disconnect,
	.id_table = diolan_u2c_table,
};

module_usb_driver(diolan_u2c_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION(DRIVER_NAME " driver");
MODULE_LICENSE("GPL");
