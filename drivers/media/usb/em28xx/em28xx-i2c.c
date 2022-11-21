// SPDX-License-Identifier: GPL-2.0+
//
// em28xx-i2c.c - driver for Empia EM2800/EM2820/2840 USB video capture devices
//
// Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
//		      Markus Rechberger <mrechberger@gmail.com>
//		      Mauro Carvalho Chehab <mchehab@kernel.org>
//		      Sascha Sommer <saschasommer@freenet.de>
// Copyright (C) 2013 Frank Schäfer <fschaefer.oss@googlemail.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "em28xx.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>

#include "xc2028.h"
#include <media/v4l2-common.h>
#include <media/tuner.h>

/* ----------------------------------------------------------- */

static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "i2c debug message level (1: normal debug, 2: show I2C transfers)");

#define dprintk(level, fmt, arg...) do {				\
	if (i2c_debug > level)						\
		dev_printk(KERN_DEBUG, &dev->intf->dev,			\
			   "i2c: %s: " fmt, __func__, ## arg);		\
} while (0)

/*
 * Time in msecs to wait for i2c xfers to finish.
 * 35ms is the maximum time a SMBUS device could wait when
 * clock stretching is used. As the transfer itself will take
 * some time to happen, set it to 35 ms.
 *
 * Ok, I2C doesn't specify any limit. So, eventually, we may need
 * to increase this timeout.
 */
#define EM28XX_I2C_XFER_TIMEOUT         35 /* ms */

static int em28xx_i2c_timeout(struct em28xx *dev)
{
	int time = EM28XX_I2C_XFER_TIMEOUT;

	switch (dev->i2c_speed & 0x03) {
	case EM28XX_I2C_FREQ_25_KHZ:
		time += 4;		/* Assume 4 ms for transfers */
		break;
	case EM28XX_I2C_FREQ_100_KHZ:
	case EM28XX_I2C_FREQ_400_KHZ:
		time += 1;		/* Assume 1 ms for transfers */
		break;
	default: /* EM28XX_I2C_FREQ_1_5_MHZ */
		break;
	}

	return msecs_to_jiffies(time);
}

/*
 * em2800_i2c_send_bytes()
 * send up to 4 bytes to the em2800 i2c device
 */
static int em2800_i2c_send_bytes(struct em28xx *dev, u8 addr, u8 *buf, u16 len)
{
	unsigned long timeout = jiffies + em28xx_i2c_timeout(dev);
	int ret;
	u8 b2[6];

	if (len < 1 || len > 4)
		return -EOPNOTSUPP;

	b2[5] = 0x80 + len - 1;
	b2[4] = addr;
	b2[3] = buf[0];
	if (len > 1)
		b2[2] = buf[1];
	if (len > 2)
		b2[1] = buf[2];
	if (len > 3)
		b2[0] = buf[3];

	/* trigger write */
	ret = dev->em28xx_write_regs(dev, 4 - len, &b2[4 - len], 2 + len);
	if (ret != 2 + len) {
		dev_warn(&dev->intf->dev,
			 "failed to trigger write to i2c address 0x%x (error=%i)\n",
			    addr, ret);
		return (ret < 0) ? ret : -EIO;
	}
	/* wait for completion */
	while (time_is_after_jiffies(timeout)) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0x80 + len - 1)
			return len;
		if (ret == 0x94 + len - 1) {
			dprintk(1, "R05 returned 0x%02x: I2C ACK error\n", ret);
			return -ENXIO;
		}
		if (ret < 0) {
			dev_warn(&dev->intf->dev,
				 "failed to get i2c transfer status from bridge register (error=%i)\n",
				ret);
			return ret;
		}
		usleep_range(5000, 6000);
	}
	dprintk(0, "write to i2c device at 0x%x timed out\n", addr);
	return -ETIMEDOUT;
}

/*
 * em2800_i2c_recv_bytes()
 * read up to 4 bytes from the em2800 i2c device
 */
