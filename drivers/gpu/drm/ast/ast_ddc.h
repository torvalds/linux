/* SPDX-License-Identifier: MIT */

#ifndef __AST_DDC_H__
#define __AST_DDC_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct ast_device;

struct ast_ddc {
	struct ast_device *ast;

	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit;
};

struct ast_ddc *ast_ddc_create(struct ast_device *ast);

#endif
