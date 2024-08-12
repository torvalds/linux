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

#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "ast_ddc.h"
#include "ast_drv.h"

struct ast_ddc {
	struct ast_device *ast;

	struct i2c_algo_bit_data bit;
	struct i2c_adapter adapter;
};

static void ast_ddc_algo_bit_data_setsda(void *data, int state)
{
	struct ast_ddc *ddc = data;
	struct ast_device *ast = ddc->ast;
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((state & 0x01) ? 0 : 1) << 2;
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0xf1, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x04);
		if (ujcrb7 == jtemp)
			break;
	}
}

static void ast_ddc_algo_bit_data_setscl(void *data, int state)
{
	struct ast_ddc *ddc = data;
	struct ast_device *ast = ddc->ast;
	int i;
	u8 ujcrb7, jtemp;

	for (i = 0; i < 0x10000; i++) {
		ujcrb7 = ((state & 0x01) ? 0 : 1);
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0xf4, ujcrb7);
		jtemp = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x01);
		if (ujcrb7 == jtemp)
			break;
	}
}

static int ast_ddc_algo_bit_data_pre_xfer(struct i2c_adapter *adapter)
{
	struct ast_ddc *ddc = i2c_get_adapdata(adapter);
	struct ast_device *ast = ddc->ast;

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&ast->modeset_lock);

	return 0;
}

static void ast_ddc_algo_bit_data_post_xfer(struct i2c_adapter *adapter)
{
	struct ast_ddc *ddc = i2c_get_adapdata(adapter);
	struct ast_device *ast = ddc->ast;

	mutex_unlock(&ast->modeset_lock);
}

static int ast_ddc_algo_bit_data_getsda(void *data)
{
	struct ast_ddc *ddc = data;
	struct ast_device *ast = ddc->ast;
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

static int ast_ddc_algo_bit_data_getscl(void *data)
{
	struct ast_ddc *ddc = data;
	struct ast_device *ast = ddc->ast;
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

static void ast_ddc_release(struct drm_device *dev, void *res)
{
	struct ast_ddc *ddc = res;

	i2c_del_adapter(&ddc->adapter);
}

struct i2c_adapter *ast_ddc_create(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct ast_ddc *ddc;
	struct i2c_adapter *adapter;
	struct i2c_algo_bit_data *bit;
	int ret;

	ddc = drmm_kzalloc(dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return ERR_PTR(-ENOMEM);
	ddc->ast = ast;

	bit = &ddc->bit;
	bit->data = ddc;
	bit->setsda = ast_ddc_algo_bit_data_setsda;
	bit->setscl = ast_ddc_algo_bit_data_setscl;
	bit->getsda = ast_ddc_algo_bit_data_getsda;
	bit->getscl = ast_ddc_algo_bit_data_getscl;
	bit->pre_xfer = ast_ddc_algo_bit_data_pre_xfer;
	bit->post_xfer = ast_ddc_algo_bit_data_post_xfer;
	bit->udelay = 20;
	bit->timeout = usecs_to_jiffies(2200);

	adapter = &ddc->adapter;
	adapter->owner = THIS_MODULE;
	adapter->algo_data = bit;
	adapter->dev.parent = dev->dev;
	snprintf(adapter->name, sizeof(adapter->name), "AST DDC bus");
	i2c_set_adapdata(adapter, ddc);

	ret = i2c_bit_add_bus(adapter);
	if (ret) {
		drm_err(dev, "Failed to register bit i2c\n");
		return ERR_PTR(ret);
	}

	ret = drmm_add_action_or_reset(dev, ast_ddc_release, ddc);
	if (ret)
		return ERR_PTR(ret);

	return &ddc->adapter;
}
