/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "include/i2caux_interface.h"
#include "../i2caux.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_sw_engine.h"
#include "../i2c_hw_engine.h"

#include "../dce110/i2caux_dce110.h"
#include "i2caux_dce112.h"

#include "../dce110/aux_engine_dce110.h"

#include "../dce110/i2c_hw_engine_dce110.h"

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

/* set register offset */
#define SR(reg_name)\
	.reg_name = mm ## reg_name

/* set register offset with instance */
#define SRI(reg_name, block, id)\
	.reg_name = mm ## block ## id ## _ ## reg_name

#define aux_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST(id), \
	.AUX_RESET_MASK = AUX_CONTROL__AUX_RESET_MASK \
}

#define hw_engine_regs(id)\
{\
		I2C_HW_ENGINE_COMMON_REG_LIST(id) \
}

static const struct dce110_aux_registers dce112_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4),
		aux_regs(5),
};

static const struct dce110_i2c_hw_engine_registers dce112_hw_engine_regs[] = {
		hw_engine_regs(1),
		hw_engine_regs(2),
		hw_engine_regs(3),
		hw_engine_regs(4),
		hw_engine_regs(5),
		hw_engine_regs(6)
};

static const struct dce110_i2c_hw_engine_shift i2c_shift = {
		I2C_COMMON_MASK_SH_LIST_DCE110(__SHIFT)
};

static const struct dce110_i2c_hw_engine_mask i2c_mask = {
		I2C_COMMON_MASK_SH_LIST_DCE110(_MASK)
};

static void construct(
	struct i2caux_dce110 *i2caux_dce110,
	struct dc_context *ctx)
{
	dal_i2caux_dce110_construct(i2caux_dce110,
				    ctx,
				    ARRAY_SIZE(dce112_aux_regs),
				    dce112_aux_regs,
				    dce112_hw_engine_regs,
				    &i2c_shift,
				    &i2c_mask);
}

/*
 * dal_i2caux_dce110_create
 *
 * @brief
 * public interface to allocate memory for DCE11 I2CAUX
 *
 * @param
 * struct adapter_service *as - [in]
 * struct dc_context *ctx - [in]
 *
 * @return
 * pointer to the base struct of DCE11 I2CAUX
 */
struct i2caux *dal_i2caux_dce112_create(
	struct dc_context *ctx)
{
	struct i2caux_dce110 *i2caux_dce110 =
		kzalloc(sizeof(struct i2caux_dce110), GFP_KERNEL);

	if (!i2caux_dce110) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	construct(i2caux_dce110, ctx);
	return &i2caux_dce110->base;
}
