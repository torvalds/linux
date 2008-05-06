/*
 *
 * device driver for philips saa7134 based TV cards
 * i2c interface support
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "saa7134-reg.h"
#include "saa7134.h"
#include <media/v4l2-common.h>

/* ----------------------------------------------------------- */

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug,"enable debug messages [i2c]");

static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan,"scan i2c bus at insmod time");

#define d1printk if (1 == i2c_debug) printk
#define d2printk if (2 == i2c_debug) printk

#define I2C_WAIT_DELAY  32
#define I2C_WAIT_RETRY  16

/* ----------------------------------------------------------- */

static char *str_i2c_status[] = {
	"IDLE", "DONE_STOP", "BUSY", "TO_SCL", "TO_ARB", "DONE_WRITE",
	"DONE_READ", "DONE_WRITE_TO", "DONE_READ_TO", "NO_DEVICE",
	"NO_ACKN", "BUS_ERR", "ARB_LOST", "SEQ_ERR", "ST_ERR", "SW_ERR"
};

enum i2c_status {
	IDLE          = 0,  // no I2C command pending
	DONE_STOP     = 1,  // I2C command done and STOP executed
	BUSY          = 2,  // executing I2C command
	TO_SCL        = 3,  // executing I2C command, time out on clock stretching
	TO_ARB        = 4,  // time out on arbitration trial, still trying
	DONE_WRITE    = 5,  // I2C command done and awaiting next write command
	DONE_READ     = 6,  // I2C command done and awaiting next read command
	DONE_WRITE_TO = 7,  // see 5, and time out on status echo
	DONE_READ_TO  = 8,  // see 6, and time out on status echo
	NO_DEVICE     = 9,  // no acknowledge on device slave address
	NO_ACKN       = 10, // no acknowledge after data byte transfer
	BUS_ERR       = 11, // bus error
	ARB_LOST      = 12, // arbitration lost during transfer
	SEQ_ERR       = 13, // erroneous programming sequence
	ST_ERR        = 14, // wrong status echoing
	SW_ERR        = 15  // software error
};

static char *str_i2c_attr[] = {
	"NOP", "STOP", "CONTINUE", "START"
};

enum i2c_attr {
	NOP           = 0,  // no operation on I2C bus
	STOP          = 1,  // stop condition, no associated byte transfer
	CONTINUE      = 2,  // continue with byte transfer
	START         = 3   // start condition with byte transfer
};

static inline enum i2c_status i2c_get_status(struct saa7134_dev *dev)
{
	enum i2c_status status;

	status = saa_readb(SAA7134_I2C_ATTR_STATUS) & 0x0f;
	d2printk(KERN_DEBUG "%s: i2c stat <= %s\n",dev->name,
		 str_i2c_status[status]);
	return status;
}

static inline void i2c_set_status(struct saa7134_dev *dev,
				  enum i2c_status status)
{
	d2printk(KERN_DEBUG "%s: i2c stat => %s\n",dev->name,
		 str_i2c_status[status]);
	saa_andorb(SAA7134_I2C_ATTR_STATUS,0x0f,status);
}

static inline void i2c_set_attr(struct saa7134_dev *dev, enum i2c_attr attr)
{
	d2printk(KERN_DEBUG "%s: i2c attr => %s\n",dev->name,
		 str_i2c_attr[attr]);
	saa_andorb(SAA7134_I2C_ATTR_STATUS,0xc0,attr << 6);
}

static inline int i2c_is_error(enum i2c_status status)
{
	switch (status) {
	case NO_DEVICE:
	case NO_ACKN:
	case BUS_ERR:
	case ARB_LOST:
	case SEQ_ERR:
	case ST_ERR:
		return true;
	default:
		return false;
	}
}

static inline int i2c_is_idle(enum i2c_status status)
{
	switch (status) {
	case IDLE:
	case DONE_STOP:
		return true;
	default:
		return false;
	}
}

static inline int i2c_is_busy(enum i2c_status status)
{
	switch (status) {
	case BUSY:
	case TO_SCL:
	case TO_ARB:
		return true;
	default:
		return false;
	}
}

static int i2c_is_busy_wait(struct saa7134_dev *dev)
{
	enum i2c_status status;
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		status = i2c_get_status(dev);
		if (!i2c_is_busy(status))
			break;
		saa_wait(I2C_WAIT_DELAY);
	}
	if (I2C_WAIT_RETRY == count)
		return false;
	return true;
}

