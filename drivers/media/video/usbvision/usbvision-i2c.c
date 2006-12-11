/*
 * I2C_ALGO_USB.C
 *  i2c algorithm for USB-I2C Bridges
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *                         Dwaine Garden <dwainegarden@rogers.com>
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include "usbvision.h"

#define DBG_I2C		1<<0
#define DBG_ALGO	1<<1

static int i2c_debug = 0;

module_param (i2c_debug, int, 0644);			// debug_i2c_usb mode of the device driver
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#define PDEBUG(level, fmt, args...) \
		if (i2c_debug & (level)) info("[%s:%d] " fmt, __PRETTY_FUNCTION__, __LINE__ , ## args)

static int usbvision_i2c_write(void *data, unsigned char addr, char *buf,
			    short len);
static int usbvision_i2c_read(void *data, unsigned char addr, char *buf,
			   short len);

static inline int try_write_address(struct i2c_adapter *i2c_adap,
				    unsigned char addr, int retries)
{
	struct i2c_algo_usb_data *adap = i2c_adap->algo_data;
	void *data;
	int i, ret = -1;
	char buf[4];

	data = i2c_get_adapdata(i2c_adap);
	buf[0] = 0x00;
	for (i = 0; i <= retries; i++) {
		ret = (usbvision_i2c_write(data, addr, buf, 1));
		if (ret == 1)
			break;	/* success! */
		udelay(5 /*adap->udelay */ );
		if (i == retries)	/* no success */
			break;
		udelay(adap->udelay);
	}
	if (i) {
		PDEBUG(DBG_ALGO,"Needed %d retries for address %#2x", i, addr);
		PDEBUG(DBG_ALGO,"Maybe there's no device at this address");
	}
	return ret;
}

static inline int try_read_address(struct i2c_adapter *i2c_adap,
				   unsigned char addr, int retries)
{
	struct i2c_algo_usb_data *adap = i2c_adap->algo_data;
	void *data;
	int i, ret = -1;
	char buf[4];

	data = i2c_get_adapdata(i2c_adap);
	for (i = 0; i <= retries; i++) {
		ret = (usbvision_i2c_read(data, addr, buf, 1));
		if (ret == 1)
			break;	/* success! */
		udelay(5 /*adap->udelay */ );
		if (i == retries)	/* no success */
			break;
		udelay(adap->udelay);
	}
	if (i) {
		PDEBUG(DBG_ALGO,"Needed %d retries for address %#2x", i, addr);
		PDEBUG(DBG_ALGO,"Maybe there's no device at this address");
	}
	return ret;
}

static inline int usb_find_address(struct i2c_adapter *i2c_adap,
				   struct i2c_msg *msg, int retries,
				   unsigned char *add)
{
	unsigned short flags = msg->flags;

	unsigned char addr;
	int ret;
	if ((flags & I2C_M_TEN)) {
		/* a ten bit address */
		addr = 0xf0 | ((msg->addr >> 7) & 0x03);
		/* try extended address code... */
		ret = try_write_address(i2c_adap, addr, retries);
		if (ret != 1) {
			err("died at extended address code, while writing");
			return -EREMOTEIO;
		}
		add[0] = addr;
		if (flags & I2C_M_RD) {
			/* okay, now switch into reading mode */
			addr |= 0x01;
			ret = try_read_address(i2c_adap, addr, retries);
			if (ret != 1) {
				err("died at extended address code, while reading");
				return -EREMOTEIO;
			}
		}

	} else {		/* normal 7bit address  */
		addr = (msg->addr << 1);
		if (flags & I2C_M_RD)
			addr |= 1;
		if (flags & I2C_M_REV_DIR_ADDR)
			addr ^= 1;

		add[0] = addr;
		if (flags & I2C_M_RD)
			ret = try_read_address(i2c_adap, addr, retries);
		else
			ret = try_write_address(i2c_adap, addr, retries);

		if (ret != 1) {
			return -EREMOTEIO;
		}
	}
	return 0;
}