static int em2800_i2c_recv_bytes(struct em28xx *dev, u8 addr, u8 *buf, u16 len)
{
	unsigned long timeout = jiffies + em28xx_i2c_timeout(dev);
	u8 buf2[4];
	int ret;
	int i;

	if (len < 1 || len > 4)
		return -EOPNOTSUPP;

	/* trigger read */
	buf2[1] = 0x84 + len - 1;
	buf2[0] = addr;
	ret = dev->em28xx_write_regs(dev, 0x04, buf2, 2);
	if (ret != 2) {
		dev_warn(&dev->intf->dev,
			 "failed to trigger read from i2c address 0x%x (error=%i)\n",
			 addr, ret);
		return (ret < 0) ? ret : -EIO;
	}

	/* wait for completion */
	while (time_is_after_jiffies(timeout)) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0x84 + len - 1)
			break;
		if (ret == 0x94 + len - 1) {
			dprintk(1, "R05 returned 0x%02x: I2C ACK error\n",
				ret);
			return -ENXIO;
		}
		if (ret < 0) {
			dev_warn(&dev->intf->dev,
				 "failed to get i2c transfer status from bridge register (error=%i)\n",
				 ret);
			return ret;
		}
		usleep_range(5000, 6000);
	}
	if (ret != 0x84 + len - 1)
		dprintk(0, "read from i2c device at 0x%x timed out\n", addr);

	/* get the received message */
	ret = dev->em28xx_read_reg_req_len(dev, 0x00, 4 - len, buf2, len);
	if (ret != len) {
		dev_warn(&dev->intf->dev,
			 "reading from i2c device at 0x%x failed: couldn't get the received message from the bridge (error=%i)\n",
			 addr, ret);
		return (ret < 0) ? ret : -EIO;
	}
	for (i = 0; i < len; i++)
		buf[i] = buf2[len - 1 - i];

	return ret;
}

/*
 * em2800_i2c_check_for_device()
 * check if there is an i2c device at the supplied address
 */
static int em2800_i2c_check_for_device(struct em28xx *dev, u8 addr)
{
	u8 buf;
	int ret;

	ret = em2800_i2c_recv_bytes(dev, addr, &buf, 1);
	if (ret == 1)
		return 0;
	return (ret < 0) ? ret : -EIO;
}

/*
 * em28xx_i2c_send_bytes()
 */
static int em28xx_i2c_send_bytes(struct em28xx *dev, u16 addr, u8 *buf,
				 u16 len, int stop)
{
	unsigned long timeout = jiffies + em28xx_i2c_timeout(dev);
	int ret;

	if (len < 1 || len > 64)
		return -EOPNOTSUPP;
	/*
	 * NOTE: limited by the USB ctrl message constraints
	 * Zero length reads always succeed, even if no device is connected
	 */

	/* Write to i2c device */
	ret = dev->em28xx_write_regs_req(dev, stop ? 2 : 3, addr, buf, len);
	if (ret != len) {
		if (ret < 0) {
			dev_warn(&dev->intf->dev,
				 "writing to i2c device at 0x%x failed (error=%i)\n",
				 addr, ret);
			return ret;
		}
		dev_warn(&dev->intf->dev,
			 "%i bytes write to i2c device at 0x%x requested, but %i bytes written\n",
				len, addr, ret);
		return -EIO;
	}

	/* wait for completion */
	while (time_is_after_jiffies(timeout)) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0) /* success */
			return len;
		if (ret == 0x10) {
			dprintk(1, "I2C ACK error on writing to addr 0x%02x\n",
				addr);
			return -ENXIO;
		}
		if (ret < 0) {
			dev_warn(&dev->intf->dev,
				 "failed to get i2c transfer status from bridge register (error=%i)\n",
				 ret);
			return ret;
		}
		usleep_range(5000, 6000);
		/*
		 * NOTE: do we really have to wait for success ?
		 * Never seen anything else than 0x00 or 0x10
		 * (even with high payload) ...
		 */
	}

	if (ret == 0x02 || ret == 0x04) {
		/* NOTE: these errors seem to be related to clock stretching */
		dprintk(0,
			"write to i2c device at 0x%x timed out (status=%i)\n",
			addr, ret);
		return -ETIMEDOUT;
	}

	dev_warn(&dev->intf->dev,
		 "write to i2c device at 0x%x failed with unknown error (status=%i)\n",
		 addr, ret);
	return -EIO;
}

/*
 * em28xx_i2c_recv_bytes()
 * read a byte from the i2c device
 */
