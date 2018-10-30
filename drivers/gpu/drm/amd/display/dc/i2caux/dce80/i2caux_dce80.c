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

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/i2caux_interface.h"
#include "../i2caux.h"

/*
 * Header of this unit
 */

#include "i2caux_dce80.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_sw_engine.h"
#include "i2c_sw_engine_dce80.h"
#include "../i2c_hw_engine.h"
#include "i2c_hw_engine_dce80.h"
#include "../i2c_generic_hw_engine.h"
#include "../aux_engine.h"


#include "../dce110/aux_engine_dce110.h"
#include "../dce110/i2caux_dce110.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"


/* set register offset */
#define SR(reg_name)\
	.reg_name = mm ## reg_name

/* set register offset with instance */
#define SRI(reg_name, block, id)\
	.reg_name = mm ## block ## id ## _ ## reg_name

#define aux_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST(id), \
	.AUX_RESET_MASK = 0 \
}

static const struct dce110_aux_registers dce80_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4),
		aux_regs(5)
};

/*
 * This unit
 */

#define FROM_I2C_AUX(ptr) \
	container_of((ptr), struct i2caux_dce80, base)

static void destruct(
	struct i2caux_dce80 *i2caux_dce80)
{
	dal_i2caux_destruct(&i2caux_dce80->base);
}

static void destroy(
	struct i2caux **i2c_engine)
{
	struct i2caux_dce80 *i2caux_dce80 = FROM_I2C_AUX(*i2c_engine);

	destruct(i2caux_dce80);

	kfree(i2caux_dce80);

	*i2c_engine = NULL;
}

static struct i2c_engine *acquire_i2c_hw_engine(
	struct i2caux *i2caux,
	struct ddc *ddc)
{
	struct i2caux_dce80 *i2caux_dce80 = FROM_I2C_AUX(i2caux);

	struct i2c_engine *engine = NULL;
	bool non_generic;

	if (!ddc)
		return NULL;

	if (ddc->hw_info.hw_supported) {
		enum gpio_ddc_line line = dal_ddc_get_line(ddc);

		if (line < GPIO_DDC_LINE_COUNT) {
			non_generic = true;
			engine = i2caux->i2c_hw_engines[line];
		}
	}

	if (!engine) {
		non_generic = false;
		engine = i2caux->i2c_generic_hw_engine;
	}

	if (!engine)
		return NULL;

	if (non_generic) {
		if (!i2caux_dce80->i2c_hw_buffer_in_use &&
			engine->base.funcs->acquire(&engine->base, ddc)) {
			i2caux_dce80->i2c_hw_buffer_in_use = true;
			return engine;
		}
	} else {
		if (engine->base.funcs->acquire(&engine->base, ddc))
			return engine;
	}

	return NULL;
}

static void release_engine(
	struct i2caux *i2caux,
	struct engine *engine)
{
	if (engine->funcs->get_engine_type(engine) ==
		I2CAUX_ENGINE_TYPE_I2C_DDC_HW)
		FROM_I2C_AUX(i2caux)->i2c_hw_buffer_in_use = false;

	dal_i2caux_release_engine(i2caux, engine);
}

static const enum gpio_ddc_line hw_ddc_lines[] = {
	GPIO_DDC_LINE_DDC1,
	GPIO_DDC_LINE_DDC2,
	GPIO_DDC_LINE_DDC3,
	GPIO_DDC_LINE_DDC4,
	GPIO_DDC_LINE_DDC5,
	GPIO_DDC_LINE_DDC6,
	GPIO_DDC_LINE_DDC_VGA
};

static const enum gpio_ddc_line hw_aux_lines[] = {
	GPIO_DDC_LINE_DDC1,
	GPIO_DDC_LINE_DDC2,
	GPIO_DDC_LINE_DDC3,
	GPIO_DDC_LINE_DDC4,
	GPIO_DDC_LINE_DDC5,
	GPIO_DDC_LINE_DDC6
};

static const struct i2caux_funcs i2caux_funcs = {
	.destroy = destroy,
	.acquire_i2c_hw_engine = acquire_i2c_hw_engine,
	.release_engine = release_engine,
	.acquire_i2c_sw_engine = dal_i2caux_acquire_i2c_sw_engine,
	.acquire_aux_engine = dal_i2caux_acquire_aux_engine,
};

static void construct(
	struct i2caux_dce80 *i2caux_dce80,
	struct dc_context *ctx)
{
	/* Entire family have I2C engine reference clock frequency
	 * changed from XTALIN (27) to XTALIN/2 (13.5) */

	struct i2caux *base = &i2caux_dce80->base;

	uint32_t reference_frequency =
		dal_i2caux_get_reference_clock(ctx->dc_bios) >> 1;

	/*bool use_i2c_sw_engine = dal_adapter_service_is_feature_supported(as,
		FEATURE_RESTORE_USAGE_I2C_SW_ENGINE);*/

	/* Use SWI2C for dce8 currently, sicne we have bug with hwi2c */
	bool use_i2c_sw_engine = true;

	uint32_t i;

	dal_i2caux_construct(base, ctx);

	i2caux_dce80->base.funcs = &i2caux_funcs;
	i2caux_dce80->i2c_hw_buffer_in_use = false;

	/* Create I2C HW engines (HW + SW pairs)
	 * for all lines which has assisted HW DDC
	 * 'i' (loop counter) used as DDC/AUX engine_id */

	i = 0;

	do {
		enum gpio_ddc_line line_id = hw_ddc_lines[i];

		struct i2c_hw_engine_dce80_create_arg hw_arg;

		if (use_i2c_sw_engine) {
			struct i2c_sw_engine_dce80_create_arg sw_arg;

			sw_arg.engine_id = i;
			sw_arg.default_speed = base->default_i2c_sw_speed;
			sw_arg.ctx = ctx;
			base->i2c_sw_engines[line_id] =
				dal_i2c_sw_engine_dce80_create(&sw_arg);
		}

		hw_arg.engine_id = i;
		hw_arg.reference_frequency = reference_frequency;
		hw_arg.default_speed = base->default_i2c_hw_speed;
		hw_arg.ctx = ctx;

		base->i2c_hw_engines[line_id] =
			dal_i2c_hw_engine_dce80_create(&hw_arg);

		++i;
	} while (i < ARRAY_SIZE(hw_ddc_lines));

	/* Create AUX engines for all lines which has assisted HW AUX
	 * 'i' (loop counter) used as DDC/AUX engine_id */

	i = 0;

	do {
		enum gpio_ddc_line line_id = hw_aux_lines[i];

		struct aux_engine_dce110_init_data arg;

		arg.engine_id = i;
		arg.timeout_period = base->aux_timeout_period;
		arg.ctx = ctx;
		arg.regs = &dce80_aux_regs[i];

		base->aux_engines[line_id] =
			dal_aux_engine_dce110_create(&arg);

		++i;
	} while (i < ARRAY_SIZE(hw_aux_lines));

	/* TODO Generic I2C SW and HW */
}

struct i2caux *dal_i2caux_dce80_create(
	struct dc_context *ctx)
{
	struct i2caux_dce80 *i2caux_dce80 =
		kzalloc(sizeof(struct i2caux_dce80), GFP_KERNEL);

	if (!i2caux_dce80) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	construct(i2caux_dce80, ctx);
	return &i2caux_dce80->base;
}