static int i2c_reset(struct saa7134_dev *dev)
{
	enum i2c_status status;
	int count;

	d2printk(KERN_DEBUG "%s: i2c reset\n",dev->name);
	status = i2c_get_status(dev);
	if (!i2c_is_error(status))
		return true;
	i2c_set_status(dev,status);

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		status = i2c_get_status(dev);
		if (!i2c_is_error(status))
			break;
		udelay(I2C_WAIT_DELAY);
	}
	if (I2C_WAIT_RETRY == count)
		return false;

	if (!i2c_is_idle(status))
		return false;

	i2c_set_attr(dev,NOP);
	return true;
}

static inline int i2c_send_byte(struct saa7134_dev *dev,
				enum i2c_attr attr,
				unsigned char data)
{
	enum i2c_status status;
	__u32 dword;

	/* have to write both attr + data in one 32bit word */
	dword  = saa_readl(SAA7134_I2C_ATTR_STATUS >> 2);
	dword &= 0x0f;
	dword |= (attr << 6);
	dword |= ((__u32)data << 8);
	dword |= 0x00 << 16;  /* 100 kHz */
//	dword |= 0x40 << 16;  /* 400 kHz */
	dword |= 0xf0 << 24;
	saa_writel(SAA7134_I2C_ATTR_STATUS >> 2, dword);
	d2printk(KERN_DEBUG "%s: i2c data => 0x%x\n",dev->name,data);

	if (!i2c_is_busy_wait(dev))
		return -EIO;
	status = i2c_get_status(dev);
	if (i2c_is_error(status))
		return -EIO;
	return 0;
}

static inline int i2c_recv_byte(struct saa7134_dev *dev)
{
	enum i2c_status status;
	unsigned char data;

	i2c_set_attr(dev,CONTINUE);
	if (!i2c_is_busy_wait(dev))
		return -EIO;
	status = i2c_get_status(dev);
	if (i2c_is_error(status))
		return -EIO;
	data = saa_readb(SAA7134_I2C_DATA);
	d2printk(KERN_DEBUG "%s: i2c data <= 0x%x\n",dev->name,data);
	return data;
}

static int saa7134_i2c_xfer(struct i2c_adapter *i2c_adap,
			    struct i2c_msg *msgs, int num)
{
	struct saa7134_dev *dev = i2c_adap->algo_data;
	enum i2c_status status;
	unsigned char data;
	int addr,rc,i,byte;

	status = i2c_get_status(dev);
	if (!i2c_is_idle(status))
		if (!i2c_reset(dev))
			return -EIO;

	d2printk("start xfer\n");
	d1printk(KERN_DEBUG "%s: i2c xfer:",dev->name);
	for (i = 0; i < num; i++) {
		if (!(msgs[i].flags & I2C_M_NOSTART) || 0 == i) {
			/* send address */
			d2printk("send address\n");
			addr  = msgs[i].addr << 1;
			if (msgs[i].flags & I2C_M_RD)
				addr |= 1;
			if (i > 0 && msgs[i].flags & I2C_M_RD) {
				/* workaround for a saa7134 i2c bug
				 * needed to talk to the mt352 demux
				 * thanks to pinnacle for the hint */
				int quirk = 0xfd;
				d1printk(" [%02x quirk]",quirk);
				i2c_send_byte(dev,START,quirk);
				i2c_recv_byte(dev);
			}
			d1printk(" < %02x", addr);
			rc = i2c_send_byte(dev,START,addr);
			if (rc < 0)
				 goto err;
		}
		if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */
			d2printk("read bytes\n");
			for (byte = 0; byte < msgs[i].len; byte++) {
				d1printk(" =");
				rc = i2c_recv_byte(dev);
				if (rc < 0)
					goto err;
				d1printk("%02x", rc);
				msgs[i].buf[byte] = rc;
			}
		} else {
			/* write bytes */
			d2printk("write bytes\n");
			for (byte = 0; byte < msgs[i].len; byte++) {
				data = msgs[i].buf[byte];
				d1printk(" %02x", data);
				rc = i2c_send_byte(dev,CONTINUE,data);
				if (rc < 0)
					goto err;
			}
		}
	}
	d2printk("xfer done\n");
	d1printk(" >");
	i2c_set_attr(dev,STOP);
	rc = -EIO;
	if (!i2c_is_busy_wait(dev))
		goto err;
	status = i2c_get_status(dev);
	if (i2c_is_error(status))
		goto err;
	/* ensure that the bus is idle for at least one bit slot */
	msleep(1);

	d1printk("\n");
	return num;
 err:
	if (1 == i2c_debug) {
		status = i2c_get_status(dev);
		printk(" ERROR: %s\n",str_i2c_status[status]);
	}
	return rc;
}

