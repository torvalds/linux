/* drivers/i2c/busses/i2c-rk30-adapter.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "i2c-rk30.h"

static void  rk30_i2c_set_clk(struct rk30_i2c *i2c, unsigned long scl_rate)
{
    return;
}
static void rk30_i2c_init_hw(struct rk30_i2c *i2c)
{
	return;
}


static irqreturn_t rk30_i2c_irq(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}


static int rk30_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
    return 0;
}

/* declare our i2c functionality */
static u32 rk30_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

/* i2c bus registration info */

static const struct i2c_algorithm rk30_i2c_algorithm = {
	.master_xfer		= rk30_i2c_xfer,
	.functionality		= rk30_i2c_func,
};

int i2c_add_rk30_adapter(struct i2c_adapter *adap)
{
    int ret = 0;
    struct rk30_i2c *i2c = (struct rk30_i2c *)adap->algo_data;

    adap->algo = &rk30_i2c_algorithm;
	adap->retries = 3;

    i2c->i2c_init_hw = &rk30_i2c_init_hw;
    i2c->i2c_set_clk = &rk30_i2c_set_clk;
    i2c->i2c_irq = &rk30_i2c_irq;

    ret = i2c_add_numbered_adapter(adap);

    return ret;
}


