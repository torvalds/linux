// SPDX-License-Identifier: GPL-2.0
// tm6000-i2c.c - driver for TM5600/TM6000/TM6010 USB video capture devices
//
// Copyright (c) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>
//
// Copyright (c) 2007 Michel Ludwig <michel.ludwig@gmail.com>
//	- Fix SMBus Read Byte command

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/i2c.h>

#include "tm6000.h"
#include "tm6000-regs.h"
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include "tuner-xc2028.h"


/* ----------------------------------------------------------- */

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#define i2c_dprintk(lvl, fmt, args...) if (i2c_debug >= lvl) do { \
			printk(KERN_DEBUG "%s at %s: " fmt, \
			dev->name, __func__, ##args); } while (0)

static int tm6000_i2c_send_regs(struct tm6000_core *dev, unsigned char addr,
				__u8 reg, char *buf, int len)
{
	int rc;
	unsigned int i2c_packet_limit = 16;

	if (dev->dev_type == TM6010)
		i2c_packet_limit = 80;

	if (!buf)
		return -1;

	if (len < 1 || len > i2c_packet_limit) {
		printk(KERN_ERR "Incorrect length of i2c packet = %d, limit set to %d\n",
			len, i2c_packet_limit);
		return -1;
	}

	/* capture mutex */
	rc = tm6000_read_write_usb(dev, USB_DIR_OUT | USB_TYPE_VENDOR |
		USB_RECIP_DEVICE, REQ_16_SET_GET_I2C_WR1_RDN,
		addr | reg << 8, 0, buf, len);

	if (rc < 0) {
		/* release mutex */
		return rc;
	}

	/* release mutex */
	return rc;
}

/* Generic read - doesn't work fine with 16bit registers */
static int tm6000_i2c_recv_regs(struct tm6000_core *dev, unsigned char addr,
				__u8 reg, char *buf, int len)
{
	int rc;
	u8 b[2];
	unsigned int i2c_packet_limit = 16;

	if (dev->dev_type == TM6010)
		i2c_packet_limit = 64;

	if (!buf)
		return -1;

	if (len < 1 || len > i2c_packet_limit) {
		printk(KERN_ERR "Incorrect length of i2c packet = %d, limit set to %d\n",
			len, i2c_packet_limit);
		return -1;
	}

	/* capture mutex */
	if ((dev->caps.has_zl10353) && (dev->demod_addr << 1 == addr) && (reg % 2 == 0)) {
		/*
		 * Workaround an I2C bug when reading from zl10353
		 */
		reg -= 1;
		len += 1;

		rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			REQ_16_SET_GET_I2C_WR1_RDN, addr | reg << 8, 0, b, len);

		*buf = b[1];
	} else {
		rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			REQ_16_SET_GET_I2C_WR1_RDN, addr | reg << 8, 0, buf, len);
	}

	/* release mutex */
	return rc;
}

/*
 * read from a 16bit register
 * for example xc2028, xc3028 or xc3028L
 */
static int tm6000_i2c_recv_regs16(struct tm6000_core *dev, unsigned char addr,
				  __u16 reg, char *buf, int len)
{
	int rc;
	unsigned char ureg;

	if (!buf || len != 2)
		return -1;

	/* capture mutex */
	if (dev->dev_type == TM6010) {
		ureg = reg & 0xFF;
		rc = tm6000_read_write_usb(dev, USB_DIR_OUT | USB_TYPE_VENDOR |
			USB_RECIP_DEVICE, REQ_16_SET_GET_I2C_WR1_RDN,
			addr | (reg & 0xFF00), 0, &ureg, 1);

		if (rc < 0) {
			/* release mutex */
			return rc;
		}

		rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR |
			USB_RECIP_DEVICE, REQ_35_AFTEK_TUNER_READ,
			reg, 0, buf, len);
	} else {
		rc = tm6000_read_write_usb(dev, USB_DIR_IN | USB_TYPE_VENDOR |
			USB_RECIP_DEVICE, REQ_14_SET_GET_I2C_WR2_RDN,
			addr, reg, buf, len);
	}

	/* release mutex */
	return rc;
}

static int tm6000_i2c_xfer(struct i2c_adapter *i2c_adap,
			   struct i2c_msg msgs[], int num)
{
	struct tm6000_core *dev = i2c_adap->algo_data;
	int addr, rc, i, byte;

