/*
   em28xx-i2c.c - driver for Empia EM2800/EM2820/2840 USB video capture devices

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
		      Sascha Sommer <saschasommer@freenet.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/i2c.h>

#include "em28xx.h"
#include "tuner-xc2028.h"
#include <media/v4l2-common.h>
#include <media/tuner.h>

/* ----------------------------------------------------------- */

static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

/*
 * em2800_i2c_send_bytes()
 * send up to 4 bytes to the em2800 i2c device
 */
static int em2800_i2c_send_bytes(struct em28xx *dev, u8 addr, u8 *buf, u16 len)
{
	int ret;
	int write_timeout;
	u8 b2[6];

	if (len < 1 || len > 4)
		return -EOPNOTSUPP;

	BUG_ON(len < 1 || len > 4);
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
		em28xx_warn("failed to trigger write to i2c address 0x%x "
			    "(error=%i)\n", addr, ret);
		return (ret < 0) ? ret : -EIO;
	}
	/* wait for completion */
	for (write_timeout = EM2800_I2C_XFER_TIMEOUT; write_timeout > 0;
	     write_timeout -= 5) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0x80 + len - 1) {
			return len;
		} else if (ret == 0x94 + len - 1) {
			return -ENODEV;
		} else if (ret < 0) {
			em28xx_warn("failed to get i2c transfer status from "
				    "bridge register (error=%i)\n", ret);
			return ret;
		}
		msleep(5);
	}
	em28xx_warn("write to i2c device at 0x%x timed out\n", addr);
	return -EIO;
}

/*
 * em2800_i2c_recv_bytes()
 * read up to 4 bytes from the em2800 i2c device
 */
static int em2800_i2c_recv_bytes(struct em28xx *dev, u8 addr, u8 *buf, u16 len)
{
	u8 buf2[4];
	int ret;
	int read_timeout;
	int i;

	if (len < 1 || len > 4)
		return -EOPNOTSUPP;

	/* trigger read */
	buf2[1] = 0x84 + len - 1;
	buf2[0] = addr;
	ret = dev->em28xx_write_regs(dev, 0x04, buf2, 2);
	if (ret != 2) {
		em28xx_warn("failed to trigger read from i2c address 0x%x "
			    "(error=%i)\n", addr, ret);
		return (ret < 0) ? ret : -EIO;
	}

	/* wait for completion */
	for (read_timeout = EM2800_I2C_XFER_TIMEOUT; read_timeout > 0;
	     read_timeout -= 5) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0x84 + len - 1) {
			break;
		} else if (ret == 0x94 + len - 1) {
			return -ENODEV;
		} else if (ret < 0) {
			em28xx_warn("failed to get i2c transfer status from "
				    "bridge register (error=%i)\n", ret);
			return ret;
		}
		msleep(5);
	}
	if (ret != 0x84 + len - 1)
		em28xx_warn("read from i2c device at 0x%x timed out\n", addr);

	/* get the received message */
	ret = dev->em28xx_read_reg_req_len(dev, 0x00, 4-len, buf2, len);
	if (ret != len) {
		em28xx_warn("reading from i2c device at 0x%x failed: "
			    "couldn't get the received message from the bridge "
			    "(error=%i)\n", addr, ret);
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
	int write_timeout, ret;

	if (len < 1 || len > 64)
		return -EOPNOTSUPP;
	/* NOTE: limited by the USB ctrl message constraints
	 * Zero length reads always succeed, even if no device is connected */

	/* Write to i2c device */
	ret = dev->em28xx_write_regs_req(dev, stop ? 2 : 3, addr, buf, len);
	if (ret != len) {
		if (ret < 0) {
			em28xx_warn("writing to i2c device at 0x%x failed "
				    "(error=%i)\n", addr, ret);
			return ret;
		} else {
			em28xx_warn("%i bytes write to i2c device at 0x%x "
				    "requested, but %i bytes written\n",
				    len, addr, ret);
			return -EIO;
		}
	}

	/* Check success of the i2c operation */
	for (write_timeout = EM2800_I2C_XFER_TIMEOUT; write_timeout > 0;
	     write_timeout -= 5) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0) { /* success */
			return len;
		} else if (ret == 0x10) {
			return -ENODEV;
		} else if (ret < 0) {
			em28xx_warn("failed to read i2c transfer status from "
				    "bridge (error=%i)\n", ret);
			return ret;
		}
		msleep(5);
		/* NOTE: do we really have to wait for success ?
		   Never seen anything else than 0x00 or 0x10
		   (even with high payload) ...			*/
	}
	em28xx_warn("write to i2c device at 0x%x timed out\n", addr);
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
	/* NOTE: limited by the USB ctrl message constraints
	 * Zero length reads always succeed, even if no device is connected */

	/* Read data from i2c device */
	ret = dev->em28xx_read_reg_req_len(dev, 2, addr, buf, len);
	if (ret != len) {
		if (ret < 0) {
			em28xx_warn("reading from i2c device at 0x%x failed "
				    "(error=%i)\n", addr, ret);
			return ret;
		} else {
			em28xx_warn("%i bytes requested from i2c device at "
				    "0x%x, but %i bytes received\n",
				    len, addr, ret);
			return -EIO;
		}
	}

	/* Check success of the i2c operation */
	ret = dev->em28xx_read_reg(dev, 0x05);
	if (ret < 0) {
		em28xx_warn("failed to read i2c transfer status from "
			    "bridge (error=%i)\n", ret);
		return ret;
	}
	if (ret > 0) {
		if (ret == 0x10) {
			return -ENODEV;
		} else {
			em28xx_warn("unknown i2c error (status=%i)\n", ret);
			return -EIO;
		}
	}
	return len;
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
 * em28xx_i2c_xfer()
 * the main i2c transfer function
 */
