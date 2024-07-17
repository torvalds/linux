/* SPDX-License-Identifier: MIT */

#ifndef __AST_DDC_H__
#define __AST_DDC_H__

struct ast_device;
struct i2c_adapter;

struct i2c_adapter *ast_ddc_create(struct ast_device *ast);

#endif