static int
usb_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
	struct i2c_msg *pmsg;
	void *data;
	int i, ret;
	unsigned char addr;

	data = i2c_get_adapdata(i2c_adap);

	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		ret = usb_find_address(i2c_adap, pmsg, i2c_adap->retries, &addr);
		if (ret != 0) {
			PDEBUG(DBG_ALGO,"got NAK from device, message #%d", i);
			return (ret < 0) ? ret : -EREMOTEIO;
		}

		if (pmsg->flags & I2C_M_RD) {
			/* read bytes into buffer */
			ret = (usbvision_i2c_read(data, addr, pmsg->buf, pmsg->len));
			if (ret < pmsg->len) {
				return (ret < 0) ? ret : -EREMOTEIO;
			}
		} else {
			/* write bytes from buffer */
			ret = (usbvision_i2c_write(data, addr, pmsg->buf, pmsg->len));
			if (ret < pmsg->len) {
				return (ret < 0) ? ret : -EREMOTEIO;
			}
		}
	}
	return num;
}

static int algo_control(struct i2c_adapter *adapter, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static u32 usb_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING;
}


/* -----exported algorithm data: -------------------------------------	*/

static struct i2c_algorithm i2c_usb_algo = {
	.master_xfer   = usb_xfer,
	.smbus_xfer    = NULL,
	.algo_control  = algo_control,
	.functionality = usb_func,
};


/*
 * registering functions to load algorithms at runtime
 */
int usbvision_i2c_usb_add_bus(struct i2c_adapter *adap)
{
	PDEBUG(DBG_I2C, "I2C   debugging is enabled [i2c]");
	PDEBUG(DBG_ALGO, "ALGO   debugging is enabled [i2c]");

	/* register new adapter to i2c module... */

	adap->algo = &i2c_usb_algo;

	adap->timeout = 100;	/* default values, should       */
	adap->retries = 3;	/* be replaced by defines       */

	i2c_add_adapter(adap);

	PDEBUG(DBG_ALGO,"i2c bus for %s registered", adap->name);

	return 0;
}


int usbvision_i2c_usb_del_bus(struct i2c_adapter *adap)
{

	i2c_del_adapter(adap);

	PDEBUG(DBG_ALGO,"i2c bus for %s unregistered", adap->name);

	return 0;
}


/* ----------------------------------------------------------------------- */
/* usbvision specific I2C functions                                        */
/* ----------------------------------------------------------------------- */
static struct i2c_adapter i2c_adap_template;
static struct i2c_algo_usb_data i2c_algo_template;
static struct i2c_client i2c_client_template;

int usbvision_init_i2c(struct usb_usbvision *usbvision)
{
	memcpy(&usbvision->i2c_adap, &i2c_adap_template,
	       sizeof(struct i2c_adapter));
	memcpy(&usbvision->i2c_algo, &i2c_algo_template,
	       sizeof(struct i2c_algo_usb_data));
	memcpy(&usbvision->i2c_client, &i2c_client_template,
	       sizeof(struct i2c_client));

	sprintf(usbvision->i2c_adap.name + strlen(usbvision->i2c_adap.name),
		" #%d", usbvision->vdev->minor & 0x1f);
	PDEBUG(DBG_I2C,"Adaptername: %s", usbvision->i2c_adap.name);

	i2c_set_adapdata(&usbvision->i2c_adap, usbvision);
	i2c_set_clientdata(&usbvision->i2c_client, usbvision);
	i2c_set_algo_usb_data(&usbvision->i2c_algo, usbvision);

	usbvision->i2c_adap.algo_data = &usbvision->i2c_algo;
	usbvision->i2c_client.adapter = &usbvision->i2c_adap;

	if (usbvision_write_reg(usbvision, USBVISION_SER_MODE, USBVISION_IIC_LRNACK) < 0) {
		printk(KERN_ERR "usbvision_init_i2c: can't write reg\n");
		return -EBUSY;
	}

#ifdef CONFIG_MODULES
	/* Request the load of the i2c modules we need */
	switch (usbvision_device_data[usbvision->DevModel].Codec) {
	case CODEC_SAA7113:
		request_module("saa7115");
		break;
	case CODEC_SAA7111:
		request_module("saa7115");
		break;
	}
	if (usbvision_device_data[usbvision->DevModel].Tuner == 1) {
		request_module("tuner");
	}
#endif

	return usbvision_i2c_usb_add_bus(&usbvision->i2c_adap);
}

