/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "dm_services.h"

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/i2caux_interface.h"
#include "../i2caux.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_sw_engine.h"
#include "../i2c_hw_engine.h"

/*
 * Header of this unit
 */
#include "i2caux_diag.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

static void destruct(
	struct i2caux *i2caux)
{
	dal_i2caux_destruct(i2caux);
}

static void destroy(
	struct i2caux **i2c_engine)
{
	destruct(*i2c_engine);

	kfree(*i2c_engine);

	*i2c_engine = NULL;
}

/* function table */
static const struct i2caux_funcs i2caux_funcs = {
	.destroy = destroy,
	.acquire_i2c_hw_engine = NULL,
	.release_engine = NULL,
	.acquire_i2c_sw_engine = NULL,
	.acquire_aux_engine = NULL,
};

static void construct(
	struct i2caux *i2caux,
	struct dc_context *ctx)
{
	dal_i2caux_construct(i2caux, ctx);
	i2caux->funcs = &i2caux_funcs;
}

struct i2caux *dal_i2caux_diag_fpga_create(
	struct dc_context *ctx)
{
	struct i2caux *i2caux =	kzalloc(sizeof(struct i2caux),
					       GFP_KERNEL);

	if (!i2caux) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	construct(i2caux, ctx);
	return i2caux;
}