	if (num <= 0)
		return 0;
	for (i = 0; i < num; i++) {
		addr = (msgs[i].addr << 1) & 0xff;
		i2c_dprintk(2, "%s %s addr=0x%x len=%d:",
			 (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			 i == num - 1 ? "stop" : "nonstop", addr, msgs[i].len);
		if (msgs[i].flags & I2C_M_RD) {
			/* read request without preceding register selection */
			/*
			 * The TM6000 only supports a read transaction
			 * immediately after a 1 or 2 byte write to select
			 * a register.  We cannot fulfil this request.
			 */
			i2c_dprintk(2, " read without preceding write not supported");
			rc = -EOPNOTSUPP;
			goto err;
		} else if (i + 1 < num && msgs[i].len <= 2 &&
			   (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {
			/* 1 or 2 byte write followed by a read */
			if (i2c_debug >= 2)
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(KERN_CONT " %02x", msgs[i].buf[byte]);
			i2c_dprintk(2, "; joined to read %s len=%d:",
				    i == num - 2 ? "stop" : "nonstop",
				    msgs[i + 1].len);

			if (msgs[i].len == 2) {
				rc = tm6000_i2c_recv_regs16(dev, addr,
					msgs[i].buf[0] << 8 | msgs[i].buf[1],
					msgs[i + 1].buf, msgs[i + 1].len);
			} else {
				rc = tm6000_i2c_recv_regs(dev, addr, msgs[i].buf[0],
					msgs[i + 1].buf, msgs[i + 1].len);
			}

			i++;

			if (addr == dev->tuner_addr << 1) {
				tm6000_set_reg(dev, REQ_50_SET_START, 0, 0);
				tm6000_set_reg(dev, REQ_51_SET_STOP, 0, 0);
			}
			if (i2c_debug >= 2)
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(KERN_CONT " %02x", msgs[i].buf[byte]);
		} else {
			/* write bytes */
			if (i2c_debug >= 2)
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(KERN_CONT " %02x", msgs[i].buf[byte]);
			rc = tm6000_i2c_send_regs(dev, addr, msgs[i].buf[0],
				msgs[i].buf + 1, msgs[i].len - 1);
		}
		if (i2c_debug >= 2)
			printk(KERN_CONT "\n");
		if (rc < 0)
			goto err;
	}

	return num;
err:
	i2c_dprintk(2, " ERROR: %i\n", rc);
	return rc;
}

static int tm6000_i2c_eeprom(struct tm6000_core *dev)
{
	int i, rc;
	unsigned char *p = dev->eedata;
	unsigned char bytes[17];

	dev->i2c_client.addr = 0xa0 >> 1;
	dev->eedata_size = 0;

	bytes[16] = '\0';
	for (i = 0; i < sizeof(dev->eedata); ) {
		*p = i;
		rc = tm6000_i2c_recv_regs(dev, 0xa0, i, p, 1);
		if (rc < 1) {
			if (p == dev->eedata)
				goto noeeprom;
			else {
				printk(KERN_WARNING
				"%s: i2c eeprom read error (err=%d)\n",
				dev->name, rc);
			}
			return -EINVAL;
		}
		dev->eedata_size++;
		p++;
		if (0 == (i % 16))
			printk(KERN_INFO "%s: i2c eeprom %02x:", dev->name, i);
		printk(KERN_CONT " %02x", dev->eedata[i]);
		if ((dev->eedata[i] >= ' ') && (dev->eedata[i] <= 'z'))
			bytes[i%16] = dev->eedata[i];
		else
			bytes[i%16] = '.';

		i++;

		if (0 == (i % 16)) {
			bytes[16] = '\0';
			printk(KERN_CONT "  %s\n", bytes);
		}
	}
	if (0 != (i%16)) {
		bytes[i%16] = '\0';
		for (i %= 16; i < 16; i++)
			printk(KERN_CONT "   ");
		printk(KERN_CONT "  %s\n", bytes);
	}

	return 0;

noeeprom:
	printk(KERN_INFO "%s: Huh, no eeprom present (err=%d)?\n",
	       dev->name, rc);
	return -EINVAL;
}

/* ----------------------------------------------------------- */

/*
 * functionality()
 */
static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm tm6000_algo = {
	.master_xfer   = tm6000_i2c_xfer,
	.functionality = functionality,
};

/* ----------------------------------------------------------- */

/*
 * tm6000_i2c_register()
 * register i2c bus
 */
int tm6000_i2c_register(struct tm6000_core *dev)
{
	int rc;

	dev->i2c_adap.owner = THIS_MODULE;
	dev->i2c_adap.algo = &tm6000_algo;
	dev->i2c_adap.dev.parent = &dev->udev->dev;
	strlcpy(dev->i2c_adap.name, dev->name, sizeof(dev->i2c_adap.name));
	dev->i2c_adap.algo_data = dev;
	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);
	rc = i2c_add_adapter(&dev->i2c_adap);
	if (rc)
		return rc;

	dev->i2c_client.adapter = &dev->i2c_adap;
	strlcpy(dev->i2c_client.name, "tm6000 internal", I2C_NAME_SIZE);
	tm6000_i2c_eeprom(dev);

	return 0;
}

/*
 * tm6000_i2c_unregister()
 * unregister i2c_bus
 */
int tm6000_i2c_unregister(struct tm6000_core *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}
