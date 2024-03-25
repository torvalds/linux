/* SPDX-License-Identifier: MIT */

#ifndef __AST_DDC_H__
#define __AST_DDC_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct drm_device;

struct ast_i2c_chan {
	struct i2c_adapter adapter;
	struct drm_device *dev;
	struct i2c_algo_bit_data bit;
};

struct ast_i2c_chan *ast_i2c_create(struct drm_device *dev);

#endif
