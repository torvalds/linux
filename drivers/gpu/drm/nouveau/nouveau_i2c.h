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
 */

#ifndef __NOUVEAU_I2C_H__
#define __NOUVEAU_I2C_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include "drm_dp_helper.h"

#define NV_I2C_PORT(n)    (0x00 + (n))
#define NV_I2C_PORT_NUM    0x10
#define NV_I2C_DEFAULT(n) (0x80 + (n))

struct nouveau_i2c_chan {
	struct i2c_adapter adapter;
	struct drm_device *dev;
	struct list_head head;
	u8  index;
	u8  type;
	u32 dcb;
	u32 drive;
	u32 sense;
	u32 state;
};

int  nouveau_i2c_init(struct drm_device *);
void nouveau_i2c_fini(struct drm_device *);
struct nouveau_i2c_chan *nouveau_i2c_find(struct drm_device *, u8 index);
bool nouveau_probe_i2c_addr(struct nouveau_i2c_chan *i2c, int addr);
int nouveau_i2c_identify(struct drm_device *dev, const char *what,
			 struct i2c_board_info *info,
			 bool (*match)(struct nouveau_i2c_chan *,
				       struct i2c_board_info *),
			 int index);

extern const struct i2c_algorithm nouveau_dp_i2c_algo;

#endif /* __NOUVEAU_I2C_H__ */
