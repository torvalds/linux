/*
   cx231xx-i2c.c - driver for Conexant Cx23100/101/102 USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
		Based on em28xx driver
		Based on Cx23885 driver

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
#include <media/v4l2-common.h>
#include <media/tuner.h>

#include "cx231xx.h"

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
		printk(fmt, ##args);			\
		}					\
} while (0)

#define dprintk2(lvl, fmt, args...)			\
do {							\
	if (i2c_debug >= lvl) {				\
		printk(KERN_DEBUG "%s at %s: " fmt,	\
		       dev->name, __func__ , ##args);	\
      } 						\
} while (0)

/*
 * cx231xx_i2c_send_bytes()
 */
int cx231xx_i2c_send_bytes(struct i2c_adapter *i2c_adap,
			   const struct i2c_msg *msg)
{
	struct cx231xx_i2c *bus = i2c_adap->algo_data;
	struct cx231xx *dev = bus->dev;
	struct cx231xx_i2c_xfer_data req_data;
	int status = 0;
	u16 size = 0;
	u8 loop = 0;
	u8 saddr_len = 1;
	u8 *buf_ptr = NULL;
	u16 saddr = 0;
	u8 need_gpio = 0;

	if ((bus->nr == 1) && (msg->addr == 0x61)
	    && (dev->tuner_type == TUNER_XC5000)) {

		size = msg->len;

		if (size == 2) {	/* register write sub addr */
			/* Just writing sub address will cause problem
			* to XC5000. So ignore the request */
			return 0;
		} else if (size == 4) {	/* register write with sub addr */
			if (msg->len >= 2)
				saddr = msg->buf[0] << 8 | msg->buf[1];
			else if (msg->len == 1)
				saddr = msg->buf[0];

			switch (saddr) {
			case 0x0000:	/* start tuner calibration mode */
				need_gpio = 1;
				/* FW Loading is done */
				dev->xc_fw_load_done = 1;
				break;
			case 0x000D:	/* Set signal source */
			case 0x0001:	/* Set TV standard - Video */
			case 0x0002:	/* Set TV standard - Audio */
			case 0x0003:	/* Set RF Frequency */
				need_gpio = 1;
				break;
			default:
				if (dev->xc_fw_load_done)
					need_gpio = 1;
				break;
			}

			if (need_gpio) {
				dprintk1(1,
				"GPIO WRITE: addr 0x%x, len %d, saddr 0x%x\n",
				msg->addr, msg->len, saddr);

				return dev->cx231xx_gpio_i2c_write(dev,
								   msg->addr,
								   msg->buf,
								   msg->len);
			}
		}

		/* special case for Xc5000 tuner case */
		saddr_len = 1;

		/* adjust the length to correct length */
		size -= saddr_len;
		buf_ptr = (u8 *) (msg->buf + 1);

		do {
			/* prepare xfer_data struct */
			req_data.dev_addr = msg->addr;
			req_data.direction = msg->flags;
			req_data.saddr_len = saddr_len;
			req_data.saddr_dat = msg->buf[0];
			req_data.buf_size = size > 16 ? 16 : size;
			req_data.p_buffer = (u8 *) (buf_ptr + loop * 16);

			bus->i2c_nostop = (size > 16) ? 1 : 0;
			bus->i2c_reserve = (loop == 0) ? 0 : 1;

			/* usb send command */
			status = dev->cx231xx_send_usb_command(bus, &req_data);
			loop++;

			if (size >= 16)
				size -= 16;
			else
				size = 0;

		} while (size > 0);

		bus->i2c_nostop = 0;
		bus->i2c_reserve = 0;

	} else {		/* regular case */

		/* prepare xfer_data struct */
		req_data.dev_addr = msg->addr;
		req_data.direction = msg->flags;
		req_data.saddr_len = 0;
		req_data.saddr_dat = 0;
		req_data.buf_size = msg->len;
		req_data.p_buffer = msg->buf;

		/* usb send command */
		status = dev->cx231xx_send_usb_command(bus, &req_data);
	}

	return status < 0 ? status : 0;
}

/*
 * cx231xx_i2c_recv_bytes()
 * read a byte from the i2c device
 */