static int em28xx_i2c_recv_bytes(struct em28xx *dev, u16 addr, u8 *buf, u16 len)
{
	int ret;

	if (len < 1 || len > 64)
		return -EOPNOTSUPP;
	/*
	 * NOTE: limited by the USB ctrl message constraints
	 * Zero length reads always succeed, even if no device is connected
	 */

	/* Read data from i2c device */
	ret = dev->em28xx_read_reg_req_len(dev, 2, addr, buf, len);
	if (ret < 0) {
		dev_warn(&dev->intf->dev,
			 "reading from i2c device at 0x%x failed (error=%i)\n",
			 addr, ret);
		return ret;
	} else if (ret != len) {
		dev_dbg(&dev->intf->dev,
			"%i bytes read from i2c device at 0x%x requested, but %i bytes written\n",
				ret, addr, len);
	}
	/*
	 * NOTE: some devices with two i2c buses have the bad habit to return 0
	 * bytes if we are on bus B AND there was no write attempt to the
	 * specified slave address before AND no device is present at the
	 * requested slave address.
	 * Anyway, the next check will fail with -ENXIO in this case, so avoid
	 * spamming the system log on device probing and do nothing here.
	 */

	/* Check success of the i2c operation */
	ret = dev->em28xx_read_reg(dev, 0x05);
	if (ret == 0) /* success */
		return len;
	if (ret < 0) {
		dev_warn(&dev->intf->dev,
			 "failed to get i2c transfer status from bridge register (error=%i)\n",
			 ret);
		return ret;
	}
	if (ret == 0x10) {
		dprintk(1, "I2C ACK error on writing to addr 0x%02x\n",
			addr);
		return -ENXIO;
	}

	if (ret == 0x02 || ret == 0x04) {
		/* NOTE: these errors seem to be related to clock stretching */
		dprintk(0,
			"write to i2c device at 0x%x timed out (status=%i)\n",
			addr, ret);
		return -ETIMEDOUT;
	}

	dev_warn(&dev->intf->dev,
		 "read from i2c device at 0x%x failed with unknown error (status=%i)\n",
		 addr, ret);
	return -EIO;
}

/*
 * em28xx_i2c_check_for_device()
 * check if there is a i2c_device at the supplied address
 */
static int em28xx_i2c_check_for_device(struct em28xx *dev, u16 addr)
{
	int ret;
	u8 buf;

	ret = em28xx_i2c_recv_bytes(dev, addr, &buf, 1);
	if (ret == 1)
		return 0;
	return (ret < 0) ? ret : -EIO;
}

/*
 * em25xx_bus_B_send_bytes
 * write bytes to the i2c device
 */
static int em25xx_bus_B_send_bytes(struct em28xx *dev, u16 addr, u8 *buf,
				   u16 len)
{
	int ret;

	if (len < 1 || len > 64)
		return -EOPNOTSUPP;
	/*
	 * NOTE: limited by the USB ctrl message constraints
	 * Zero length reads always succeed, even if no device is connected
	 */

	/* Set register and write value */
	ret = dev->em28xx_write_regs_req(dev, 0x06, addr, buf, len);
	if (ret != len) {
		if (ret < 0) {
			dev_warn(&dev->intf->dev,
				 "writing to i2c device at 0x%x failed (error=%i)\n",
				 addr, ret);
			return ret;
		}

		dev_warn(&dev->intf->dev,
			 "%i bytes write to i2c device at 0x%x requested, but %i bytes written\n",
			 len, addr, ret);
		return -EIO;
	}
	/* Check success */
	ret = dev->em28xx_read_reg_req(dev, 0x08, 0x0000);
	/*
	 * NOTE: the only error we've seen so far is
	 * 0x01 when the slave device is not present
	 */
	if (!ret)
		return len;

	if (ret > 0) {
		dprintk(1, "Bus B R08 returned 0x%02x: I2C ACK error\n", ret);
		return -ENXIO;
	}

	return ret;
	/*
	 * NOTE: With chip types (other chip IDs) which actually don't support
	 * this operation, it seems to succeed ALWAYS ! (even if there is no
	 * slave device or even no second i2c bus provided)
	 */
}

/*
 * em25xx_bus_B_recv_bytes
 * read bytes from the i2c device
 */