/* ----------------------------------------------------------- */

static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static int attach_inform(struct i2c_client *client)
{
	struct saa7134_dev *dev = client->adapter->algo_data;

	d1printk( "%s i2c attach [addr=0x%x,client=%s]\n",
		client->driver->driver.name, client->addr, client->name);

	/* Am I an i2c remote control? */

	switch (client->addr) {
		case 0x7a:
		case 0x47:
		case 0x71:
		case 0x2d:
		{
			struct IR_i2c *ir = i2c_get_clientdata(client);
			d1printk("%s i2c IR detected (%s).\n",
				 client->driver->driver.name, ir->phys);
			saa7134_set_i2c_ir(dev,ir);
			break;
		}
	}

	return 0;
}

static struct i2c_algorithm saa7134_algo = {
	.master_xfer   = saa7134_i2c_xfer,
	.functionality = functionality,
};

static struct i2c_adapter saa7134_adap_template = {
	.owner         = THIS_MODULE,
	.class         = I2C_CLASS_TV_ANALOG,
	.name          = "saa7134",
	.id            = I2C_HW_SAA7134,
	.algo          = &saa7134_algo,
	.client_register = attach_inform,
};

static struct i2c_client saa7134_client_template = {
	.name	= "saa7134 internal",
};

/* ----------------------------------------------------------- */

static int
saa7134_i2c_eeprom(struct saa7134_dev *dev, unsigned char *eedata, int len)
{
	unsigned char buf;
	int i,err;

	dev->i2c_client.addr = 0xa0 >> 1;
	buf = 0;
	if (1 != (err = i2c_master_send(&dev->i2c_client,&buf,1))) {
		printk(KERN_INFO "%s: Huh, no eeprom present (err=%d)?\n",
		       dev->name,err);
		return -1;
	}
	if (len != (err = i2c_master_recv(&dev->i2c_client,eedata,len))) {
		printk(KERN_WARNING "%s: i2c eeprom read error (err=%d)\n",
		       dev->name,err);
		return -1;
	}
	for (i = 0; i < len; i++) {
		if (0 == (i % 16))
			printk(KERN_INFO "%s: i2c eeprom %02x:",dev->name,i);
		printk(" %02x",eedata[i]);
		if (15 == (i % 16))
			printk("\n");
	}
	return 0;
}

static char *i2c_devs[128] = {
	[ 0x20      ] = "mpeg encoder (saa6752hs)",
	[ 0xa0 >> 1 ] = "eeprom",
	[ 0xc0 >> 1 ] = "tuner (analog)",
	[ 0x86 >> 1 ] = "tda9887",
	[ 0x5a >> 1 ] = "remote control",
};

static void do_i2c_scan(char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i,rc;

	for (i = 0; i < ARRAY_SIZE(i2c_devs); i++) {
		c->addr = i;
		rc = i2c_master_recv(c,&buf,0);
		if (rc < 0)
			continue;
		printk("%s: i2c scan: found device @ 0x%x  [%s]\n",
		       name, i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

void saa7134_i2c_call_clients(struct saa7134_dev *dev,
			      unsigned int cmd, void *arg)
{
	BUG_ON(NULL == dev->i2c_adap.algo_data);
	i2c_clients_command(&dev->i2c_adap, cmd, arg);
}

int saa7134_i2c_register(struct saa7134_dev *dev)
{
	dev->i2c_adap = saa7134_adap_template;
	dev->i2c_adap.dev.parent = &dev->pci->dev;
	strcpy(dev->i2c_adap.name,dev->name);
	dev->i2c_adap.algo_data = dev;
	i2c_add_adapter(&dev->i2c_adap);

	dev->i2c_client = saa7134_client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	saa7134_i2c_eeprom(dev,dev->eedata,sizeof(dev->eedata));
	if (i2c_scan)
		do_i2c_scan(dev->name,&dev->i2c_client);
	return 0;
}

int saa7134_i2c_unregister(struct saa7134_dev *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
