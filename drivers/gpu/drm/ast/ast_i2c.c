// SPDX-License-Identifier: MIT
/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "ast_drv.h"

static void ast_i2c_setsda(void *i2c_priv, int data)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_device *ast = to_ast_device(i2c->dev);
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((data & 0x01) ? 0 : 1) << 2;
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0xf1, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x04);
		if (ujcrb7 == jtemp)
			break;
	}
}

static void ast_i2c_setscl(void *i2c_priv, int clock)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_device *ast = to_ast_device(i2c->dev);
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((clock & 0x01) ? 0 : 1);
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0xf4, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x01);
		if (ujcrb7 == jtemp)
			break;
	}
}

static int ast_i2c_getsda(void *i2c_priv)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_device *ast = to_ast_device(i2c->dev);
	uint32_t val, val2, count, pass;

	count = 0;
	pass = 0;
	val = (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x20) >> 5) & 0x01;
	do {
		val2 = (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x20) >> 5) & 0x01;
		if (val == val2) {
			pass++;
		} else {
			pass = 0;
			val = (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x20) >> 5) & 0x01;
		}
	} while ((pass < 5) && (count++ < 0x10000));

	return val & 1 ? 1 : 0;
}

static int ast_i2c_getscl(void *i2c_priv)
{
	struct ast_i2c_chan *i2c = i2c_priv;
	struct ast_device *ast = to_ast_device(i2c->dev);
	uint32_t val, val2, count, pass;

	count = 0;
	pass = 0;
	val = (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x10) >> 4) & 0x01;
	do {
		val2 = (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x10) >> 4) & 0x01;
		if (val == val2) {
			pass++;
		} else {
			pass = 0;
			val = (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x10) >> 4) & 0x01;
		}
	} while ((pass < 5) && (count++ < 0x10000));

	return val & 1 ? 1 : 0;
}

static void ast_i2c_release(struct drm_device *dev, void *res)
{
	struct ast_i2c_chan *i2c = res;

	i2c_del_adapter(&i2c->adapter);
	kfree(i2c);
}

struct ast_i2c_chan *ast_i2c_create(struct drm_device *dev)
{
	struct ast_i2c_chan *i2c;
	int ret;

	i2c = kzalloc(sizeof(struct ast_i2c_chan), GFP_KERNEL);
	if (!i2c)
		return NULL;

	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.class = I2C_CLASS_DDC;
	i2c->adapter.dev.parent = dev->dev;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	snprintf(i2c->adapter.name, sizeof(i2c->adapter.name),
		 "AST i2c bit bus");
	i2c->adapter.algo_data = &i2c->bit;

	i2c->bit.udelay = 20;
	i2c->bit.timeout = 2;
	i2c->bit.data = i2c;
	i2c->bit.setsda = ast_i2c_setsda;
	i2c->bit.setscl = ast_i2c_setscl;
	i2c->bit.getsda = ast_i2c_getsda;
	i2c->bit.getscl = ast_i2c_getscl;
	ret = i2c_bit_add_bus(&i2c->adapter);
	if (ret) {
		drm_err(dev, "Failed to register bit i2c\n");
		goto out_kfree;
	}

	ret = drmm_add_action_or_reset(dev, ast_i2c_release, i2c);
	if (ret)
		return NULL;
	return i2c;

out_kfree:
	kfree(i2c);
	return NULL;
}
