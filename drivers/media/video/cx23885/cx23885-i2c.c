/*
 *  Driver for the Conexant CX23885 PCIe bridge
 *
 *  Copyright (c) 2006 Steven Toth <stoth@hauppauge.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "cx23885.h"

#include <media/v4l2-common.h>

static unsigned int i2c_debug = 0;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

static unsigned int i2c_scan = 0;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

#define dprintk(level,fmt, arg...)	if (i2c_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt, dev->name , ## arg)

#define I2C_WAIT_DELAY 32
#define I2C_WAIT_RETRY 64

#define I2C_EXTEND  (1 << 3)
#define I2C_NOSTOP  (1 << 4)

static inline int i2c_slave_did_ack(struct i2c_adapter *i2c_adap)
{
	struct cx23885_i2c *bus = i2c_adap->algo_data;
	struct cx23885_dev *dev = bus->dev;
	return cx_read(bus->reg_stat) & 0x01;
}

static inline int i2c_is_busy(struct i2c_adapter *i2c_adap)
{
	struct cx23885_i2c *bus = i2c_adap->algo_data;
	struct cx23885_dev *dev = bus->dev;
	return cx_read(bus->reg_stat) & 0x02 ? 1 : 0;
}

static int i2c_wait_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_is_busy(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static int i2c_sendbytes(struct i2c_adapter *i2c_adap,
			 const struct i2c_msg *msg, int last)
{
	struct cx23885_i2c *bus = i2c_adap->algo_data;
	struct cx23885_dev *dev = bus->dev;
	u32 wdata, addr, ctrl;
	int retval, cnt;

	dprintk(1, "%s()\n", __FUNCTION__);
	/* Deal with i2c probe functions with zero payload */
	if (msg->len == 0) {
		cx_write(bus->reg_addr, msg->addr << 25);
		cx_write(bus->reg_ctrl, bus->i2c_period | (1 << 2));
		if (!i2c_wait_done(i2c_adap))
			return -EIO;
		if (!i2c_slave_did_ack(i2c_adap))
			return -EIO;

		dprintk(1, "%s() returns 0\n", __FUNCTION__);
		return 0;
	}


	/* dev, reg + first byte */
	addr = (msg->addr << 25) | msg->buf[0];
	wdata = msg->buf[0];
	ctrl = bus->i2c_period | (1 << 12) | (1 << 2);

	if (msg->len > 1)
		ctrl |= I2C_NOSTOP | I2C_EXTEND;

	cx_write(bus->reg_addr, addr);
	cx_write(bus->reg_wdata, wdata);
	cx_write(bus->reg_ctrl, ctrl);

	retval = i2c_wait_done(i2c_adap);
	if (retval < 0)
		goto err;
	if (retval == 0)
		goto eio;
	if (i2c_debug) {
		printk(" <W %02x %02x", msg->addr << 1, msg->buf[0]);
		if (!(ctrl & I2C_NOSTOP))
			printk(" >\n");
	}

	for (cnt = 1; cnt < msg->len; cnt++ ) {
		/* following bytes */
		wdata = msg->buf[cnt];
		ctrl = bus->i2c_period | (1 << 12) | (1 << 2);

		if (cnt < msg->len-1 || !last)
			ctrl |= I2C_NOSTOP | I2C_EXTEND;

		cx_write(bus->reg_addr, addr);
		cx_write(bus->reg_wdata, wdata);
		cx_write(bus->reg_ctrl, ctrl);

		retval = i2c_wait_done(i2c_adap);
		if (retval < 0)
			goto err;
		if (retval == 0)
			goto eio;
		if (i2c_debug) {
			printk(" %02x", msg->buf[cnt]);
			if (!(ctrl & I2C_NOSTOP))
				printk(" >\n");
		}
	}
	return msg->len;

 eio:
	retval = -EIO;
 err:
	printk(" ERR: %d\n", retval);
	return retval;
}

static int i2c_readbytes(struct i2c_adapter *i2c_adap,
			 const struct i2c_msg *msg, int last)
{
	struct cx23885_i2c *bus = i2c_adap->algo_data;
	struct cx23885_dev *dev = bus->dev;
	u32 ctrl, cnt;
	int retval;

	dprintk(1, "%s()\n", __FUNCTION__);

	/* Deal with i2c probe functions with zero payload */
	if (msg->len == 0) {
		cx_write(bus->reg_addr, msg->addr << 25);
		cx_write(bus->reg_ctrl, bus->i2c_period | (1 << 2) | 1);
		if (!i2c_wait_done(i2c_adap))
			return -EIO;
		if (!i2c_slave_did_ack(i2c_adap))
			return -EIO;


		dprintk(1, "%s() returns 0\n", __FUNCTION__);
		return 0;
	}

	for(cnt = 0; cnt < msg->len; cnt++) {

		ctrl = bus->i2c_period | (1 << 12) | (1 << 2) | 1;

		if (cnt < msg->len-1 || !last)
			ctrl |= I2C_NOSTOP | I2C_EXTEND;

		cx_write(bus->reg_addr, msg->addr << 25);
		cx_write(bus->reg_ctrl, ctrl);

		retval = i2c_wait_done(i2c_adap);
		if (retval < 0)
			goto err;
		if (retval == 0)
			goto eio;
		msg->buf[cnt] = cx_read(bus->reg_rdata) & 0xff;
		if (i2c_debug) {
			if (!(ctrl & I2C_NOSTOP))
				printk(" <R %02x", (msg->addr << 1) +1);
			printk(" =%02x", msg->buf[cnt]);
			if (!(ctrl & I2C_NOSTOP))
				printk(" >\n");
		}
	}
	return msg->len;

 eio:
	retval = -EIO;
 err:
	printk(" ERR: %d\n", retval);
	return retval;
}

