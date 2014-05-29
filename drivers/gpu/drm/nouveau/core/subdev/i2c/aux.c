/*
 * Copyright 2009 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "priv.h"

int
nv_rdaux(struct nouveau_i2c_port *port, u32 addr, u8 *data, u8 size)
{
	struct nouveau_i2c *i2c = nouveau_i2c(port);
	if (port->func->aux) {
		int ret = i2c->acquire(port, 0);
		if (ret == 0) {
			ret = port->func->aux(port, true, 9, addr, data, size);
			i2c->release(port);
		}
		return ret;
	}
	return -ENODEV;
}

int
nv_wraux(struct nouveau_i2c_port *port, u32 addr, u8 *data, u8 size)
{
	struct nouveau_i2c *i2c = nouveau_i2c(port);
	if (port->func->aux) {
		int ret = i2c->acquire(port, 0);
		if (ret == 0) {
			ret = port->func->aux(port, true, 8, addr, data, size);
			i2c->release(port);
		}
		return ret;
	}
	return -ENODEV;
}

static int
aux_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct nouveau_i2c_port *port = adap->algo_data;
	struct nouveau_i2c *i2c = nouveau_i2c(port);
	struct i2c_msg *msg = msgs;
	int ret, mcnt = num;

	if (!port->func->aux)
		return -ENODEV;

	ret = i2c->acquire(port, 0);
	if (ret)
		return ret;

	while (mcnt--) {
		u8 remaining = msg->len;
		u8 *ptr = msg->buf;

		while (remaining) {
			u8 cnt = (remaining > 16) ? 16 : remaining;
			u8 cmd;

			if (msg->flags & I2C_M_RD)
				cmd = 1;
			else
				cmd = 0;

			if (mcnt || remaining > 16)
				cmd |= 4; /* MOT */

			ret = port->func->aux(port, true, cmd, msg->addr, ptr, cnt);
			if (ret < 0) {
				i2c->release(port);
				return ret;
			}

			ptr += cnt;
			remaining -= cnt;
		}

		msg++;
	}

	i2c->release(port);
	return num;
}

static u32
aux_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

const struct i2c_algorithm nouveau_i2c_aux_algo = {
	.master_xfer = aux_xfer,
	.functionality = aux_func
};