static int em25xx_bus_B_recv_bytes(struct em28xx *dev, u16 addr, u8 *buf,
				   u16 len)
{
	int ret;

	if (len < 1 || len > 64)
		return -EOPNOTSUPP;
	/*
	 * NOTE: limited by the USB ctrl message constraints
	 * Zero length reads always succeed, even if no device is connected
	 */

	/* Read value */
	ret = dev->em28xx_read_reg_req_len(dev, 0x06, addr, buf, len);
	if (ret < 0) {
		dev_warn(&dev->intf->dev,
			 "reading from i2c device at 0x%x failed (error=%i)\n",
			 addr, ret);
		return ret;
	}
	/*
	 * NOTE: some devices with two i2c buses have the bad habit to return 0
	 * bytes if we are on bus B AND there was no write attempt to the
	 * specified slave address before AND no device is present at the
	 * requested slave address.
	 * Anyway, the next check will fail with -ENXIO in this case, so avoid
	 * spamming the system log on device probing and do nothing here.
	 */

	/* Check success */
	ret = dev->em28xx_read_reg_req(dev, 0x08, 0x0000);
	/*
	 * NOTE: the only error we've seen so far is
	 * 0x01 when the slave device is not present
	 */
	if (!ret)
		return len;

	if (ret > 0) {
		dprintk(1, "Bus B R08 returned 0x%02x: I2C ACK error\n", ret);
		return -ENXIO;
	}

	return ret;
	/*
	 * NOTE: With chip types (other chip IDs) which actually don't support
	 * this operation, it seems to succeed ALWAYS ! (even if there is no
	 * slave device or even no second i2c bus provided)
	 */
}

/*
 * em25xx_bus_B_check_for_device()
 * check if there is a i2c device at the supplied address
 */
static int em25xx_bus_B_check_for_device(struct em28xx *dev, u16 addr)
{
	u8 buf;
	int ret;

	ret = em25xx_bus_B_recv_bytes(dev, addr, &buf, 1);
	if (ret < 0)
		return ret;

	return 0;
	/*
	 * NOTE: With chips which do not support this operation,
	 * it seems to succeed ALWAYS ! (even if no device connected)
	 */
}

static inline int i2c_check_for_device(struct em28xx_i2c_bus *i2c_bus, u16 addr)
{
	struct em28xx *dev = i2c_bus->dev;
	int rc = -EOPNOTSUPP;

	if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM28XX)
		rc = em28xx_i2c_check_for_device(dev, addr);
	else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM2800)
		rc = em2800_i2c_check_for_device(dev, addr);
	else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM25XX_BUS_B)
		rc = em25xx_bus_B_check_for_device(dev, addr);
	return rc;
}

static inline int i2c_recv_bytes(struct em28xx_i2c_bus *i2c_bus,
				 struct i2c_msg msg)
{
	struct em28xx *dev = i2c_bus->dev;
	u16 addr = msg.addr << 1;
	int rc = -EOPNOTSUPP;

	if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM28XX)
		rc = em28xx_i2c_recv_bytes(dev, addr, msg.buf, msg.len);
	else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM2800)
		rc = em2800_i2c_recv_bytes(dev, addr, msg.buf, msg.len);
	else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM25XX_BUS_B)
		rc = em25xx_bus_B_recv_bytes(dev, addr, msg.buf, msg.len);
	return rc;
}

static inline int i2c_send_bytes(struct em28xx_i2c_bus *i2c_bus,
				 struct i2c_msg msg, int stop)
{
	struct em28xx *dev = i2c_bus->dev;
	u16 addr = msg.addr << 1;
	int rc = -EOPNOTSUPP;

	if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM28XX)
		rc = em28xx_i2c_send_bytes(dev, addr, msg.buf, msg.len, stop);
	else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM2800)
		rc = em2800_i2c_send_bytes(dev, addr, msg.buf, msg.len);
	else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM25XX_BUS_B)
		rc = em25xx_bus_B_send_bytes(dev, addr, msg.buf, msg.len);
	return rc;
}

/*
 * em28xx_i2c_xfer()
 * the main i2c transfer function
 */
static int em28xx_i2c_xfer(struct i2c_adapter *i2c_adap,
			   struct i2c_msg msgs[], int num)
{
	struct em28xx_i2c_bus *i2c_bus = i2c_adap->algo_data;
	struct em28xx *dev = i2c_bus->dev;
	unsigned int bus = i2c_bus->bus;
	int addr, rc, i;
	u8 reg;