void call_i2c_clients(struct usb_usbvision *usbvision, unsigned int cmd,
		      void *arg)
{
	BUG_ON(NULL == usbvision->i2c_adap.algo_data);
	i2c_clients_command(&usbvision->i2c_adap, cmd, arg);
}

static int attach_inform(struct i2c_client *client)
{
	struct usb_usbvision *usbvision;

	usbvision = (struct usb_usbvision *)i2c_get_adapdata(client->adapter);

	switch (client->addr << 1) {
		case 0x43:
		case 0x4b:
		{
			struct tuner_setup tun_setup;

			tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
			tun_setup.type = TUNER_TDA9887;
			tun_setup.addr = client->addr;

			call_i2c_clients(usbvision, TUNER_SET_TYPE_ADDR, &tun_setup);

			break;
		}
		case 0x42:
			PDEBUG(DBG_I2C,"attach_inform: saa7114 detected.");
			break;
		case 0x4a:
			PDEBUG(DBG_I2C,"attach_inform: saa7113 detected.");
			break;
		case 0xa0:
			PDEBUG(DBG_I2C,"attach_inform: eeprom detected.");
			break;

		default:
			{
				struct tuner_setup tun_setup;

				PDEBUG(DBG_I2C,"attach inform: detected I2C address %x", client->addr << 1);
				usbvision->tuner_addr = client->addr;

				if ((usbvision->have_tuner) && (usbvision->tuner_type != -1)) {
					tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
					tun_setup.type = usbvision->tuner_type;
					tun_setup.addr = usbvision->tuner_addr;
					call_i2c_clients(usbvision, TUNER_SET_TYPE_ADDR, &tun_setup);
				}
			}
			break;
	}
	return 0;
}

static int detach_inform(struct i2c_client *client)
{
	struct usb_usbvision *usbvision;

	usbvision = (struct usb_usbvision *)i2c_get_adapdata(client->adapter);

	PDEBUG(DBG_I2C,"usbvision[%d] detaches %s", usbvision->nr, client->name);
	return 0;
}

static int
usbvision_i2c_read_max4(struct usb_usbvision *usbvision, unsigned char addr,
		     char *buf, short len)
{
	int rc, retries;

	for (retries = 5;;) {
		rc = usbvision_write_reg(usbvision, USBVISION_SER_ADRS, addr);
		if (rc < 0)
			return rc;

		/* Initiate byte read cycle                    */
		/* USBVISION_SER_CONT <- d0-d2 n. of bytes to r/w */
		/*                    d3 0=Wr 1=Rd             */
		rc = usbvision_write_reg(usbvision, USBVISION_SER_CONT,
				      (len & 0x07) | 0x18);
		if (rc < 0)
			return rc;

		/* Test for Busy and ACK */
		do {
			/* USBVISION_SER_CONT -> d4 == 0 busy */
			rc = usbvision_read_reg(usbvision, USBVISION_SER_CONT);
		} while (rc > 0 && ((rc & 0x10) != 0));	/* Retry while busy */
		if (rc < 0)
			return rc;

		/* USBVISION_SER_CONT -> d5 == 1 Not ack */
		if ((rc & 0x20) == 0)	/* Ack? */
			break;

		/* I2C abort */
		rc = usbvision_write_reg(usbvision, USBVISION_SER_CONT, 0x00);
		if (rc < 0)
			return rc;

		if (--retries < 0)
			return -1;
	}

	switch (len) {
	case 4:
		buf[3] = usbvision_read_reg(usbvision, USBVISION_SER_DAT4);
	case 3:
		buf[2] = usbvision_read_reg(usbvision, USBVISION_SER_DAT3);
	case 2:
		buf[1] = usbvision_read_reg(usbvision, USBVISION_SER_DAT2);
	case 1:
		buf[0] = usbvision_read_reg(usbvision, USBVISION_SER_DAT1);
		break;
	default:
		printk(KERN_ERR
		       "usbvision_i2c_read_max4: buffer length > 4\n");
	}

	if (i2c_debug & DBG_I2C) {
		int idx;
		for (idx = 0; idx < len; idx++) {
			PDEBUG(DBG_I2C,"read %x from address %x", (unsigned char)buf[idx], addr);
		}
	}
	return len;
}


