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
 */

#include "saa7134.h"
#include "saa7134-reg.h"

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <media/v4l2-common.h>

/* ----------------------------------------------------------- */

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug,"enable debug messages [i2c]");

static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan,"scan i2c bus at insmod time");

#define i2c_dbg(level, fmt, arg...) do { \
	if (i2c_debug == level) \
		printk(KERN_DEBUG pr_fmt("i2c: " fmt), ## arg); \
	} while (0)

#define i2c_cont(level, fmt, arg...) do { \
	if (i2c_debug == level) \
		pr_cont(fmt, ## arg); \
	} while (0)

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
	i2c_dbg(2, "i2c stat <= %s\n", str_i2c_status[status]);
	return status;
}

static inline void i2c_set_status(struct saa7134_dev *dev,
				  enum i2c_status status)
{
	i2c_dbg(2, "i2c stat => %s\n", str_i2c_status[status]);
	saa_andorb(SAA7134_I2C_ATTR_STATUS,0x0f,status);
}

static inline void i2c_set_attr(struct saa7134_dev *dev, enum i2c_attr attr)
{
	i2c_dbg(2, "i2c attr => %s\n", str_i2c_attr[attr]);
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

	i2c_dbg(2, "i2c reset\n");
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
	i2c_dbg(2, "i2c data => 0x%x\n", data);

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
	i2c_dbg(2, "i2c data <= 0x%x\n", data);
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

	i2c_dbg(2, "start xfer\n");
	i2c_dbg(1, "i2c xfer:");
	for (i = 0; i < num; i++) {
		if (!(msgs[i].flags & I2C_M_NOSTART) || 0 == i) {
			/* send address */
			i2c_dbg(2, "send address\n");
			addr  = msgs[i].addr << 1;
			if (msgs[i].flags & I2C_M_RD)
				addr |= 1;
			if (i > 0 && msgs[i].flags &
			    I2C_M_RD && msgs[i].addr != 0x40 &&
			    msgs[i].addr != 0x41 &&
			    msgs[i].addr != 0x19) {
				/* workaround for a saa7134 i2c bug
				 * needed to talk to the mt352 demux
				 * thanks to pinnacle for the hint */
				int quirk = 0xfe;
				i2c_cont(1, " [%02x quirk]", quirk);
				i2c_send_byte(dev,START,quirk);
				i2c_recv_byte(dev);
			}
			i2c_cont(1, " < %02x", addr);
			rc = i2c_send_byte(dev,START,addr);
			if (rc < 0)
				 goto err;
		}
		if (msgs[i].flags & I2C_M_RD) {
			/* read bytes */
			i2c_dbg(2, "read bytes\n");
			for (byte = 0; byte < msgs[i].len; byte++) {
				i2c_cont(1, " =");
				rc = i2c_recv_byte(dev);
				if (rc < 0)
					goto err;
				i2c_cont(1, "%02x", rc);
				msgs[i].buf[byte] = rc;
			}
			/* discard mysterious extra byte when reading
			   from Samsung S5H1411.  i2c bus gets error
			   if we do not. */
			if (0x19 == msgs[i].addr) {
				i2c_cont(1, " ?");
				rc = i2c_recv_byte(dev);
				if (rc < 0)
					goto err;
				i2c_cont(1, "%02x", rc);
			}
		} else {
			/* write bytes */
			i2c_dbg(2, "write bytes\n");
			for (byte = 0; byte < msgs[i].len; byte++) {
				data = msgs[i].buf[byte];
				i2c_cont(1, " %02x", data);
				rc = i2c_send_byte(dev,CONTINUE,data);
				if (rc < 0)
					goto err;
			}
		}
	}
	i2c_dbg(2, "xfer done\n");
	i2c_cont(1, " >");
	i2c_set_attr(dev,STOP);
	rc = -EIO;
	if (!i2c_is_busy_wait(dev))
		goto err;
	status = i2c_get_status(dev);
	if (i2c_is_error(status))
		goto err;
	/* ensure that the bus is idle for at least one bit slot */
	msleep(1);

	i2c_cont(1, "\n");
	return num;
 err:
	if (1 == i2c_debug) {
		status = i2c_get_status(dev);
		i2c_cont(1, " ERROR: %s\n", str_i2c_status[status]);
	}
	return rc;
}