	/*
	 * prevent i2c xfer attempts after device is disconnected
	 * some fe's try to do i2c writes/reads from their release
	 * interfaces when called in disconnect path
	 */
	if (dev->disconnected)
		return -ENODEV;

	if (!rt_mutex_trylock(&dev->i2c_bus_lock))
		return -EAGAIN;

	/* Switch I2C bus if needed */
	if (bus != dev->cur_i2c_bus &&
	    i2c_bus->algo_type == EM28XX_I2C_ALGO_EM28XX) {
		if (bus == 1)
			reg = EM2874_I2C_SECONDARY_BUS_SELECT;
		else
			reg = 0;
		em28xx_write_reg_bits(dev, EM28XX_R06_I2C_CLK, reg,
				      EM2874_I2C_SECONDARY_BUS_SELECT);
		dev->cur_i2c_bus = bus;
	}

	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		if (!msgs[i].len) {
			/*
			 * no len: check only for device presence
			 * This code is only called during device probe.
			 */
			rc = i2c_check_for_device(i2c_bus, addr);

			if (rc == -ENXIO)
				rc = -ENODEV;
		} else if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */
			rc = i2c_recv_bytes(i2c_bus, msgs[i]);
		} else {
			/* write bytes */
			rc = i2c_send_bytes(i2c_bus, msgs[i], i == num - 1);
		}

		if (rc < 0)
			goto error;

		dprintk(2, "%s %s addr=%02x len=%d: %*ph\n",
			(msgs[i].flags & I2C_M_RD) ? "read" : "write",
			i == num - 1 ? "stop" : "nonstop",
			addr, msgs[i].len,
			msgs[i].len, msgs[i].buf);
	}

	rt_mutex_unlock(&dev->i2c_bus_lock);
	return num;

error:
	dprintk(2, "%s %s addr=%02x len=%d: %sERROR: %i\n",
		(msgs[i].flags & I2C_M_RD) ? "read" : "write",
		i == num - 1 ? "stop" : "nonstop",
		addr, msgs[i].len,
		(rc == -ENODEV) ? "no device " : "",
		rc);

	rt_mutex_unlock(&dev->i2c_bus_lock);
	return rc;
}

/*
 * based on linux/sunrpc/svcauth.h and linux/hash.h
 * The original hash function returns a different value, if arch is x86_64
 * or i386.
 */
static inline unsigned long em28xx_hash_mem(char *buf, int length, int bits)
{
	unsigned long hash = 0;
	unsigned long l = 0;
	int len = 0;
	unsigned char c;

	do {
		if (len == length) {
			c = (char)len;
			len = -1;
		} else {
			c = *buf++;
		}
		l = (l << 8) | c;
		len++;
		if ((len & (32 / 8 - 1)) == 0)
			hash = ((hash ^ l) * 0x9e370001UL);
	} while (len);

	return (hash >> (32 - bits)) & 0xffffffffUL;
}

/*
 * Helper function to read data blocks from i2c clients with 8 or 16 bit
 * address width, 8 bit register width and auto incrementation been activated
 */
static int em28xx_i2c_read_block(struct em28xx *dev, unsigned int bus, u16 addr,
				 bool addr_w16, u16 len, u8 *data)
{
	int remain = len, rsize, rsize_max, ret;
	u8 buf[2];

	/* Sanity check */
	if (addr + remain > (addr_w16 * 0xff00 + 0xff + 1))
		return -EINVAL;
	/* Select address */
	buf[0] = addr >> 8;
	buf[1] = addr & 0xff;
	ret = i2c_master_send(&dev->i2c_client[bus],
			      buf + !addr_w16, 1 + addr_w16);
	if (ret < 0)
		return ret;
	/* Read data */
	if (dev->board.is_em2800)
		rsize_max = 4;
	else
		rsize_max = 64;
	while (remain > 0) {
		if (remain > rsize_max)
			rsize = rsize_max;
		else
			rsize = remain;

		ret = i2c_master_recv(&dev->i2c_client[bus], data, rsize);
		if (ret < 0)
			return ret;

		remain -= rsize;
		data += rsize;
	}

	return len;
}