static int usbvision_i2c_write_max4(struct usb_usbvision *usbvision,
				 unsigned char addr, const char *buf,
				 short len)
{
	int rc, retries;
	int i;
	unsigned char value[6];
	unsigned char ser_cont;

	ser_cont = (len & 0x07) | 0x10;

	value[0] = addr;
	value[1] = ser_cont;
	for (i = 0; i < len; i++)
		value[i + 2] = buf[i];

	for (retries = 5;;) {
		rc = usb_control_msg(usbvision->dev,
				     usb_sndctrlpipe(usbvision->dev, 1),
				     USBVISION_OP_CODE,
				     USB_DIR_OUT | USB_TYPE_VENDOR |
				     USB_RECIP_ENDPOINT, 0,
				     (__u16) USBVISION_SER_ADRS, value,
				     len + 2, HZ);

		if (rc < 0)
			return rc;

		rc = usbvision_write_reg(usbvision, USBVISION_SER_CONT,
				      (len & 0x07) | 0x10);
		if (rc < 0)
			return rc;

		/* Test for Busy and ACK */
		do {
			rc = usbvision_read_reg(usbvision, USBVISION_SER_CONT);
		} while (rc > 0 && ((rc & 0x10) != 0));	/* Retry while busy */
		if (rc < 0)
			return rc;

		if ((rc & 0x20) == 0)	/* Ack? */
			break;

		/* I2C abort */
		usbvision_write_reg(usbvision, USBVISION_SER_CONT, 0x00);

		if (--retries < 0)
			return -1;

	}

	if (i2c_debug & DBG_I2C) {
		int idx;
		for (idx = 0; idx < len; idx++) {
			PDEBUG(DBG_I2C,"wrote %x at address %x", (unsigned char)buf[idx], addr);
		}
	}
	return len;
}

static int usbvision_i2c_write(void *data, unsigned char addr, char *buf,
			    short len)
{
	char *bufPtr = buf;
	int retval;
	int wrcount = 0;
	int count;
	int maxLen = 4;
	struct usb_usbvision *usbvision = (struct usb_usbvision *) data;

	while (len > 0) {
		count = (len > maxLen) ? maxLen : len;
		retval = usbvision_i2c_write_max4(usbvision, addr, bufPtr, count);
		if (retval > 0) {
			len -= count;
			bufPtr += count;
			wrcount += count;
		} else
			return (retval < 0) ? retval : -EFAULT;
	}
	return wrcount;
}

static int usbvision_i2c_read(void *data, unsigned char addr, char *buf,
			   short len)
{
	char temp[4];
	int retval, i;
	int rdcount = 0;
	int count;
	struct usb_usbvision *usbvision = (struct usb_usbvision *) data;

	while (len > 0) {
		count = (len > 3) ? 4 : len;
		retval = usbvision_i2c_read_max4(usbvision, addr, temp, count);
		if (retval > 0) {
			for (i = 0; i < len; i++)
				buf[rdcount + i] = temp[i];
			len -= count;
			rdcount += count;
		} else
			return (retval < 0) ? retval : -EFAULT;
	}
	return rdcount;
}

static struct i2c_algo_usb_data i2c_algo_template = {
	.data		= NULL,
	.inb		= usbvision_i2c_read,
	.outb		= usbvision_i2c_write,
	.udelay		= 10,
	.mdelay		= 10,
	.timeout	= 100,
};

static struct i2c_adapter i2c_adap_template = {
	.owner = THIS_MODULE,
	.name              = "usbvision",
	.id                = I2C_HW_B_BT848, /* FIXME */
	.algo              = NULL,
	.algo_data         = NULL,
	.client_register   = attach_inform,
	.client_unregister = detach_inform,
#ifdef I2C_ADAP_CLASS_TV_ANALOG
	.class             = I2C_ADAP_CLASS_TV_ANALOG,
#else
	.class		   = I2C_CLASS_TV_ANALOG,
#endif
};

static struct i2c_client i2c_client_template = {
	.name		= "usbvision internal",
};

EXPORT_SYMBOL(usbvision_i2c_usb_add_bus);
EXPORT_SYMBOL(usbvision_i2c_usb_del_bus);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
