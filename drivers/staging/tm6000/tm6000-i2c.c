/*
   tm6000-i2c.c - driver for TM5600/TM6000 USB video capture devices

   Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

   Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>
	- Fix SMBus Read Byte command

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

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

#include "tm6000.h"
#include "tm6000-regs.h"
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include "tuner-xc2028.h"


/*FIXME: Hack to avoid needing to patch i2c-id.h */
#define I2C_HW_B_TM6000 I2C_HW_B_EM28XX
/* ----------------------------------------------------------- */

static unsigned int i2c_scan = 0;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

static unsigned int i2c_debug = 0;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#define i2c_dprintk(lvl,fmt, args...) if (i2c_debug>=lvl) do{ \
			printk(KERN_DEBUG "%s at %s: " fmt, \
			dev->name, __FUNCTION__ , ##args); } while (0)


/* Returns 0 if address is found */
static int tm6000_i2c_scan(struct i2c_adapter *i2c_adap, int addr)
{
	struct tm6000_core *dev = i2c_adap->algo_data;

#if 1
	/* HACK: i2c scan is not working yet */
	if (
		(dev->caps.has_tuner   && (addr==dev->tuner_addr)) ||
		(dev->caps.has_tda9874 && (addr==0xb0)) ||
		(dev->caps.has_eeprom  && (addr==0xa0))
	   ) {
		printk("Hack: enabling device at addr 0x%02x\n",addr);
		return (1);
	} else {
		return -ENODEV;
	}
#else
	int rc=-ENODEV;
	char buf[1];

	/* This sends addr + 1 byte with 0 */
	rc = tm6000_read_write_usb (dev,
		USB_DIR_IN | USB_TYPE_VENDOR,
		REQ_16_SET_GET_I2C_WR1_RDN,
		addr, 0,
		buf, 0);
	msleep(10);

	if (rc<0) {
		if (i2c_debug>=2)
			printk("no device at addr 0x%02x\n",addr);
	}

	printk("Hack: check on addr 0x%02x returned %d\n",addr,rc);

	return rc;
#endif
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
		i2c_dprintk(2,"%s %s addr=0x%x len=%d:",
			 (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			 i == num - 1 ? "stop" : "nonstop", addr, msgs[i].len);
		if (!msgs[i].len) {
			/* Do I2C scan */
			rc = tm6000_i2c_scan(i2c_adap, addr);
		} else if (msgs[i].flags & I2C_M_RD) {
			/* read request without preceding register selection */
			/*
			 * The TM6000 only supports a read transaction
			 * immediately after a 1 or 2 byte write to select
			 * a register.  We cannot fulfil this request.
			 */
			i2c_dprintk(2, " read without preceding write not"
				       " supported");
			rc = -EOPNOTSUPP;
			goto err;
		} else if (i + 1 < num && msgs[i].len <= 2 &&
			   (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {
			/* 1 or 2 byte write followed by a read */
			if (i2c_debug >= 2)
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			i2c_dprintk(2, "; joined to read %s len=%d:",
				    i == num - 2 ? "stop" : "nonstop",
				    msgs[i + 1].len);
			rc = tm6000_read_write_usb (dev,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				msgs[i].len == 1 ? REQ_16_SET_GET_I2C_WR1_RDN
						 : REQ_14_SET_GET_I2C_WR2_RDN,
				addr | msgs[i].buf[0] << 8,
				msgs[i].len == 1 ? 0 : msgs[i].buf[1],
				msgs[i + 1].buf, msgs[i + 1].len);
			i++;
			if (i2c_debug >= 2)
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
		} else {
			/* write bytes */
			if (i2c_debug >= 2)
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			rc = tm6000_read_write_usb(dev,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				REQ_16_SET_GET_I2C_WR1_RDN,
				addr | msgs[i].buf[0] << 8, 0,
				msgs[i].buf + 1, msgs[i].len - 1);
		}
		if (i2c_debug >= 2)
			printk("\n");
		if (rc < 0)
			goto err;
	}

	return num;
err:
	i2c_dprintk(2," ERROR: %i\n", rc);
	return rc;
}

static int tm6000_i2c_eeprom(struct tm6000_core *dev,
			     unsigned char *eedata, int len)
{
	int i, rc;
	unsigned char *p = eedata;
	unsigned char bytes[17];

	dev->i2c_client.addr = 0xa0 >> 1;

	bytes[16] = '\0';
	for (i = 0; i < len; ) {
	*p = i;
	rc = tm6000_read_write_usb (dev,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		REQ_16_SET_GET_I2C_WR1_RDN, 0xa0 | i<<8, 0, p, 1);
		if (rc < 1) {
			if (p == eedata)
				goto noeeprom;
			else {
				printk(KERN_WARNING
				"%s: i2c eeprom read error (err=%d)\n",
				dev->name, rc);
			}
			return -1;
		}
		p++;
		if (0 == (i % 16))
			printk(KERN_INFO "%s: i2c eeprom %02x:", dev->name, i);
		printk(" %02x", eedata[i]);
		if ((eedata[i] >= ' ') && (eedata[i] <= 'z')) {
			bytes[i%16] = eedata[i];
		} else {
			bytes[i%16]='.';
		}

		i++;

		if (0 == (i % 16)) {
			bytes[16] = '\0';
			printk("  %s\n", bytes);
		}
	}
	if (0 != (i%16)) {
		bytes[i%16] = '\0';
		for (i %= 16; i < 16; i++)
			printk("   ");
	}
	printk("  %s\n", bytes);

	return 0;

noeeprom:
	printk(KERN_INFO "%s: Huh, no eeprom present (err=%d)?\n",
		       dev->name, rc);
	return rc;
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

#define mass_write(addr, reg, data...)					\
	{ const static u8 _val[] = data;				\
	rc=tm6000_read_write_usb(dev,USB_DIR_OUT | USB_TYPE_VENDOR,	\
	REQ_16_SET_GET_I2C_WR1_RDN,(reg<<8)+addr, 0x00, (u8 *) _val,	\
	ARRAY_SIZE(_val));						\
	if (rc<0) {							\
		printk(KERN_ERR "Error on line %d: %d\n",__LINE__,rc);	\
		return rc;						\
	}								\
	msleep (10);							\
	}

/* Tuner callback to provide the proper gpio changes needed for xc2028 */

static int tm6000_tuner_callback(void *ptr, int command, int arg)
{
	int rc=0;
	struct tm6000_core *dev = ptr;

	if (dev->tuner_type!=TUNER_XC2028)
		return 0;

	switch (command) {
	case XC2028_RESET_CLK:
		tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT,
					0x02, arg);
		msleep(10);
		rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN,
					TM6000_GPIO_CLK, 0);
		if (rc<0)
			return rc;
		msleep(10);
		rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN,
					TM6000_GPIO_CLK, 1);
		break;
	case XC2028_TUNER_RESET:
		/* Reset codes during load firmware */
		switch (arg) {
		case 0:
			tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN,
					dev->tuner_reset_gpio, 0x00);
			msleep(130);
			tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN,
					dev->tuner_reset_gpio, 0x01);
			msleep(130);
			break;
		case 1:
			tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT,
						0x02, 0x01);
			msleep(10);
			break;

		case 2:
			rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN,
						TM6000_GPIO_CLK, 0);
			if (rc<0)
				return rc;
			msleep(100);
			rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN,
						TM6000_GPIO_CLK, 1);
			msleep(100);
			break;
		}
	}
	return (rc);
}