static int em28xx_i2c_eeprom(struct em28xx *dev, unsigned int bus,
			     u8 **eedata, u16 *eedata_len)
{
	const u16 len = 256;
	/*
	 * FIXME common length/size for bytes to read, to display, hash
	 * calculation and returned device dataset. Simplifies the code a lot,
	 * but we might have to deal with multiple sizes in the future !
	 */
	int err;
	struct em28xx_eeprom *dev_config;
	u8 buf, *data;

	*eedata = NULL;
	*eedata_len = 0;

	/* EEPROM is always on i2c bus 0 on all known devices. */

	dev->i2c_client[bus].addr = 0xa0 >> 1;

	/* Check if board has eeprom */
	err = i2c_master_recv(&dev->i2c_client[bus], &buf, 0);
	if (err < 0) {
		dev_info(&dev->intf->dev, "board has no eeprom\n");
		return -ENODEV;
	}

	data = kzalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Read EEPROM content */
	err = em28xx_i2c_read_block(dev, bus, 0x0000,
				    dev->eeprom_addrwidth_16bit,
				    len, data);
	if (err != len) {
		dev_err(&dev->intf->dev,
			"failed to read eeprom (err=%d)\n", err);
		goto error;
	}

	if (i2c_debug) {
		/* Display eeprom content */
		print_hex_dump(KERN_DEBUG, "em28xx eeprom ", DUMP_PREFIX_OFFSET,
			       16, 1, data, len, true);

		if (dev->eeprom_addrwidth_16bit)
			dev_info(&dev->intf->dev,
				 "eeprom %06x: ... (skipped)\n", 256);
	}

	if (dev->eeprom_addrwidth_16bit &&
	    data[0] == 0x26 && data[3] == 0x00) {
		/* new eeprom format; size 4-64kb */
		u16 mc_start;
		u16 hwconf_offset;

		dev->hash = em28xx_hash_mem(data, len, 32);
		mc_start = (data[1] << 8) + 4;	/* usually 0x0004 */

		dev_info(&dev->intf->dev,
			 "EEPROM ID = %4ph, EEPROM hash = 0x%08lx\n",
			 data, dev->hash);
		dev_info(&dev->intf->dev,
			 "EEPROM info:\n");
		dev_info(&dev->intf->dev,
			 "\tmicrocode start address = 0x%04x, boot configuration = 0x%02x\n",
			 mc_start, data[2]);
		/*
		 * boot configuration (address 0x0002):
		 * [0]   microcode download speed: 1 = 400 kHz; 0 = 100 kHz
		 * [1]   always selects 12 kb RAM
		 * [2]   USB device speed: 1 = force Full Speed; 0 = auto detect
		 * [4]   1 = force fast mode and no suspend for device testing
		 * [5:7] USB PHY tuning registers; determined by device
		 *       characterization
		 */

		/*
		 * Read hardware config dataset offset from address
		 * (microcode start + 46)
		 */
		err = em28xx_i2c_read_block(dev, bus, mc_start + 46, 1, 2,
					    data);
		if (err != 2) {
			dev_err(&dev->intf->dev,
				"failed to read hardware configuration data from eeprom (err=%d)\n",
				err);
			goto error;
		}

		/* Calculate hardware config dataset start address */
		hwconf_offset = mc_start + data[0] + (data[1] << 8);

		/* Read hardware config dataset */
		/*
		 * NOTE: the microcode copy can be multiple pages long, but
		 * we assume the hardware config dataset is the same as in
		 * the old eeprom and not longer than 256 bytes.
		 * tveeprom is currently also limited to 256 bytes.
		 */
		err = em28xx_i2c_read_block(dev, bus, hwconf_offset, 1, len,
					    data);
		if (err != len) {
			dev_err(&dev->intf->dev,
				"failed to read hardware configuration data from eeprom (err=%d)\n",
				err);
			goto error;
		}

		/* Verify hardware config dataset */
		/* NOTE: not all devices provide this type of dataset */
		if (data[0] != 0x1a || data[1] != 0xeb ||
		    data[2] != 0x67 || data[3] != 0x95) {
			dev_info(&dev->intf->dev,
				 "\tno hardware configuration dataset found in eeprom\n");
			kfree(data);
			return 0;
		}

		/*
		 * TODO: decrypt eeprom data for camera bridges
		 * (em25xx, em276x+)
		 */

	} else if (!dev->eeprom_addrwidth_16bit &&
		   data[0] == 0x1a && data[1] == 0xeb &&
		   data[2] == 0x67 && data[3] == 0x95) {
		dev->hash = em28xx_hash_mem(data, len, 32);
		dev_info(&dev->intf->dev,
			 "EEPROM ID = %4ph, EEPROM hash = 0x%08lx\n",
			 data, dev->hash);
		dev_info(&dev->intf->dev,
			 "EEPROM info:\n");
	} else {
		dev_info(&dev->intf->dev,
			 "unknown eeprom format or eeprom corrupted !\n");
		err = -ENODEV;
		goto error;
	}

	*eedata = data;
	*eedata_len = len;
	dev_config = (void *)*eedata;

	switch (le16_to_cpu(dev_config->chip_conf) >> 4 & 0x3) {
	case 0:
		dev_info(&dev->intf->dev, "\tNo audio on board.\n");
		break;
	case 1:
		dev_info(&dev->intf->dev, "\tAC97 audio (5 sample rates)\n");
		break;
	case 2:
		if (dev->chip_id < CHIP_ID_EM2860)
			dev_info(&dev->intf->dev,
				 "\tI2S audio, sample rate=32k\n");
		else
			dev_info(&dev->intf->dev,
				 "\tI2S audio, 3 sample rates\n");
		break;
	case 3:
		if (dev->chip_id < CHIP_ID_EM2860)
			dev_info(&dev->intf->dev,
				 "\tI2S audio, 3 sample rates\n");
		else
			dev_info(&dev->intf->dev,
				 "\tI2S audio, 5 sample rates\n");
		break;
	}

	if (le16_to_cpu(dev_config->chip_conf) & 1 << 3)
		dev_info(&dev->intf->dev, "\tUSB Remote wakeup capable\n");

	if (le16_to_cpu(dev_config->chip_conf) & 1 << 2)
		dev_info(&dev->intf->dev, "\tUSB Self power capable\n");

	switch (le16_to_cpu(dev_config->chip_conf) & 0x3) {
	case 0:
		dev_info(&dev->intf->dev, "\t500mA max power\n");
		break;
	case 1:
		dev_info(&dev->intf->dev, "\t400mA max power\n");
		break;
	case 2:
		dev_info(&dev->intf->dev, "\t300mA max power\n");
		break;
	case 3:
		dev_info(&dev->intf->dev, "\t200mA max power\n");
		break;
	}
	dev_info(&dev->intf->dev,
		 "\tTable at offset 0x%02x, strings=0x%04x, 0x%04x, 0x%04x\n",
		 dev_config->string_idx_table,
		 le16_to_cpu(dev_config->string1),
		 le16_to_cpu(dev_config->string2),
		 le16_to_cpu(dev_config->string3));

	return 0;

error:
	kfree(data);
	return err;
}

