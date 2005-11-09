/*
   em2820-i2c.c - driver for Empia EM2820/2840 USB video capture devices

   Copyright (C) 2005 Markus Rechberger <mrechberger@gmail.com>
                      Ludovico Cavedon <cavedon@sssup.it>
                      Mauro Carvalho Chehab <mchehab@brturbo.com.br>

   Based on the em2800 driver from Sascha Sommer <saschasommer@freenet.de>

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
#include <linux/videodev.h>
#include <media/tuner.h>
#include <linux/video_decoder.h>

/* To be moved to compat.h */
#if !defined(I2C_HW_B_EM2820)
#define I2C_HW_B_EM2820 I2C_HW_B_BT848
#endif

#include "em2820.h"

/* ----------------------------------------------------------- */

static unsigned int i2c_scan = 0;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

static unsigned int i2c_debug = 0;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#define dprintk(fmt, args...) if (i2c_debug) do {\
			printk(KERN_DEBUG "%s: %s: " fmt "\n",\
			dev->name, __FUNCTION__ , ##args); } while (0)
#define dprintk1(fmt, args...) if (i2c_debug) do{ \
			printk(KERN_DEBUG "%s: %s: " fmt, \
			dev->name, __FUNCTION__ , ##args); } while (0)
#define dprintk2(fmt, args...) if (i2c_debug) do {\
			printk(fmt , ##args); } while (0)

/*
 * i2c_send_bytes()
 * untested for more than 4 bytes
 */
static int i2c_send_bytes(void *data, unsigned char addr, char *buf, short len,
			  int stop)
{
	int wrcount = 0;
	struct em2820 *dev = (struct em2820 *)data;

	wrcount = dev->em2820_write_regs_req(dev, stop ? 2 : 3, addr, buf, len);

	return wrcount;
}

/*
 * i2c_recv_byte()
 * read a byte from the i2c device
 */
static int i2c_recv_bytes(struct em2820 *dev, unsigned char addr, char *buf,
			  int len)
{
	int ret;
	ret = dev->em2820_read_reg_req_len(dev, 2, addr, buf, len);
	if (ret < 0) {
		em2820_warn("reading i2c device failed (error=%i)\n", ret);
		return ret;
	}
	if (dev->em2820_read_reg(dev, 0x5) != 0)
		return -ENODEV;
	return ret;
}

/*
 * i2c_check_for_device()
 * check if there is a i2c_device at the supplied address
 */
static int i2c_check_for_device(struct em2820 *dev, unsigned char addr)
{
	char msg;
	int ret;
	msg = addr;

	ret = dev->em2820_read_reg_req(dev, 2, addr);
	if (ret < 0) {
		em2820_warn("reading from i2c device failed (error=%i)\n", ret);
		return ret;
	}
	if (dev->em2820_read_reg(dev, 0x5) != 0)
		return -ENODEV;
	return 0;
}

/*
 * em2820_i2c_xfer()
 * the main i2c transfer function
 */
static int em2820_i2c_xfer(struct i2c_adapter *i2c_adap,
			   struct i2c_msg msgs[], int num)
{
	struct em2820 *dev = i2c_adap->algo_data;
	int addr, rc, i, byte;

	if (num <= 0)
		return 0;
	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		dprintk1("%s %s addr=%x len=%d:",
			 (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			 i == num - 1 ? "stop" : "nonstop", addr, msgs[i].len);
		if (!msgs[i].len) {	/* no len: check only for device presence */
			rc = i2c_check_for_device(dev, addr);
			if (rc < 0) {
				dprintk2(" no device\n");
				return rc;
			}

		}
		if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */

			rc = i2c_recv_bytes(dev, addr, msgs[i].buf,
					    msgs[i].len);
			if (i2c_debug) {
				for (byte = 0; byte < msgs[i].len; byte++) {
					printk(" %02x", msgs[i].buf[byte]);
				}
			}
		} else {
			/* write bytes */
			if (i2c_debug) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
			rc = i2c_send_bytes(dev, addr, msgs[i].buf, msgs[i].len,
					    i == num - 1);
			if (rc < 0)
				goto err;
		}
		if (i2c_debug)
			printk("\n");
	}

	return num;
      err:
	dprintk2(" ERROR: %i\n", rc);
	return rc;
}

static int em2820_i2c_eeprom(struct em2820 *dev, unsigned char *eedata, int len)
{
	unsigned char buf, *p = eedata;
	struct em2820_eeprom *em_eeprom = (void *)eedata;
	int i, err, size = len, block;

	dev->i2c_client.addr = 0xa0 >> 1;
	buf = 0;
	if (1 != (err = i2c_master_send(&dev->i2c_client, &buf, 1))) {
		printk(KERN_INFO "%s: Huh, no eeprom present (err=%d)?\n",
		       dev->name, err);
		return -1;
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
			return -1;
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

	printk(KERN_INFO "EEPROM ID= 0x%08x\n", em_eeprom->id);
	printk(KERN_INFO "Vendor/Product ID= %04x:%04x\n", em_eeprom->vendor_ID,
	       em_eeprom->product_ID);

	switch (em_eeprom->chip_conf >> 4 & 0x3) {
	case 0:
		printk(KERN_INFO "No audio on board.\n");
		break;
	case 1:
		printk(KERN_INFO "AC97 audio (5 sample rates)\n");
		break;
	case 2:
		printk(KERN_INFO "I2S audio, sample rate=32k\n");
		break;
	case 3:
		printk(KERN_INFO "I2S audio, 3 sample rates\n");
		break;
	}

	if (em_eeprom->chip_conf & 1 << 3)
		printk(KERN_INFO "USB Remote wakeup capable\n");

	if (em_eeprom->chip_conf & 1 << 2)
		printk(KERN_INFO "USB Self power capable\n");

	switch (em_eeprom->chip_conf & 0x3) {
	case 0:
		printk(KERN_INFO "500mA max power\n");
		break;
	case 1:
		printk(KERN_INFO "400mA max power\n");
		break;
	case 2:
		printk(KERN_INFO "300mA max power\n");
		break;
	case 3:
		printk(KERN_INFO "200mA max power\n");
		break;
	}

	return 0;
}

/* ----------------------------------------------------------- */

/*
 * algo_control()
 */
static int algo_control(struct i2c_adapter *adapter,
			unsigned int cmd, unsigned long arg)
{
	return 0;
}

/*
 * functionality()
 */
static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

#ifndef I2C_PEC
static void inc_use(struct i2c_adapter *adap)
{
	MOD_INC_USE_COUNT;
}

static void dec_use(struct i2c_adapter *adap)
{
	MOD_DEC_USE_COUNT;
}
#endif

static int em2820_set_tuner(int check_eeprom, struct i2c_client *client)
{
	struct em2820 *dev = client->adapter->algo_data;
	struct tuner_setup tun_setup;

	if (dev->has_tuner) {
		tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
		tun_setup.type = dev->tuner_type;
		tun_setup.addr = dev->tuner_addr;

		em2820_i2c_call_clients(dev, TUNER_SET_TYPE_ADDR, &tun_setup);
	}

	return (0);
}

/*
 * attach_inform()
 * gets called when a device attaches to the i2c bus
 * does some basic configuration
 */
static int attach_inform(struct i2c_client *client)
{
	struct em2820 *dev = client->adapter->algo_data;

	dprintk("address %x", client->addr << 1);
	switch (client->addr << 1) {
	case 0x68:
		em2820_i2c_call_clients(dev, TDA9887_SET_CONFIG, &dev->tda9887_conf);
		break;
	case 0x4a:
		dprintk1("attach_inform: saa7113 detected.\n");
		break;
	case 0xa0:
		dprintk1("attach_inform: eeprom detected.\n");
		break;
	case 0x80:
	case 0x88:
		dprintk1("attach_inform: msp34xx detected.\n");
		break;
	case 0xb8:
	case 0xba:
		dprintk1("attach_inform: tvp5150 detected.\n");
		break;
	default:
		dev->tuner_addr = client->addr;
		em2820_set_tuner(-1, client);
	}

	return 0;
}

static struct i2c_algorithm em2820_algo = {
	.master_xfer   = em2820_i2c_xfer,
	.algo_control  = algo_control,
	.functionality = functionality,
};

static struct i2c_adapter em2820_adap_template = {
#ifdef I2C_PEC
	.owner = THIS_MODULE,
#else
	.inc_use = inc_use,
	.dec_use = dec_use,
#endif
#ifdef I2C_CLASS_TV_ANALOG
	.class = I2C_CLASS_TV_ANALOG,
#endif
	.name = "em2820",
	.id = I2C_HW_B_EM2820,
	.algo = &em2820_algo,
	.client_register = attach_inform,
};

static struct i2c_client em2820_client_template = {
	.name = "em2820 internal",
	.flags = I2C_CLIENT_ALLOW_USE,
};

/* ----------------------------------------------------------- */

/*
 * i2c_devs
 * incomplete list of known devices
 */
static char *i2c_devs[128] = {
	[0x4a >> 1] = "saa7113h",
	[0x60 >> 1] = "remote IR sensor",
	[0x86 >> 1] = "tda9887",
	[0x80 >> 1] = "msp34xx",
	[0x88 >> 1] = "msp34xx",
	[0xa0 >> 1] = "eeprom",
	[0xb8 >> 1] = "tvp5150a",
	[0xba >> 1] = "tvp5150a",
	[0xc0 >> 1] = "tuner (analog)",
	[0xc2 >> 1] = "tuner (analog)",
	[0xc4 >> 1] = "tuner (analog)",
	[0xc6 >> 1] = "tuner (analog)",
};

/*
 * do_i2c_scan()
 * check i2c address range for devices
 */
static void do_i2c_scan(char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i, rc;

	for (i = 0; i < 128; i++) {
		c->addr = i;
		rc = i2c_master_recv(c, &buf, 0);
		if (rc < 0)
			continue;
		printk(KERN_INFO "%s: found device @ 0x%x [%s]", name,
		       i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/*
 * em2820_i2c_call_clients()
 * send commands to all attached i2c devices
 */
void em2820_i2c_call_clients(struct em2820 *dev, unsigned int cmd, void *arg)
{
	BUG_ON(NULL == dev->i2c_adap.algo_data);
	i2c_clients_command(&dev->i2c_adap, cmd, arg);
}

/*
 * em2820_i2c_register()
 * register i2c bus
 */
int em2820_i2c_register(struct em2820 *dev)
{
	BUG_ON(!dev->em2820_write_regs || !dev->em2820_read_reg);
	BUG_ON(!dev->em2820_write_regs_req || !dev->em2820_read_reg_req);
	dev->i2c_adap = em2820_adap_template;
	dev->i2c_adap.dev.parent = &dev->udev->dev;
	strcpy(dev->i2c_adap.name, dev->name);
	dev->i2c_adap.algo_data = dev;
	i2c_add_adapter(&dev->i2c_adap);

	dev->i2c_client = em2820_client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	em2820_i2c_eeprom(dev, dev->eedata, sizeof(dev->eedata));

	if (i2c_scan)
		do_i2c_scan(dev->name, &dev->i2c_client);
	return 0;
}

/*
 * em2820_i2c_unregister()
 * unregister i2c_bus
 */
int em2820_i2c_unregister(struct em2820 *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}