/* ----------------------------------------------------------- */

static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm saa7134_algo = {
	.master_xfer   = saa7134_i2c_xfer,
	.functionality = functionality,
};

static const struct i2c_adapter saa7134_adap_template = {
	.owner         = THIS_MODULE,
	.name          = "saa7134",
	.algo          = &saa7134_algo,
};

static const struct i2c_client saa7134_client_template = {
	.name	= "saa7134 internal",
};

/* ----------------------------------------------------------- */

/* On Medion 7134 reading EEPROM needs DVB-T demod i2c gate open */
static void saa7134_i2c_eeprom_md7134_gate(struct saa7134_dev *dev)
{
	u8 subaddr = 0x7, dmdregval;
	u8 data[2];
	int ret;
	struct i2c_msg i2cgatemsg_r[] = { {.addr = 0x08, .flags = 0,
					   .buf = &subaddr, .len = 1},
					  {.addr = 0x08,
					   .flags = I2C_M_RD,
					   .buf = &dmdregval, .len = 1}
					};
	struct i2c_msg i2cgatemsg_w[] = { {.addr = 0x08, .flags = 0,
					   .buf = data, .len = 2} };

	ret = i2c_transfer(&dev->i2c_adap, i2cgatemsg_r, 2);
	if ((ret == 2) && (dmdregval & 0x2)) {
		pr_debug("%s: DVB-T demod i2c gate was left closed\n",
			 dev->name);

		data[0] = subaddr;
		data[1] = (dmdregval & ~0x2);
		if (i2c_transfer(&dev->i2c_adap, i2cgatemsg_w, 1) != 1)
			pr_err("%s: EEPROM i2c gate open failure\n",
			  dev->name);
	}
}

static int
saa7134_i2c_eeprom(struct saa7134_dev *dev, unsigned char *eedata, int len)
{
	unsigned char buf;
	int i,err;

	if (dev->board == SAA7134_BOARD_MD7134)
		saa7134_i2c_eeprom_md7134_gate(dev);

	dev->i2c_client.addr = 0xa0 >> 1;
	buf = 0;
	if (1 != (err = i2c_master_send(&dev->i2c_client,&buf,1))) {
		pr_info("%s: Huh, no eeprom present (err=%d)?\n",
		       dev->name,err);
		return -1;
	}
	if (len != (err = i2c_master_recv(&dev->i2c_client,eedata,len))) {
		pr_warn("%s: i2c eeprom read error (err=%d)\n",
		       dev->name,err);
		return -1;
	}

	for (i = 0; i < len; i += 16) {
		int size = (len - i) > 16 ? 16 : len - i;

		pr_info("i2c eeprom %02x: %*ph\n", i, size, &eedata[i]);
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

static void do_i2c_scan(struct i2c_client *c)
{
	unsigned char buf;
	int i,rc;

	for (i = 0; i < ARRAY_SIZE(i2c_devs); i++) {
		c->addr = i;
		rc = i2c_master_recv(c,&buf,0);
		if (rc < 0)
			continue;
		pr_info("i2c scan: found device @ 0x%x  [%s]\n",
			 i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

int saa7134_i2c_register(struct saa7134_dev *dev)
{
	dev->i2c_adap = saa7134_adap_template;
	dev->i2c_adap.dev.parent = &dev->pci->dev;
	strcpy(dev->i2c_adap.name,dev->name);
	dev->i2c_adap.algo_data = dev;
	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);
	i2c_add_adapter(&dev->i2c_adap);

	dev->i2c_client = saa7134_client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	saa7134_i2c_eeprom(dev,dev->eedata,sizeof(dev->eedata));
	if (i2c_scan)
		do_i2c_scan(&dev->i2c_client);

	/* Instantiate the IR receiver device, if present */
	saa7134_probe_i2c_ir(dev);
	return 0;
}

int saa7134_i2c_unregister(struct saa7134_dev *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}
