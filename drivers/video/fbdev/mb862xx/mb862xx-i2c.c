/*
 * Coral-P(A)/Lime I2C adapter driver
 *
 * (C) 2011 DENX Software Engineering, Anatolij Gustschin <agust@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/export.h>

#include "mb862xxfb.h"
#include "mb862xx_reg.h"

static int mb862xx_i2c_wait_event(struct i2c_adapter *adap)
{
	struct mb862xxfb_par *par = adap->algo_data;
	u32 reg;

	do {
		udelay(10);
		reg = inreg(i2c, GC_I2C_BCR);
		if (reg & (I2C_INT | I2C_BER))
			break;
	} while (1);

	return (reg & I2C_BER) ? 0 : 1;
}

static int mb862xx_i2c_do_address(struct i2c_adapter *adap, int addr)
{
	struct mb862xxfb_par *par = adap->algo_data;

	outreg(i2c, GC_I2C_DAR, addr);
	outreg(i2c, GC_I2C_CCR, I2C_CLOCK_AND_ENABLE);
	outreg(i2c, GC_I2C_BCR, par->i2c_rs ? I2C_REPEATED_START : I2C_START);
	if (!mb862xx_i2c_wait_event(adap))
		return -EIO;
	par->i2c_rs = !(inreg(i2c, GC_I2C_BSR) & I2C_LRB);
	return par->i2c_rs;
}

static int mb862xx_i2c_write_byte(struct i2c_adapter *adap, u8 byte)
{
	struct mb862xxfb_par *par = adap->algo_data;

	outreg(i2c, GC_I2C_DAR, byte);
	outreg(i2c, GC_I2C_BCR, I2C_START);
	if (!mb862xx_i2c_wait_event(adap))
		return -EIO;
	return !(inreg(i2c, GC_I2C_BSR) & I2C_LRB);
}

static int mb862xx_i2c_read_byte(struct i2c_adapter *adap, u8 *byte, int last)
{
	struct mb862xxfb_par *par = adap->algo_data;

	outreg(i2c, GC_I2C_BCR, I2C_START | (last ? 0 : I2C_ACK));
	if (!mb862xx_i2c_wait_event(adap))
		return 0;
	*byte = inreg(i2c, GC_I2C_DAR);
	return 1;
}

static void mb862xx_i2c_stop(struct i2c_adapter *adap)
{
	struct mb862xxfb_par *par = adap->algo_data;

	outreg(i2c, GC_I2C_BCR, I2C_STOP);
	outreg(i2c, GC_I2C_CCR, I2C_DISABLE);
	par->i2c_rs = 0;
}

static int mb862xx_i2c_read(struct i2c_adapter *adap, struct i2c_msg *m)
{
	int i, ret = 0;
	int last = m->len - 1;

	for (i = 0; i < m->len; i++) {
		if (!mb862xx_i2c_read_byte(adap, &m->buf[i], i == last)) {
			ret = -EIO;
			break;
		}
	}
	return ret;
}

static int mb862xx_i2c_write(struct i2c_adapter *adap, struct i2c_msg *m)
{
	int i, ret = 0;

	for (i = 0; i < m->len; i++) {
		if (!mb862xx_i2c_write_byte(adap, m->buf[i])) {
			ret = -EIO;
			break;
		}
	}
	return ret;
}

static int mb862xx_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			int num)
{
	struct mb862xxfb_par *par = adap->algo_data;
	struct i2c_msg *m;
	int addr;
	int i = 0, err = 0;

	dev_dbg(par->dev, "%s: %d msgs\n", __func__, num);

	for (i = 0; i < num; i++) {
		m = &msgs[i];
		if (!m->len) {
			dev_dbg(par->dev, "%s: null msgs\n", __func__);
			continue;
		}
		addr = m->addr;
		if (m->flags & I2C_M_RD)
			addr |= 1;

		err = mb862xx_i2c_do_address(adap, addr);
		if (err < 0)
			break;
		if (m->flags & I2C_M_RD)
			err = mb862xx_i2c_read(adap, m);
		else
			err = mb862xx_i2c_write(adap, m);
	}

	if (i)
		mb862xx_i2c_stop(adap);

	return (err < 0) ? err : i;
}

static u32 mb862xx_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_BYTE_DATA;
}

static const struct i2c_algorithm mb862xx_algo = {
	.master_xfer	= mb862xx_xfer,
	.functionality	= mb862xx_func,
};

static struct i2c_adapter mb862xx_i2c_adapter = {
	.name		= "MB862xx I2C adapter",
	.algo		= &mb862xx_algo,
	.owner		= THIS_MODULE,
};

int mb862xx_i2c_init(struct mb862xxfb_par *par)
{
	mb862xx_i2c_adapter.algo_data = par;
	par->adap = &mb862xx_i2c_adapter;

	return i2c_add_adapter(par->adap);
}

void mb862xx_i2c_exit(struct mb862xxfb_par *par)
{
	if (par->adap) {
		i2c_del_adapter(par->adap);
		par->adap = NULL;
	}
}