static int attach_inform(struct i2c_client *client)
{
	struct tm6000_core *dev = client->adapter->algo_data;
	struct tuner_setup tun_setup;

	i2c_dprintk(1, "%s i2c attach [addr=0x%x,client=%s]\n",
		client->driver->driver.name, client->addr, client->name);

	switch (client->addr<<1) {
	case 0xb0:
		request_module("tvaudio");
		return 0;
	}

	/* If tuner, initialize the tuner part */
	if ( dev->tuner_addr != client->addr<<1 ) {
		return 0;
	}

	memset (&tun_setup, 0, sizeof(tun_setup));

	tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
	tun_setup.type = dev->tuner_type;
	tun_setup.addr = dev->tuner_addr>>1;
	tun_setup.tuner_callback = tm6000_tuner_callback;

	client->driver->command (client,TUNER_SET_TYPE_ADDR, &tun_setup);

	return 0;
}

static struct i2c_algorithm tm6000_algo = {
	.master_xfer   = tm6000_i2c_xfer,
	.algo_control  = algo_control,
	.functionality = functionality,
};

static struct i2c_adapter tm6000_adap_template = {
#ifdef I2C_PEC
	.owner = THIS_MODULE,
#else
	.inc_use = inc_use,
	.dec_use = dec_use,
#endif
	.class = I2C_CLASS_TV_ANALOG,
	.name = "tm6000",
	.id = I2C_HW_B_TM6000,
	.algo = &tm6000_algo,
	.client_register = attach_inform,
};

static struct i2c_client tm6000_client_template = {
	.name = "tm6000 internal",
};

/* ----------------------------------------------------------- */

/*
 * i2c_devs
 * incomplete list of known devices
 */
static char *i2c_devs[128] = {
	[0xc2 >> 1] = "tuner (analog)",
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
		printk(KERN_INFO "%s: found i2c device @ 0x%x [%s]\n", name,
		       i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/*
 * tm6000_i2c_call_clients()
 * send commands to all attached i2c devices
 */
void tm6000_i2c_call_clients(struct tm6000_core *dev, unsigned int cmd, void *arg)
{
	BUG_ON(NULL == dev->i2c_adap.algo_data);
	i2c_clients_command(&dev->i2c_adap, cmd, arg);
}

/*
 * tm6000_i2c_register()
 * register i2c bus
 */
int tm6000_i2c_register(struct tm6000_core *dev)
{
	unsigned char eedata[256];

	dev->i2c_adap = tm6000_adap_template;
	dev->i2c_adap.dev.parent = &dev->udev->dev;
	strcpy(dev->i2c_adap.name, dev->name);
	dev->i2c_adap.algo_data = dev;
	i2c_add_adapter(&dev->i2c_adap);

	dev->i2c_client = tm6000_client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	if (i2c_scan)
		do_i2c_scan(dev->name, &dev->i2c_client);

	tm6000_i2c_eeprom(dev, eedata, sizeof(eedata));

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
