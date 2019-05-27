// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/i2c.h>

#include "stk1160.h"
#include "stk1160-reg.h"

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#define dprintk_i2c(fmt, args...)				\
do {								\
	if (i2c_debug)						\
		printk(KERN_DEBUG fmt, ##args);			\
} while (0)

static int stk1160_i2c_busy_wait(struct stk1160 *dev, u8 wait_bit_mask)
{
	unsigned long end;
	u8 flag;

	/* Wait until read/write finish bit is set */
	end = jiffies + msecs_to_jiffies(STK1160_I2C_TIMEOUT);
	while (time_is_after_jiffies(end)) {

		stk1160_read_reg(dev, STK1160_SICTL+1, &flag);
		/* read/write done? */
		if (flag & wait_bit_mask)
			goto done;

		usleep_range(10 * USEC_PER_MSEC, 20 * USEC_PER_MSEC);
	}

	return -ETIMEDOUT;

done:
	return 0;
}

static int stk1160_i2c_write_reg(struct stk1160 *dev, u8 addr,
		u8 reg, u8 value)
{
	int rc;

	/* Set serial device address */
	rc = stk1160_write_reg(dev, STK1160_SICTL_SDA, addr);
	if (rc < 0)
		return rc;

	/* Set i2c device register sub-address */
	rc = stk1160_write_reg(dev, STK1160_SBUSW_WA, reg);
	if (rc < 0)
		return rc;

	/* Set i2c device register value */
	rc = stk1160_write_reg(dev, STK1160_SBUSW_WD, value);
	if (rc < 0)
		return rc;

	/* Start write now */
	rc = stk1160_write_reg(dev, STK1160_SICTL, 0x01);
	if (rc < 0)
		return rc;

	rc = stk1160_i2c_busy_wait(dev, 0x04);
	if (rc < 0)
		return rc;

	return 0;
}

static int stk1160_i2c_read_reg(struct stk1160 *dev, u8 addr,
		u8 reg, u8 *value)
{
	int rc;

	/* Set serial device address */
	rc = stk1160_write_reg(dev, STK1160_SICTL_SDA, addr);
	if (rc < 0)
		return rc;

	/* Set i2c device register sub-address */
	rc = stk1160_write_reg(dev, STK1160_SBUSR_RA, reg);
	if (rc < 0)
		return rc;

	/* Start read now */
	rc = stk1160_write_reg(dev, STK1160_SICTL, 0x20);
	if (rc < 0)
		return rc;

	rc = stk1160_i2c_busy_wait(dev, 0x01);
	if (rc < 0)
		return rc;

	rc = stk1160_read_reg(dev, STK1160_SBUSR_RD, value);
	if (rc < 0)
		return rc;

	return 0;
}

/*
 * stk1160_i2c_check_for_device()
 * check if there is a i2c_device at the supplied address
 */
static int stk1160_i2c_check_for_device(struct stk1160 *dev,
		unsigned char addr)
{
	int rc;

	/* Set serial device address */
	rc = stk1160_write_reg(dev, STK1160_SICTL_SDA, addr);
	if (rc < 0)
		return rc;

	/* Set device sub-address, we'll chip version reg */
	rc = stk1160_write_reg(dev, STK1160_SBUSR_RA, 0x00);
	if (rc < 0)
		return rc;

	/* Start read now */
	rc = stk1160_write_reg(dev, STK1160_SICTL, 0x20);
	if (rc < 0)
		return rc;

	rc = stk1160_i2c_busy_wait(dev, 0x01);
	if (rc < 0)
		return -ENODEV;

	return 0;
}

/*
 * stk1160_i2c_xfer()
 * the main i2c transfer function
 */
static int stk1160_i2c_xfer(struct i2c_adapter *i2c_adap,
			   struct i2c_msg msgs[], int num)
{
	struct stk1160 *dev = i2c_adap->algo_data;
	int addr, rc, i;

	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		dprintk_i2c("%s: addr=%x", __func__, addr);

		if (!msgs[i].len) {
			/* no len: check only for device presence */
			rc = stk1160_i2c_check_for_device(dev, addr);
			if (rc < 0) {
				dprintk_i2c(" no device\n");
				return rc;
			}

		} else if (msgs[i].flags & I2C_M_RD) {
			/* read request without preceding register selection */
			dprintk_i2c(" subaddr not selected");
			rc = -EOPNOTSUPP;
			goto err;

		} else if (i + 1 < num && msgs[i].len <= 2 &&
			   (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {

			if (msgs[i].len != 1 || msgs[i + 1].len != 1) {
				dprintk_i2c(" len not supported");
				rc = -EOPNOTSUPP;
				goto err;
			}

			dprintk_i2c(" subaddr=%x", msgs[i].buf[0]);

			rc = stk1160_i2c_read_reg(dev, addr, msgs[i].buf[0],
				msgs[i + 1].buf);

			dprintk_i2c(" read=%x", *msgs[i + 1].buf);

			/* consumed two msgs, so we skip one of them */
			i++;

		} else {
			if (msgs[i].len != 2) {
				dprintk_i2c(" len not supported");
				rc = -EOPNOTSUPP;
				goto err;
			}

			dprintk_i2c(" subaddr=%x write=%x",
				msgs[i].buf[0],  msgs[i].buf[1]);

			rc = stk1160_i2c_write_reg(dev, addr, msgs[i].buf[0],
				msgs[i].buf[1]);
		}

		if (rc < 0)
			goto err;
		dprintk_i2c(" OK\n");
	}

	return num;
err:
	dprintk_i2c(" ERROR: %d\n", rc);
	return num;
}

/*
 * functionality(), what da heck is this?
 */
static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm algo = {
	.master_xfer   = stk1160_i2c_xfer,
	.functionality = functionality,
};

static const struct i2c_adapter adap_template = {
	.owner = THIS_MODULE,
	.name = "stk1160",
	.algo = &algo,
};

static const struct i2c_client client_template = {
	.name = "stk1160 internal",
};

/*
 * stk1160_i2c_register()
 * register i2c bus
 */
int stk1160_i2c_register(struct stk1160 *dev)
{
	int rc;

	dev->i2c_adap = adap_template;
	dev->i2c_adap.dev.parent = dev->dev;
	strscpy(dev->i2c_adap.name, "stk1160", sizeof(dev->i2c_adap.name));
	dev->i2c_adap.algo_data = dev;

	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);

	rc = i2c_add_adapter(&dev->i2c_adap);
	if (rc < 0) {
		stk1160_err("cannot add i2c adapter (%d)\n", rc);
		return rc;
	}

	dev->i2c_client = client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	/* Set i2c clock divider device address */
	stk1160_write_reg(dev, STK1160_SICTL_CD,  0x0f);

	/* ??? */
	stk1160_write_reg(dev, STK1160_ASIC + 3,  0x00);

	return 0;
}

/*
 * stk1160_i2c_unregister()
 * unregister i2c_bus
 */
int stk1160_i2c_unregister(struct stk1160 *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}
