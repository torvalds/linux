// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Auvitek AU0828 USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 */

#include "au0828.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "media/tuner.h"
#include <media/v4l2-common.h>

static int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

#define I2C_WAIT_DELAY 25
#define I2C_WAIT_RETRY 1000

static inline int i2c_slave_did_read_ack(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, AU0828_I2C_STATUS_201) &
		AU0828_I2C_STATUS_NO_READ_ACK ? 0 : 1;
}

static int i2c_wait_read_ack(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_slave_did_read_ack(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static inline int i2c_is_read_busy(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, AU0828_I2C_STATUS_201) &
		AU0828_I2C_STATUS_READ_DONE ? 0 : 1;
}

static int i2c_wait_read_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_is_read_busy(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static inline int i2c_is_write_done(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, AU0828_I2C_STATUS_201) &
		AU0828_I2C_STATUS_WRITE_DONE ? 1 : 0;
}

static int i2c_wait_write_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (i2c_is_write_done(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static inline int i2c_is_busy(struct i2c_adapter *i2c_adap)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	return au0828_read(dev, AU0828_I2C_STATUS_201) &
		AU0828_I2C_STATUS_BUSY ? 1 : 0;
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

/* FIXME: Implement join handling correctly */
static int i2c_sendbytes(struct i2c_adapter *i2c_adap,
	const struct i2c_msg *msg, int joined_rlen)
{
	int i, strobe = 0;
	struct au0828_dev *dev = i2c_adap->algo_data;
	u8 i2c_speed = dev->board.i2c_clk_divider;

	dprintk(4, "%s()\n", __func__);

	au0828_write(dev, AU0828_I2C_MULTIBYTE_MODE_2FF, 0x01);

	if (((dev->board.tuner_type == TUNER_XC5000) ||
	     (dev->board.tuner_type == TUNER_XC5000C)) &&
	    (dev->board.tuner_addr == msg->addr)) {
		/*
		 * Due to I2C clock stretch, we need to use a lower speed
		 * on xc5000 for commands. However, firmware transfer can
		 * speed up to 400 KHz.
		 */
		if (msg->len == 64)
			i2c_speed = AU0828_I2C_CLK_250KHZ;
		else
			i2c_speed = AU0828_I2C_CLK_20KHZ;
	}
	/* Set the I2C clock */
	au0828_write(dev, AU0828_I2C_CLK_DIVIDER_202, i2c_speed);

	/* Hardware needs 8 bit addresses */
	au0828_write(dev, AU0828_I2C_DEST_ADDR_203, msg->addr << 1);

	dprintk(4, "SEND: %02x\n", msg->addr);

	/* Deal with i2c_scan */
	if (msg->len == 0) {
		/* The analog tuner detection code makes use of the SMBUS_QUICK
		   message (which involves a zero length i2c write).  To avoid
		   checking the status register when we didn't strobe out any
		   actual bytes to the bus, just do a read check.  This is
		   consistent with how I saw i2c device checking done in the
		   USB trace of the Windows driver */
		au0828_write(dev, AU0828_I2C_TRIGGER_200,
			     AU0828_I2C_TRIGGER_READ);

		if (!i2c_wait_done(i2c_adap))
			return -EIO;

		if (i2c_wait_read_ack(i2c_adap))
			return -EIO;

		return 0;
	}

	for (i = 0; i < msg->len;) {

		dprintk(4, " %02x\n", msg->buf[i]);

		au0828_write(dev, AU0828_I2C_WRITE_FIFO_205, msg->buf[i]);

		strobe++;
		i++;

		if ((strobe >= 4) || (i >= msg->len)) {

			/* Strobe the byte into the bus */
			if (i < msg->len)
				au0828_write(dev, AU0828_I2C_TRIGGER_200,
					     AU0828_I2C_TRIGGER_WRITE |
					     AU0828_I2C_TRIGGER_HOLD);
			else
				au0828_write(dev, AU0828_I2C_TRIGGER_200,
					     AU0828_I2C_TRIGGER_WRITE);

			/* Reset strobe trigger */
			strobe = 0;

			if (!i2c_wait_write_done(i2c_adap))
				return -EIO;

		}

	}
	if (!i2c_wait_done(i2c_adap))
		return -EIO;

	dprintk(4, "\n");

	return msg->len;
}

/* FIXME: Implement join handling correctly */
static int i2c_readbytes(struct i2c_adapter *i2c_adap,
	const struct i2c_msg *msg, int joined)
{
	struct au0828_dev *dev = i2c_adap->algo_data;
	u8 i2c_speed = dev->board.i2c_clk_divider;
	int i;

	dprintk(4, "%s()\n", __func__);

	au0828_write(dev, AU0828_I2C_MULTIBYTE_MODE_2FF, 0x01);

	/*
	 * Due to xc5000c clock stretch, we cannot use full speed at
	 * readings from xc5000, as otherwise they'll fail.
	 */
	if (((dev->board.tuner_type == TUNER_XC5000) ||
	     (dev->board.tuner_type == TUNER_XC5000C)) &&
	    (dev->board.tuner_addr == msg->addr))
		i2c_speed = AU0828_I2C_CLK_20KHZ;

	/* Set the I2C clock */
	au0828_write(dev, AU0828_I2C_CLK_DIVIDER_202, i2c_speed);

	/* Hardware needs 8 bit addresses */
	au0828_write(dev, AU0828_I2C_DEST_ADDR_203, msg->addr << 1);

	dprintk(4, " RECV:\n");

	/* Deal with i2c_scan */
	if (msg->len == 0) {
		au0828_write(dev, AU0828_I2C_TRIGGER_200,
			     AU0828_I2C_TRIGGER_READ);

		if (i2c_wait_read_ack(i2c_adap))
			return -EIO;
		return 0;
	}

	for (i = 0; i < msg->len;) {

		i++;

		if (i < msg->len)
			au0828_write(dev, AU0828_I2C_TRIGGER_200,
				     AU0828_I2C_TRIGGER_READ |
				     AU0828_I2C_TRIGGER_HOLD);
		else
			au0828_write(dev, AU0828_I2C_TRIGGER_200,
				     AU0828_I2C_TRIGGER_READ);

		if (!i2c_wait_read_done(i2c_adap))
			return -EIO;

		msg->buf[i-1] = au0828_read(dev, AU0828_I2C_READ_FIFO_209) &
			0xff;

		dprintk(4, " %02x\n", msg->buf[i-1]);
	}
	if (!i2c_wait_done(i2c_adap))
		return -EIO;

	dprintk(4, "\n");

	return msg->len;
}

static int i2c_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg *msgs, int num)
{
	int i, retval = 0;

	dprintk(4, "%s(num = %d)\n", __func__, num);

	for (i = 0; i < num; i++) {
		dprintk(4, "%s(num = %d) addr = 0x%02x  len = 0x%x\n",
			__func__, num, msgs[i].addr, msgs[i].len);
		if (msgs[i].flags & I2C_M_RD) {
			/* read */
			retval = i2c_readbytes(i2c_adap, &msgs[i], 0);
		} else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {
			/* write then read from same address */
			retval = i2c_sendbytes(i2c_adap, &msgs[i],
					       msgs[i + 1].len);
			if (retval < 0)
				goto err;
			i++;
			retval = i2c_readbytes(i2c_adap, &msgs[i], 1);
		} else {
			/* write */
			retval = i2c_sendbytes(i2c_adap, &msgs[i], 0);
		}
		if (retval < 0)
			goto err;
	}
	return num;

err:
	return retval;
}

static u32 au0828_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static const struct i2c_algorithm au0828_i2c_algo_template = {
	.master_xfer	= i2c_xfer,
	.functionality	= au0828_functionality,
};

/* ----------------------------------------------------------------------- */

static const struct i2c_adapter au0828_i2c_adap_template = {
	.name              = KBUILD_MODNAME,
	.owner             = THIS_MODULE,
	.algo              = &au0828_i2c_algo_template,
};

static const struct i2c_client au0828_i2c_client_template = {
	.name	= "au0828 internal",
};

static char *i2c_devs[128] = {
	[0x8e >> 1] = "au8522",
	[0xa0 >> 1] = "eeprom",
	[0xc2 >> 1] = "tuner/xc5000",
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
		pr_info("%s: i2c scan: found device @ 0x%x  [%s]\n",
		       name, i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/* init + register i2c adapter */
int au0828_i2c_register(struct au0828_dev *dev)
{
	dprintk(1, "%s()\n", __func__);

	dev->i2c_adap = au0828_i2c_adap_template;
	dev->i2c_algo = au0828_i2c_algo_template;
	dev->i2c_client = au0828_i2c_client_template;

	dev->i2c_adap.dev.parent = &dev->usbdev->dev;

	strscpy(dev->i2c_adap.name, KBUILD_MODNAME,
		sizeof(dev->i2c_adap.name));

	dev->i2c_adap.algo = &dev->i2c_algo;
	dev->i2c_adap.algo_data = dev;
#ifdef CONFIG_VIDEO_AU0828_V4L2
	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);
#else
	i2c_set_adapdata(&dev->i2c_adap, dev);
#endif
	i2c_add_adapter(&dev->i2c_adap);

	dev->i2c_client.adapter = &dev->i2c_adap;

	if (0 == dev->i2c_rc) {
		pr_info("i2c bus registered\n");
		if (i2c_scan)
			do_i2c_scan(KBUILD_MODNAME, &dev->i2c_client);
	} else
		pr_info("i2c bus register FAILED\n");

	return dev->i2c_rc;
}

int au0828_i2c_unregister(struct au0828_dev *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}

