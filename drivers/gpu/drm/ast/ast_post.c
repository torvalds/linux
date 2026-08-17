/*
 * Copyright 2012 Red Hat Inc.
 *
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
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_print.h>

#include "ast_drv.h"
#include "ast_post.h"

int ast_post_gpu(struct ast_device *ast)
{
	int ret;

	if (AST_GEN(ast) >= 7) {
		ret = ast_2600_post(ast);
		if (ret)
			return ret;
	} else if (AST_GEN(ast) >= 6) {
		ret = ast_2500_post(ast);
		if (ret)
			return ret;
	} else if (AST_GEN(ast) >= 4) {
		ret = ast_2300_post(ast);
		if (ret)
			return ret;
	} else  if (AST_GEN(ast) >= 2) {
		ret = ast_2100_post(ast);
		if (ret)
			return ret;
	} else  {
		ret = ast_2000_post(ast);
		if (ret)
			return ret;
	}

	return 0;
}

#define TIMEOUT              5000000

bool mmc_test(struct ast_device *ast, u32 datagen, u8 test_ctl)
{
	u32 data, timeout;

	ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
	ast_moutdwm(ast, AST_REG_MCR70, (datagen << 3) | test_ctl);
	timeout = 0;
	do {
		data = ast_mindwm(ast, AST_REG_MCR70) & 0x3000;
		if (data & 0x2000)
			return false;
		if (++timeout > TIMEOUT) {
			ast_moutdwm(ast, AST_REG_MCR70, 0x00000000);
			return false;
		}
	} while (!data);
	ast_moutdwm(ast, AST_REG_MCR70, 0x0);
	return true;
}

bool mmc_test_burst(struct ast_device *ast, u32 datagen)
{
	return mmc_test(ast, datagen, 0xc1);
}