/* ----------------------------------------------------------- */

/*
 * functionality()
 */
static u32 functionality(struct i2c_adapter *i2c_adap)
{
	struct em28xx_i2c_bus *i2c_bus = i2c_adap->algo_data;

	if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM28XX ||
	    i2c_bus->algo_type == EM28XX_I2C_ALGO_EM25XX_BUS_B) {
		return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
	} else if (i2c_bus->algo_type == EM28XX_I2C_ALGO_EM2800)  {
		return (I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL) &
			~I2C_FUNC_SMBUS_WRITE_BLOCK_DATA;
	}

	WARN(1, "Unknown i2c bus algorithm.\n");
	return 0;
}

static const struct i2c_algorithm em28xx_algo = {
	.master_xfer   = em28xx_i2c_xfer,
	.functionality = functionality,
};

static const struct i2c_adapter em28xx_adap_template = {
	.owner = THIS_MODULE,
	.name = "em28xx",
	.algo = &em28xx_algo,
};

static const struct i2c_client em28xx_client_template = {
	.name = "em28xx internal",
};

/* ----------------------------------------------------------- */

/*
 * i2c_devs
 * incomplete list of known devices
 */
static char *i2c_devs[128] = {
	[0x1c >> 1] = "lgdt330x",
	[0x3e >> 1] = "remote IR sensor",
	[0x4a >> 1] = "saa7113h",
	[0x52 >> 1] = "drxk",
	[0x60 >> 1] = "remote IR sensor",
	[0x8e >> 1] = "remote IR sensor",
	[0x86 >> 1] = "tda9887",
	[0x80 >> 1] = "msp34xx",
	[0x88 >> 1] = "msp34xx",
	[0xa0 >> 1] = "eeprom",
	[0xb0 >> 1] = "tda9874",
	[0xb8 >> 1] = "tvp5150a",
	[0xba >> 1] = "webcam sensor or tvp5150a",
	[0xc0 >> 1] = "tuner (analog)",
	[0xc2 >> 1] = "tuner (analog)",
	[0xc4 >> 1] = "tuner (analog)",
	[0xc6 >> 1] = "tuner (analog)",
};