static int em28xx_i2c_xfer(struct i2c_adapter *i2c_adap,
			   struct i2c_msg msgs[], int num)
{
	struct em28xx *dev = i2c_adap->algo_data;
	int addr, rc, i, byte;

	if (num <= 0)
		return 0;
	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		if (i2c_debug)
			printk(KERN_DEBUG "%s at %s: %s %s addr=%02x len=%d:",
			       dev->name, __func__ ,
			       (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			       i == num - 1 ? "stop" : "nonstop",
			       addr, msgs[i].len);
		if (!msgs[i].len) { /* no len: check only for device presence */
			if (dev->board.is_em2800)
				rc = em2800_i2c_check_for_device(dev, addr);
			else
				rc = em28xx_i2c_check_for_device(dev, addr);
			if (rc == -ENODEV) {
				if (i2c_debug)
					printk(" no device\n");
				return rc;
			}
		} else if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */
			if (dev->board.is_em2800)
				rc = em2800_i2c_recv_bytes(dev, addr,
							   msgs[i].buf,
							   msgs[i].len);
			else
				rc = em28xx_i2c_recv_bytes(dev, addr,
							   msgs[i].buf,
							   msgs[i].len);
			if (i2c_debug) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
		} else {
			/* write bytes */
			if (i2c_debug) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
			if (dev->board.is_em2800)
				rc = em2800_i2c_send_bytes(dev, addr,
							   msgs[i].buf,
							   msgs[i].len);
			else
				rc = em28xx_i2c_send_bytes(dev, addr,
							   msgs[i].buf,
							   msgs[i].len,
							   i == num - 1);
		}
		if (rc < 0) {
			if (i2c_debug)
				printk(" ERROR: %i\n", rc);
			return rc;
		}
		if (i2c_debug)
			printk("\n");
	}

	return num;
}

/* based on linux/sunrpc/svcauth.h and linux/hash.h
 * The original hash function returns a different value, if arch is x86_64
 *  or i386.
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
		} else
			c = *buf++;
		l = (l << 8) | c;
		len++;
		if ((len & (32 / 8 - 1)) == 0)
			hash = ((hash^l) * 0x9e370001UL);
	} while (len);

	return (hash >> (32 - bits)) & 0xffffffffUL;
}

/* Helper function to read data blocks from i2c clients with 8 or 16 bit
 * address width, 8 bit register width and auto incrementation been activated */
static int em28xx_i2c_read_block(struct em28xx *dev, u16 addr, bool addr_w16,
				 u16 len, u8 *data)
{
	int remain = len, rsize, rsize_max, ret;
	u8 buf[2];

	/* Sanity check */
	if (addr + remain > (addr_w16 * 0xff00 + 0xff + 1))
		return -EINVAL;
	/* Select address */
	buf[0] = addr >> 8;
	buf[1] = addr & 0xff;
	ret = i2c_master_send(&dev->i2c_client, buf + !addr_w16, 1 + addr_w16);
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

		ret = i2c_master_recv(&dev->i2c_client, data, rsize);
		if (ret < 0)
			return ret;

		remain -= rsize;
		data += rsize;
	}

	return len;
}