static int cx231xx_i2c_recv_bytes(struct i2c_adapter *i2c_adap,
				  const struct i2c_msg *msg)
{
	struct cx231xx_i2c *bus = i2c_adap->algo_data;
	struct cx231xx *dev = bus->dev;
	struct cx231xx_i2c_xfer_data req_data;
	int status = 0;
	u16 saddr = 0;
	u8 need_gpio = 0;

	if ((bus->nr == 1) && (msg->addr == 0x61)
	    && dev->tuner_type == TUNER_XC5000) {

		if (msg->len == 2)
			saddr = msg->buf[0] << 8 | msg->buf[1];
		else if (msg->len == 1)
			saddr = msg->buf[0];

		if (dev->xc_fw_load_done) {

			switch (saddr) {
			case 0x0009:	/* BUSY check */
				dprintk1(1,
				"GPIO R E A D: Special case BUSY check \n");
				/*Try read BUSY register, just set it to zero*/
				msg->buf[0] = 0;
				if (msg->len == 2)
					msg->buf[1] = 0;
				return 0;
			case 0x0004:	/* read Lock status */
				need_gpio = 1;
				break;

			}

			if (need_gpio) {
				/* this is a special case to handle Xceive tuner
				clock stretch issue with gpio based I2C */

				dprintk1(1,
				"GPIO R E A D: addr 0x%x, len %d, saddr 0x%x\n",
				msg->addr, msg->len,
				msg->buf[0] << 8 | msg->buf[1]);

				status =
				    dev->cx231xx_gpio_i2c_write(dev, msg->addr,
								msg->buf,
								msg->len);
				status =
				    dev->cx231xx_gpio_i2c_read(dev, msg->addr,
							       msg->buf,
							       msg->len);
				return status;
			}
		}

		/* prepare xfer_data struct */
		req_data.dev_addr = msg->addr;
		req_data.direction = msg->flags;
		req_data.saddr_len = msg->len;
		req_data.saddr_dat = msg->buf[0] << 8 | msg->buf[1];
		req_data.buf_size = msg->len;
		req_data.p_buffer = msg->buf;

		/* usb send command */
		status = dev->cx231xx_send_usb_command(bus, &req_data);

	} else {

		/* prepare xfer_data struct */
		req_data.dev_addr = msg->addr;
		req_data.direction = msg->flags;
		req_data.saddr_len = 0;
		req_data.saddr_dat = 0;
		req_data.buf_size = msg->len;
		req_data.p_buffer = msg->buf;

		/* usb send command */
		status = dev->cx231xx_send_usb_command(bus, &req_data);
	}

	return status < 0 ? status : 0;
}

/*
 * cx231xx_i2c_recv_bytes_with_saddr()
 * read a byte from the i2c device
 */
static int cx231xx_i2c_recv_bytes_with_saddr(struct i2c_adapter *i2c_adap,
					     const struct i2c_msg *msg1,
					     const struct i2c_msg *msg2)
{
	struct cx231xx_i2c *bus = i2c_adap->algo_data;
	struct cx231xx *dev = bus->dev;
	struct cx231xx_i2c_xfer_data req_data;
	int status = 0;
	u16 saddr = 0;
	u8 need_gpio = 0;

	if (msg1->len == 2)
		saddr = msg1->buf[0] << 8 | msg1->buf[1];
	else if (msg1->len == 1)
		saddr = msg1->buf[0];

	if ((bus->nr == 1) && (msg2->addr == 0x61)
	    && dev->tuner_type == TUNER_XC5000) {

		if ((msg2->len < 16)) {

			dprintk1(1,
			"i2c_read: addr 0x%x, len %d, saddr 0x%x, len %d\n",
			msg2->addr, msg2->len, saddr, msg1->len);

			switch (saddr) {
			case 0x0008:	/* read FW load status */
				need_gpio = 1;
				break;
			case 0x0004:	/* read Lock status */
				need_gpio = 1;
				break;
			}

			if (need_gpio) {
				status =
				    dev->cx231xx_gpio_i2c_write(dev, msg1->addr,
								msg1->buf,
								msg1->len);
				status =
				    dev->cx231xx_gpio_i2c_read(dev, msg2->addr,
							       msg2->buf,
							       msg2->len);
				return status;
			}
		}
	}

	/* prepare xfer_data struct */
	req_data.dev_addr = msg2->addr;
	req_data.direction = msg2->flags;
	req_data.saddr_len = msg1->len;
	req_data.saddr_dat = saddr;
	req_data.buf_size = msg2->len;
	req_data.p_buffer = msg2->buf;

	/* usb send command */
	status = dev->cx231xx_send_usb_command(bus, &req_data);

	return status < 0 ? status : 0;
}

/*
 * cx231xx_i2c_check_for_device()
 * check if there is a i2c_device at the supplied address
 */
static int cx231xx_i2c_check_for_device(struct i2c_adapter *i2c_adap,
					const struct i2c_msg *msg)
{
	struct cx231xx_i2c *bus = i2c_adap->algo_data;
	struct cx231xx *dev = bus->dev;
	struct cx231xx_i2c_xfer_data req_data;
	int status = 0;

	/* prepare xfer_data struct */
	req_data.dev_addr = msg->addr;
	req_data.direction = msg->flags;
	req_data.saddr_len = 0;
	req_data.saddr_dat = 0;
	req_data.buf_size = 0;
	req_data.p_buffer = NULL;

	/* usb send command */
	status = dev->cx231xx_send_usb_command(bus, &req_data);

	return status < 0 ? status : 0;
}

/*
 * cx231xx_i2c_xfer()
 * the main i2c transfer function
 */
static int cx231xx_i2c_xfer(struct i2c_adapter *i2c_adap,
			    struct i2c_msg msgs[], int num)
{
	struct cx231xx_i2c *bus = i2c_adap->algo_data;
	struct cx231xx *dev = bus->dev;
	int addr, rc, i, byte;