/*
 * do_i2c_scan()
 * check i2c address range for devices
 */
void em28xx_do_i2c_scan(struct em28xx *dev, unsigned int bus)
{
	u8 i2c_devicelist[128];
	unsigned char buf;
	int i, rc;

	memset(i2c_devicelist, 0, sizeof(i2c_devicelist));

	for (i = 0; i < ARRAY_SIZE(i2c_devs); i++) {
		dev->i2c_client[bus].addr = i;
		rc = i2c_master_recv(&dev->i2c_client[bus], &buf, 0);
		if (rc < 0)
			continue;
		i2c_devicelist[i] = i;
		dev_info(&dev->intf->dev,
			 "found i2c device @ 0x%x on bus %d [%s]\n",
			 i << 1, bus, i2c_devs[i] ? i2c_devs[i] : "???");
	}

	if (bus == dev->def_i2c_bus)
		dev->i2c_hash = em28xx_hash_mem(i2c_devicelist,
						sizeof(i2c_devicelist), 32);
}

/*
 * em28xx_i2c_register()
 * register i2c bus
 */
int em28xx_i2c_register(struct em28xx *dev, unsigned int bus,
			enum em28xx_i2c_algo_type algo_type)
{
	int retval;

	if (WARN_ON(!dev->em28xx_write_regs || !dev->em28xx_read_reg ||
		    !dev->em28xx_write_regs_req || !dev->em28xx_read_reg_req))
		return -ENODEV;

	if (bus >= NUM_I2C_BUSES)
		return -ENODEV;

	dev->i2c_adap[bus] = em28xx_adap_template;
	dev->i2c_adap[bus].dev.parent = &dev->intf->dev;
	strscpy(dev->i2c_adap[bus].name, dev_name(&dev->intf->dev),
		sizeof(dev->i2c_adap[bus].name));

	dev->i2c_bus[bus].bus = bus;
	dev->i2c_bus[bus].algo_type = algo_type;
	dev->i2c_bus[bus].dev = dev;
	dev->i2c_adap[bus].algo_data = &dev->i2c_bus[bus];

	retval = i2c_add_adapter(&dev->i2c_adap[bus]);
	if (retval < 0) {
		dev_err(&dev->intf->dev,
			"%s: i2c_add_adapter failed! retval [%d]\n",
			__func__, retval);
		return retval;
	}

	dev->i2c_client[bus] = em28xx_client_template;
	dev->i2c_client[bus].adapter = &dev->i2c_adap[bus];

	/* Up to now, all eeproms are at bus 0 */
	if (!bus) {
		retval = em28xx_i2c_eeprom(dev, bus,
					   &dev->eedata, &dev->eedata_len);
		if (retval < 0 && retval != -ENODEV) {
			dev_err(&dev->intf->dev,
				"%s: em28xx_i2_eeprom failed! retval [%d]\n",
				__func__, retval);
		}
	}

	if (i2c_scan)
		em28xx_do_i2c_scan(dev, bus);

	return 0;
}

/*
 * em28xx_i2c_unregister()
 * unregister i2c_bus
 */
int em28xx_i2c_unregister(struct em28xx *dev, unsigned int bus)
{
	if (bus >= NUM_I2C_BUSES)
		return -ENODEV;

	i2c_del_adapter(&dev->i2c_adap[bus]);
	return 0;
}