static int em28xx_i2c_eeprom(struct em28xx *dev, u8 **eedata, u16 *eedata_len)
{
	const u16 len = 256;
	/* FIXME common length/size for bytes to read, to display, hash
	 * calculation and returned device dataset. Simplifies the code a lot,
	 * but we might have to deal with multiple sizes in the future !      */
	int i, err;
	struct em28xx_eeprom *dev_config;
	u8 buf, *data;

	*eedata = NULL;
	*eedata_len = 0;

	dev->i2c_client.addr = 0xa0 >> 1;

	/* Check if board has eeprom */
	err = i2c_master_recv(&dev->i2c_client, &buf, 0);
	if (err < 0) {
		em28xx_info("board has no eeprom\n");
		return -ENODEV;
	}

	data = kzalloc(len, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	/* Read EEPROM content */
	err = em28xx_i2c_read_block(dev, 0x0000, dev->eeprom_addrwidth_16bit,
				    len, data);
	if (err != len) {
		em28xx_errdev("failed to read eeprom (err=%d)\n", err);
		goto error;
	}

	/* Display eeprom content */
	for (i = 0; i < len; i++) {
		if (0 == (i % 16)) {
			if (dev->eeprom_addrwidth_16bit)
				em28xx_info("i2c eeprom %04x:", i);
			else
				em28xx_info("i2c eeprom %02x:", i);
		}
		printk(" %02x", data[i]);
		if (15 == (i % 16))
			printk("\n");
	}
	if (dev->eeprom_addrwidth_16bit)
		em28xx_info("i2c eeprom %04x: ... (skipped)\n", i);

	if (dev->eeprom_addrwidth_16bit &&
	    data[0] == 0x26 && data[3] == 0x00) {
		/* new eeprom format; size 4-64kb */
		u16 mc_start;
		u16 hwconf_offset;

		dev->hash = em28xx_hash_mem(data, len, 32);
		mc_start = (data[1] << 8) + 4;	/* usually 0x0004 */

		em28xx_info("EEPROM ID = %02x %02x %02x %02x, "
			    "EEPROM hash = 0x%08lx\n",
			    data[0], data[1], data[2], data[3], dev->hash);
		em28xx_info("EEPROM info:\n");
		em28xx_info("\tmicrocode start address = 0x%04x, "
			    "boot configuration = 0x%02x\n",
			    mc_start, data[2]);
		/* boot configuration (address 0x0002):
		 * [0]   microcode download speed: 1 = 400 kHz; 0 = 100 kHz
		 * [1]   always selects 12 kb RAM
		 * [2]   USB device speed: 1 = force Full Speed; 0 = auto detect
		 * [4]   1 = force fast mode and no suspend for device testing
		 * [5:7] USB PHY tuning registers; determined by device
		 *       characterization
		 */

		/* Read hardware config dataset offset from address
		 * (microcode start + 46)			    */
		err = em28xx_i2c_read_block(dev, mc_start + 46, 1, 2, data);
		if (err != 2) {
			em28xx_errdev("failed to read hardware configuration data from eeprom (err=%d)\n",
				      err);
			goto error;
		}

		/* Calculate hardware config dataset start address */
		hwconf_offset = mc_start + data[0] + (data[1] << 8);

		/* Read hardware config dataset */
		/* NOTE: the microcode copy can be multiple pages long, but
		 * we assume the hardware config dataset is the same as in
		 * the old eeprom and not longer than 256 bytes.
		 * tveeprom is currently also limited to 256 bytes.
		 */
		err = em28xx_i2c_read_block(dev, hwconf_offset, 1, len, data);
		if (err != len) {
			em28xx_errdev("failed to read hardware configuration data from eeprom (err=%d)\n",
				      err);
			goto error;
		}

		/* Verify hardware config dataset */
		/* NOTE: not all devices provide this type of dataset */
		if (data[0] != 0x1a || data[1] != 0xeb ||
		    data[2] != 0x67 || data[3] != 0x95) {
			em28xx_info("\tno hardware configuration dataset found in eeprom\n");
			kfree(data);
			return 0;
		}

		/* TODO: decrypt eeprom data for camera bridges (em25xx, em276x+) */

	} else if (!dev->eeprom_addrwidth_16bit &&
		   data[0] == 0x1a && data[1] == 0xeb &&
		   data[2] == 0x67 && data[3] == 0x95) {
		dev->hash = em28xx_hash_mem(data, len, 32);
		em28xx_info("EEPROM ID = %02x %02x %02x %02x, "
			    "EEPROM hash = 0x%08lx\n",
			    data[0], data[1], data[2], data[3], dev->hash);
		em28xx_info("EEPROM info:\n");
	} else {
		em28xx_info("unknown eeprom format or eeprom corrupted !\n");
		err = -ENODEV;
		goto error;
	}

	*eedata = data;
	*eedata_len = len;
	dev_config = (void *)eedata;

	switch (le16_to_cpu(dev_config->chip_conf) >> 4 & 0x3) {
	case 0:
		em28xx_info("\tNo audio on board.\n");
		break;
	case 1:
		em28xx_info("\tAC97 audio (5 sample rates)\n");
		break;
	case 2:
		em28xx_info("\tI2S audio, sample rate=32k\n");
		break;
	case 3:
		em28xx_info("\tI2S audio, 3 sample rates\n");
		break;
	}

	if (le16_to_cpu(dev_config->chip_conf) & 1 << 3)
		em28xx_info("\tUSB Remote wakeup capable\n");

	if (le16_to_cpu(dev_config->chip_conf) & 1 << 2)
		em28xx_info("\tUSB Self power capable\n");

	switch (le16_to_cpu(dev_config->chip_conf) & 0x3) {
	case 0:
		em28xx_info("\t500mA max power\n");
		break;
	case 1:
		em28xx_info("\t400mA max power\n");
		break;
	case 2:
		em28xx_info("\t300mA max power\n");
		break;
	case 3:
		em28xx_info("\t200mA max power\n");
		break;
	}
	em28xx_info("\tTable at offset 0x%02x, strings=0x%04x, 0x%04x, 0x%04x\n",
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
static u32 functionality(struct i2c_adapter *adap)
{
	struct em28xx *dev = adap->algo_data;
	u32 func_flags = I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
	if (dev->board.is_em2800)
		func_flags &= ~I2C_FUNC_SMBUS_WRITE_BLOCK_DATA;
	return func_flags;
}

static struct i2c_algorithm em28xx_algo = {
	.master_xfer   = em28xx_i2c_xfer,
	.functionality = functionality,
};

static struct i2c_adapter em28xx_adap_template = {
	.owner = THIS_MODULE,
	.name = "em28xx",
	.algo = &em28xx_algo,
};

static struct i2c_client em28xx_client_template = {
	.name = "em28xx internal",
};

/* ----------------------------------------------------------- */

/*
 * i2c_devs
 * incomplete list of known devices
 */
static char *i2c_devs[128] = {
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
void em28xx_do_i2c_scan(struct em28xx *dev)
{
	u8 i2c_devicelist[128];
	unsigned char buf;
	int i, rc;

	memset(i2c_devicelist, 0, ARRAY_SIZE(i2c_devicelist));

	for (i = 0; i < ARRAY_SIZE(i2c_devs); i++) {
		dev->i2c_client.addr = i;
		rc = i2c_master_recv(&dev->i2c_client, &buf, 0);
		if (rc < 0)
			continue;
		i2c_devicelist[i] = i;
		em28xx_info("found i2c device @ 0x%x [%s]\n",
			    i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}

	dev->i2c_hash = em28xx_hash_mem(i2c_devicelist,
					ARRAY_SIZE(i2c_devicelist), 32);
}

/*
 * em28xx_i2c_register()
 * register i2c bus
 */
int em28xx_i2c_register(struct em28xx *dev)
{
	int retval;

	BUG_ON(!dev->em28xx_write_regs || !dev->em28xx_read_reg);
	BUG_ON(!dev->em28xx_write_regs_req || !dev->em28xx_read_reg_req);
	dev->i2c_adap = em28xx_adap_template;
	dev->i2c_adap.dev.parent = &dev->udev->dev;
	strcpy(dev->i2c_adap.name, dev->name);
	dev->i2c_adap.algo_data = dev;
	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);

	retval = i2c_add_adapter(&dev->i2c_adap);
	if (retval < 0) {
		em28xx_errdev("%s: i2c_add_adapter failed! retval [%d]\n",
			__func__, retval);
		return retval;
	}

	dev->i2c_client = em28xx_client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	retval = em28xx_i2c_eeprom(dev, &dev->eedata, &dev->eedata_len);
	if ((retval < 0) && (retval != -ENODEV)) {
		em28xx_errdev("%s: em28xx_i2_eeprom failed! retval [%d]\n",
			__func__, retval);

		return retval;
	}

	if (i2c_scan)
		em28xx_do_i2c_scan(dev);

	return 0;
}

/*
 * em28xx_i2c_unregister()
 * unregister i2c_bus
 */
int em28xx_i2c_unregister(struct em28xx *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}