static int i2c_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg *msgs, int num)
{
	struct cx23885_i2c *bus = i2c_adap->algo_data;
	struct cx23885_dev *dev = bus->dev;
	int i, retval = 0;

	dprintk(1, "%s(num = %d)\n", __FUNCTION__, num);

	for (i = 0 ; i < num; i++) {
		dprintk(1, "%s(num = %d) addr = 0x%02x  len = 0x%x\n",
			__FUNCTION__, num, msgs[i].addr, msgs[i].len);
		if (msgs[i].flags & I2C_M_RD) {
			/* read */
			retval = i2c_readbytes(i2c_adap, &msgs[i], i+1 == num);
			if (retval < 0)
				goto err;
		} else {
			/* write */
			retval = i2c_sendbytes(i2c_adap, &msgs[i], i+1 == num);
			if (retval < 0)
				goto err;
		}
	}
	return num;

 err:
	return retval;
}

static int attach_inform(struct i2c_client *client)
{
	struct cx23885_dev *dev = i2c_get_adapdata(client->adapter);

	dprintk(1, "%s i2c attach [addr=0x%x,client=%s]\n",
		client->driver->driver.name, client->addr, client->name);

	if (!client->driver->command)
		return 0;

	return 0;
}

static int detach_inform(struct i2c_client *client)
{
	struct cx23885_dev *dev = i2c_get_adapdata(client->adapter);

	dprintk(1, "i2c detach [client=%s]\n", client->name);

	return 0;
}

void cx23885_call_i2c_clients(struct cx23885_i2c *bus,
			      unsigned int cmd, void *arg)
{
	if (bus->i2c_rc != 0)
		return;

	i2c_clients_command(&bus->i2c_adap, cmd, arg);
}

static int cx23885_algo_control(struct i2c_adapter *adap,
				unsigned int cmd, unsigned long arg)
{
	return 0;
}

static u32 cx23885_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static struct i2c_algorithm cx23885_i2c_algo_template = {
	.master_xfer	= i2c_xfer,
	.algo_control	= cx23885_algo_control,
	.functionality	= cx23885_functionality,
};

/* ----------------------------------------------------------------------- */

static struct i2c_adapter cx23885_i2c_adap_template = {
	.name              = "cx23885",
	.owner             = THIS_MODULE,
	.id                = I2C_HW_B_CX23885,
	.algo              = &cx23885_i2c_algo_template,
	.client_register   = attach_inform,
	.client_unregister = detach_inform,
};

static struct i2c_client cx23885_i2c_client_template = {
	.name	= "cx23885 internal",
};

static char *i2c_devs[128] = {
	[ 0x1c >> 1 ] = "lgdt3303",
	[ 0x86 >> 1 ] = "tda9887",
	[ 0x32 >> 1 ] = "cx24227",
	[ 0x88 >> 1 ] = "cx25837",
	[ 0x84 >> 1 ] = "tda8295",
	[ 0xa0 >> 1 ] = "eeprom",
	[ 0xc0 >> 1 ] = "tuner/mt2131/tda8275",
	[ 0xc2 >> 1 ] = "tuner/mt2131/tda8275",
};

static void do_i2c_scan(char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i, rc;

	for (i = 0; i < 128; i++) {
		c->addr = i;
		rc = i2c_master_recv(c, &buf, 0);
		if (rc < 0)
			continue;
		printk("%s: i2c scan: found device @ 0x%x  [%s]\n",
		       name, i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/* init + register i2c algo-bit adapter */
int cx23885_i2c_register(struct cx23885_i2c *bus)
{
	struct cx23885_dev *dev = bus->dev;

	dprintk(1, "%s(bus = %d)\n", __FUNCTION__, bus->nr);

	memcpy(&bus->i2c_adap, &cx23885_i2c_adap_template,
	       sizeof(bus->i2c_adap));
	memcpy(&bus->i2c_algo, &cx23885_i2c_algo_template,
	       sizeof(bus->i2c_algo));
	memcpy(&bus->i2c_client, &cx23885_i2c_client_template,
	       sizeof(bus->i2c_client));

	bus->i2c_adap.dev.parent = &dev->pci->dev;

	strlcpy(bus->i2c_adap.name, bus->dev->name,
		sizeof(bus->i2c_adap.name));

	bus->i2c_algo.data = bus;
	bus->i2c_adap.algo_data = bus;
	i2c_add_adapter(&bus->i2c_adap);

	bus->i2c_client.adapter = &bus->i2c_adap;

	if (0 == bus->i2c_rc) {
		printk("%s: i2c bus %d registered\n", dev->name, bus->nr);
		if (i2c_scan)
			do_i2c_scan(dev->name, &bus->i2c_client);
	} else
		printk("%s: i2c bus %d register FAILED\n", dev->name, bus->nr);

	return bus->i2c_rc;
}

int cx23885_i2c_unregister(struct cx23885_i2c *bus)
{
	i2c_del_adapter(&bus->i2c_adap);
	return 0;
}

/* ----------------------------------------------------------------------- */

EXPORT_SYMBOL(cx23885_call_i2c_clients);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
