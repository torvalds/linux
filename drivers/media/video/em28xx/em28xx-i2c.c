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


#define dprintk1(lvl, fmt, args...)			\
do {							\
	if (i2c_debug >= lvl) {				\
	printk(fmt, ##args);				\
      }							\
} while (0)

#define dprintk2(lvl, fmt, args...)			\
do {							\
	if (i2c_debug >= lvl) {				\
		printk(KERN_DEBUG "%s at %s: " fmt,	\
		       dev->name, __func__ , ##args);	\
      } 						\
} while (0)

/*
 * em2800_i2c_send_max4()
 * send up to 4 bytes to the i2c device
 */
static int em2800_i2c_send_max4(struct em28xx *dev, unsigned char addr,
				char *buf, int len)
{
	int ret;
	int write_timeout;
	unsigned char b2[6];
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

	ret = dev->em28xx_write_regs(dev, 4 - len, &b2[4 - len], 2 + len);
	if (ret != 2 + len) {
		em28xx_warn("writing to i2c device failed (error=%i)\n", ret);
		return -EIO;
	}
	for (write_timeout = EM2800_I2C_WRITE_TIMEOUT; write_timeout > 0;
	     write_timeout -= 5) {
		ret = dev->em28xx_read_reg(dev, 0x05);
		if (ret == 0x80 + len - 1)
			return len;
		msleep(5);
	}
	em28xx_warn("i2c write timed out\n");
	return -EIO;
}

/*
 * em2800_i2c_send_bytes()
 */
static int em2800_i2c_send_bytes(void *data, unsigned char addr, char *buf,
				 short len)
{
	char *bufPtr = buf;
	int ret;
	int wrcount = 0;
	int count;
	int maxLen = 4;
	struct em28xx *dev = (struct em28xx *)data;
	while (len > 0) {
		count = (len > maxLen) ? maxLen : len;
		ret = em2800_i2c_send_max4(dev, addr, bufPtr, count);
		if (ret > 0) {
			len -= count;
			bufPtr += count;
			wrcount += count;
		} else
			return (ret < 0) ? ret : -EFAULT;
	}
	return wrcount;
}

/*
 * em2800_i2c_check_for_device()
 * check if there is a i2c_device at the supplied address
 */
static int em2800_i2c_check_for_device(struct em28xx *dev, unsigned char addr)
{
	char msg;
	int ret;
	int write_timeout;
	msg = addr;
	ret = dev->em28xx_write_regs(dev, 0x04, &msg, 1);
	if (ret < 0) {
		em28xx_warn("setting i2c device address failed (error=%i)\n",
			    ret);
		return ret;
	}
	msg = 0x84;
	ret = dev->em28xx_write_regs(dev, 0x05, &msg, 1);
	if (ret < 0) {
		em28xx_warn("preparing i2c read failed (error=%i)\n", ret);
		return ret;
	}
	for (write_timeout = EM2800_I2C_WRITE_TIMEOUT; write_timeout > 0;
	     write_timeout -= 5) {
		unsigned reg = dev->em28xx_read_reg(dev, 0x5);

		if (reg == 0x94)
			return -ENODEV;
		else if (reg == 0x84)
			return 0;
		msleep(5);
	}
	return -ENODEV;
}

/*
 * em2800_i2c_recv_bytes()
 * read from the i2c device
 */
static int em2800_i2c_recv_bytes(struct em28xx *dev, unsigned char addr,
				 char *buf, int len)
{
	int ret;
	/* check for the device and set i2c read address */
	ret = em2800_i2c_check_for_device(dev, addr);
	if (ret) {
		em28xx_warn
		    ("preparing read at i2c address 0x%x failed (error=%i)\n",
		     addr, ret);
		return ret;
	}
	ret = dev->em28xx_read_reg_req_len(dev, 0x0, 0x3, buf, len);
	if (ret < 0) {
		em28xx_warn("reading from i2c device at 0x%x failed (error=%i)",
			    addr, ret);
		return ret;
	}
	return ret;
}

/*
 * em28xx_i2c_send_bytes()
 * untested for more than 4 bytes
 */
static int em28xx_i2c_send_bytes(void *data, unsigned char addr, char *buf,
				 short len, int stop)
{
	int wrcount = 0;
	struct em28xx *dev = (struct em28xx *)data;

	wrcount = dev->em28xx_write_regs_req(dev, stop ? 2 : 3, addr, buf, len);

	return wrcount;
}

/*
 * em28xx_i2c_recv_bytes()
 * read a byte from the i2c device
 */
static int em28xx_i2c_recv_bytes(struct em28xx *dev, unsigned char addr,
				 char *buf, int len)
{
	int ret;
	ret = dev->em28xx_read_reg_req_len(dev, 2, addr, buf, len);
	if (ret < 0) {
		em28xx_warn("reading i2c device failed (error=%i)\n", ret);
		return ret;
	}
	if (dev->em28xx_read_reg(dev, 0x5) != 0)
		return -ENODEV;
	return ret;
}

/*
 * em28xx_i2c_check_for_device()
 * check if there is a i2c_device at the supplied address
 */
static int em28xx_i2c_check_for_device(struct em28xx *dev, unsigned char addr)
{
	char msg;
	int ret;
	msg = addr;

	ret = dev->em28xx_read_reg_req(dev, 2, addr);
	if (ret < 0) {
		em28xx_warn("reading from i2c device failed (error=%i)\n", ret);
		return ret;
	}
	if (dev->em28xx_read_reg(dev, 0x5) != 0)
		return -ENODEV;
	return 0;
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
		dprintk2(2, "%s %s addr=%x len=%d:",
			 (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			 i == num - 1 ? "stop" : "nonstop", addr, msgs[i].len);
		if (!msgs[i].len) { /* no len: check only for device presence */
			if (dev->board.is_em2800)
				rc = em2800_i2c_check_for_device(dev, addr);
			else
				rc = em28xx_i2c_check_for_device(dev, addr);
			if (rc < 0) {
				dprintk2(2, " no device\n");
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
			if (i2c_debug >= 2) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
		} else {
			/* write bytes */
			if (i2c_debug >= 2) {
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
		if (rc < 0)
			goto err;
		if (i2c_debug >= 2)
			printk("\n");
	}

	return num;
err:
	dprintk2(2, " ERROR: %i\n", rc);
	return rc;
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

static int em28xx_i2c_eeprom(struct em28xx *dev, unsigned char *eedata, int len)
{
	unsigned char buf, *p = eedata;
	struct em28xx_eeprom *em_eeprom = (void *)eedata;
	int i, err, size = len, block;

	if (dev->chip_id == CHIP_ID_EM2874) {
		/* Empia switched to a 16-bit addressable eeprom in newer
		   devices.  While we could certainly write a routine to read
		   the eeprom, there is nothing of use in there that cannot be
		   accessed through registers, and there is the risk that we
		   could corrupt the eeprom (since a 16-bit read call is
		   interpreted as a write call by 8-bit eeproms).
		*/
		return 0;
	}

	dev->i2c_client.addr = 0xa0 >> 1;

	/* Check if board has eeprom */
	err = i2c_master_recv(&dev->i2c_client, &buf, 0);
	if (err < 0) {
		em28xx_errdev("board has no eeprom\n");
		memset(eedata, 0, len);
		return -ENODEV;
	}

	buf = 0;

	err = i2c_master_send(&dev->i2c_client, &buf, 1);
	if (err != 1) {
		printk(KERN_INFO "%s: Huh, no eeprom present (err=%d)?\n",
		       dev->name, err);
		return err;
	}
	while (size > 0) {
		if (size > 16)
			block = 16;
		else
			block = size;

		if (block !=
		    (err = i2c_master_recv(&dev->i2c_client, p, block))) {
			printk(KERN_WARNING
			       "%s: i2c eeprom read error (err=%d)\n",
			       dev->name, err);
			return err;
		}
		size -= block;
		p += block;
	}
	for (i = 0; i < len; i++) {
		if (0 == (i % 16))
			printk(KERN_INFO "%s: i2c eeprom %02x:", dev->name, i);
		printk(" %02x", eedata[i]);
		if (15 == (i % 16))
			printk("\n");
	}

	if (em_eeprom->id == 0x9567eb1a)
		dev->hash = em28xx_hash_mem(eedata, len, 32);

	printk(KERN_INFO "%s: EEPROM ID= 0x%08x, EEPROM hash = 0x%08lx\n",
	       dev->name, em_eeprom->id, dev->hash);

	printk(KERN_INFO "%s: EEPROM info:\n", dev->name);

	switch (em_eeprom->chip_conf >> 4 & 0x3) {
	case 0:
		printk(KERN_INFO "%s:\tNo audio on board.\n", dev->name);
		break;
	case 1:
		printk(KERN_INFO "%s:\tAC97 audio (5 sample rates)\n",
				 dev->name);
		break;
	case 2:
		printk(KERN_INFO "%s:\tI2S audio, sample rate=32k\n",
				 dev->name);
		break;
	case 3:
		printk(KERN_INFO "%s:\tI2S audio, 3 sample rates\n",
				 dev->name);
		break;
	}

	if (em_eeprom->chip_conf & 1 << 3)
		printk(KERN_INFO "%s:\tUSB Remote wakeup capable\n", dev->name);

	if (em_eeprom->chip_conf & 1 << 2)
		printk(KERN_INFO "%s:\tUSB Self power capable\n", dev->name);

	switch (em_eeprom->chip_conf & 0x3) {
	case 0:
		printk(KERN_INFO "%s:\t500mA max power\n", dev->name);
		break;
	case 1:
		printk(KERN_INFO "%s:\t400mA max power\n", dev->name);
		break;
	case 2:
		printk(KERN_INFO "%s:\t300mA max power\n", dev->name);
		break;
	case 3:
		printk(KERN_INFO "%s:\t200mA max power\n", dev->name);
		break;
	}
	printk(KERN_INFO "%s:\tTable at 0x%02x, strings=0x%04x, 0x%04x, 0x%04x\n",
				dev->name,
				em_eeprom->string_idx_table,
				em_eeprom->string1,
				em_eeprom->string2,
				em_eeprom->string3);

	return 0;
}

/* ----------------------------------------------------------- */

/*
 * functionality()
 */
static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
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
	[0x4a >> 1] = "saa7113h",
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
		printk(KERN_INFO "%s: found i2c device @ 0x%x [%s]\n",
		       dev->name, i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
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

	retval = em28xx_i2c_eeprom(dev, dev->eedata, sizeof(dev->eedata));
	if ((retval < 0) && (retval != -ENODEV)) {
		em28xx_errdev("%s: em28xx_i2_eeprom failed! retval [%d]\n",
			__func__, retval);

		return retval;
	}

	if (i2c_scan)
		em28xx_do_i2c_scan(dev);

	/* Instantiate the IR receiver device, if present */
	em28xx_register_i2c_ir(dev);

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