	if (num <= 0)
		return 0;

	for (i = 0; i < num; i++) {

		addr = msgs[i].addr >> 1;

		dprintk2(2, "%s %s addr=%x len=%d:",
			 (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			 i == num - 1 ? "stop" : "nonstop", addr, msgs[i].len);
		if (!msgs[i].len) {
			/* no len: check only for device presence */
			rc = cx231xx_i2c_check_for_device(i2c_adap, &msgs[i]);
			if (rc < 0) {
				dprintk2(2, " no device\n");
				return rc;
			}

		} else if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */
			rc = cx231xx_i2c_recv_bytes(i2c_adap, &msgs[i]);
			if (i2c_debug >= 2) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
		} else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr
			   && (msgs[i].len <= 2) && (bus->nr < 2)) {
			/* read bytes */
			rc = cx231xx_i2c_recv_bytes_with_saddr(i2c_adap,
							       &msgs[i],
							       &msgs[i + 1]);
			if (i2c_debug >= 2) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
			i++;
		} else {
			/* write bytes */
			if (i2c_debug >= 2) {
				for (byte = 0; byte < msgs[i].len; byte++)
					printk(" %02x", msgs[i].buf[byte]);
			}
			rc = cx231xx_i2c_send_bytes(i2c_adap, &msgs[i]);
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

/* ----------------------------------------------------------- */

/*
 * functionality()
 */
static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static struct i2c_algorithm cx231xx_algo = {
	.master_xfer = cx231xx_i2c_xfer,
	.functionality = functionality,
};

static struct i2c_adapter cx231xx_adap_template = {
	.owner = THIS_MODULE,
	.name = "cx231xx",
	.algo = &cx231xx_algo,
};

static struct i2c_client cx231xx_client_template = {
	.name = "cx231xx internal",
};

/* ----------------------------------------------------------- */

/*
 * i2c_devs
 * incomplete list of known devices
 */
static char *i2c_devs[128] = {
	[0x60 >> 1] = "colibri",
	[0x88 >> 1] = "hammerhead",
	[0x8e >> 1] = "CIR",
	[0x32 >> 1] = "GeminiIII",
	[0x02 >> 1] = "Aquarius",
	[0xa0 >> 1] = "eeprom",
	[0xc0 >> 1] = "tuner/XC3028",
	[0xc2 >> 1] = "tuner/XC5000",
};

/*
 * cx231xx_do_i2c_scan()
 * check i2c address range for devices
 */
void cx231xx_do_i2c_scan(struct cx231xx *dev, struct i2c_client *c)
{
	unsigned char buf;
	int i, rc;

	cx231xx_info(": Checking for I2C devices ..\n");
	for (i = 0; i < 128; i++) {
		c->addr = i;
		rc = i2c_master_recv(c, &buf, 0);
		if (rc < 0)
			continue;
		cx231xx_info("%s: i2c scan: found device @ 0x%x  [%s]\n",
			     dev->name, i << 1,
			     i2c_devs[i] ? i2c_devs[i] : "???");
	}
	cx231xx_info(": Completed Checking for I2C devices.\n");
}

/*
 * cx231xx_i2c_register()
 * register i2c bus
 */
int cx231xx_i2c_register(struct cx231xx_i2c *bus)
{
	struct cx231xx *dev = bus->dev;

	BUG_ON(!dev->cx231xx_send_usb_command);

	memcpy(&bus->i2c_adap, &cx231xx_adap_template, sizeof(bus->i2c_adap));
	memcpy(&bus->i2c_algo, &cx231xx_algo, sizeof(bus->i2c_algo));
	memcpy(&bus->i2c_client, &cx231xx_client_template,
	       sizeof(bus->i2c_client));

	bus->i2c_adap.dev.parent = &dev->udev->dev;

	strlcpy(bus->i2c_adap.name, bus->dev->name, sizeof(bus->i2c_adap.name));

	bus->i2c_algo.data = bus;
	bus->i2c_adap.algo_data = bus;
	i2c_set_adapdata(&bus->i2c_adap, &dev->v4l2_dev);
	i2c_add_adapter(&bus->i2c_adap);

	bus->i2c_client.adapter = &bus->i2c_adap;

	if (0 == bus->i2c_rc) {
		if (i2c_scan)
			cx231xx_do_i2c_scan(dev, &bus->i2c_client);

		/* Instantiate the IR receiver device, if present */
		cx231xx_register_i2c_ir(dev);
	} else
		cx231xx_warn("%s: i2c bus %d register FAILED\n",
			     dev->name, bus->nr);

	return bus->i2c_rc;
}

/*
 * cx231xx_i2c_unregister()
 * unregister i2c_bus
 */
int cx231xx_i2c_unregister(struct cx231xx_i2c *bus)
{
	i2c_del_adapter(&bus->i2c_adap);
	return 0;
}
