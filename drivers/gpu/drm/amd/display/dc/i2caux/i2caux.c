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
#include "dc_bios_types.h"

/*
 * Header of this unit
 */

#include "i2caux.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "engine.h"
#include "i2c_engine.h"
#include "aux_engine.h"

/*
 * This unit
 */

#include "dce80/i2caux_dce80.h"

#include "dce100/i2caux_dce100.h"

#include "dce110/i2caux_dce110.h"

#include "dce112/i2caux_dce112.h"

#include "dce120/i2caux_dce120.h"

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "dcn10/i2caux_dcn10.h"
#endif

#include "diagnostics/i2caux_diag.h"

/*
 * @brief
 * Plain API, available publicly
 */

struct i2caux *dal_i2caux_create(
	struct dc_context *ctx)
{
	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment)) {
		return dal_i2caux_diag_fpga_create(ctx);
	}

	switch (ctx->dce_version) {
	case DCE_VERSION_8_0:
	case DCE_VERSION_8_1:
	case DCE_VERSION_8_3:
		return dal_i2caux_dce80_create(ctx);
	case DCE_VERSION_11_2:
	case DCE_VERSION_11_22:
		return dal_i2caux_dce112_create(ctx);
	case DCE_VERSION_11_0:
		return dal_i2caux_dce110_create(ctx);
	case DCE_VERSION_10_0:
		return dal_i2caux_dce100_create(ctx);
	case DCE_VERSION_12_0:
		return dal_i2caux_dce120_create(ctx);
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case DCN_VERSION_1_0:
		return dal_i2caux_dcn10_create(ctx);
#endif

#if defined(CONFIG_DRM_AMD_DC_DCN1_01)
	case DCN_VERSION_1_01:
		return dal_i2caux_dcn10_create(ctx);
#endif
	default:
		BREAK_TO_DEBUGGER();
		return NULL;
	}
}

bool dal_i2caux_submit_i2c_command(
	struct i2caux *i2caux,
	struct ddc *ddc,
	struct i2c_command *cmd)
{
	struct i2c_engine *engine;
	uint8_t index_of_payload = 0;
	bool result;

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!cmd) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	/*
	 * default will be SW, however there is a feature flag in adapter
	 * service that determines whether SW i2c_engine will be available or
	 * not, if sw i2c is not available we will fallback to hw. This feature
	 * flag is set to not creating sw i2c engine for every dce except dce80
	 * currently
	 */
	switch (cmd->engine) {
	case I2C_COMMAND_ENGINE_DEFAULT:
	case I2C_COMMAND_ENGINE_SW:
		/* try to acquire SW engine first,
		 * acquire HW engine if SW engine not available */
		engine = i2caux->funcs->acquire_i2c_sw_engine(i2caux, ddc);

		if (!engine)
			engine = i2caux->funcs->acquire_i2c_hw_engine(
				i2caux, ddc);
	break;
	case I2C_COMMAND_ENGINE_HW:
	default:
		/* try to acquire HW engine first,
		 * acquire SW engine if HW engine not available */
		engine = i2caux->funcs->acquire_i2c_hw_engine(i2caux, ddc);

		if (!engine)
			engine = i2caux->funcs->acquire_i2c_sw_engine(
				i2caux, ddc);
	}

	if (!engine)
		return false;

	engine->funcs->set_speed(engine, cmd->speed);

	result = true;

	while (index_of_payload < cmd->number_of_payloads) {
		bool mot = (index_of_payload != cmd->number_of_payloads - 1);

		struct i2c_payload *payload = cmd->payloads + index_of_payload;

		struct i2caux_transaction_request request = { 0 };

		request.operation = payload->write ?
			I2CAUX_TRANSACTION_WRITE :
			I2CAUX_TRANSACTION_READ;

		request.payload.address_space =
			I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C;
		request.payload.address = (payload->address << 1) |
			!payload->write;
		request.payload.length = payload->length;
		request.payload.data = payload->data;

		if (!engine->base.funcs->submit_request(
			&engine->base, &request, mot)) {
			result = false;
			break;
		}

		++index_of_payload;
	}

	i2caux->funcs->release_engine(i2caux, &engine->base);

	return result;
}

bool dal_i2caux_submit_aux_command(
	struct i2caux *i2caux,
	struct ddc *ddc,
	struct aux_command *cmd)
{
	struct aux_engine *engine;
	uint8_t index_of_payload = 0;
	bool result;
	bool mot;

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!cmd) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	engine = i2caux->funcs->acquire_aux_engine(i2caux, ddc);

	if (!engine)
		return false;

	engine->delay = cmd->defer_delay;
	engine->max_defer_write_retry = cmd->max_defer_write_retry;

	result = true;

	while (index_of_payload < cmd->number_of_payloads) {
		struct aux_payload *payload = cmd->payloads + index_of_payload;
		struct i2caux_transaction_request request = { 0 };

		if (cmd->mot == I2C_MOT_UNDEF)
			mot = (index_of_payload != cmd->number_of_payloads - 1);
		else
			mot = (cmd->mot == I2C_MOT_TRUE);

		request.operation = payload->write ?
			I2CAUX_TRANSACTION_WRITE :
			I2CAUX_TRANSACTION_READ;

		if (payload->i2c_over_aux) {
			request.payload.address_space =
				I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C;

			request.payload.address = (payload->address << 1) |
				!payload->write;
		} else {
			request.payload.address_space =
				I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD;

			request.payload.address = payload->address;
		}

		request.payload.length = payload->length;
		request.payload.data = payload->data;

		if (!engine->base.funcs->submit_request(
			&engine->base, &request, mot)) {
			result = false;
			break;
		}

		++index_of_payload;
	}

	i2caux->funcs->release_engine(i2caux, &engine->base);

	return result;
}

static bool get_hw_supported_ddc_line(
	struct ddc *ddc,
	enum gpio_ddc_line *line)
{
	enum gpio_ddc_line line_found;

	*line = GPIO_DDC_LINE_UNKNOWN;

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!ddc->hw_info.hw_supported)
		return false;

	line_found = dal_ddc_get_line(ddc);

	if (line_found >= GPIO_DDC_LINE_COUNT)
		return false;

	*line = line_found;

	return true;
}

void dal_i2caux_configure_aux(
	struct i2caux *i2caux,
	struct ddc *ddc,
	union aux_config cfg)
{
	struct aux_engine *engine =
		i2caux->funcs->acquire_aux_engine(i2caux, ddc);

	if (!engine)
		return;

	engine->funcs->configure(engine, cfg);

	i2caux->funcs->release_engine(i2caux, &engine->base);
}

void dal_i2caux_destroy(
	struct i2caux **i2caux)
{
	if (!i2caux || !*i2caux) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*i2caux)->funcs->destroy(i2caux);

	*i2caux = NULL;
}

/*
 * @brief
 * An utility function used by 'struct i2caux' and its descendants
 */

uint32_t dal_i2caux_get_reference_clock(
		struct dc_bios *bios)
{
	struct dc_firmware_info info = { { 0 } };

	if (bios->funcs->get_firmware_info(bios, &info) != BP_RESULT_OK)
		return 0;

	return info.pll_info.crystal_frequency;
}

/*
 * @brief
 * i2caux
 */

enum {
	/* following are expressed in KHz */
	DEFAULT_I2C_SW_SPEED = 50,
	DEFAULT_I2C_HW_SPEED = 50,

	DEFAULT_I2C_SW_SPEED_100KHZ = 100,
	DEFAULT_I2C_HW_SPEED_100KHZ = 100,

	/* This is the timeout as defined in DP 1.2a,
	 * 2.3.4 "Detailed uPacket TX AUX CH State Description". */
	AUX_TIMEOUT_PERIOD = 400,

	/* Ideally, the SW timeout should be just above 550usec
	 * which is programmed in HW.
	 * But the SW timeout of 600usec is not reliable,
	 * because on some systems, delay_in_microseconds()
	 * returns faster than it should.
	 * EPR #379763: by trial-and-error on different systems,
	 * 700usec is the minimum reliable SW timeout for polling
	 * the AUX_SW_STATUS.AUX_SW_DONE bit.
	 * This timeout expires *only* when there is
	 * AUX Error or AUX Timeout conditions - not during normal operation.
	 * During normal operation, AUX_SW_STATUS.AUX_SW_DONE bit is set
	 * at most within ~240usec. That means,
	 * increasing this timeout will not affect normal operation,
	 * and we'll timeout after
	 * SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD = 1600usec.
	 * This timeout is especially important for
	 * resume from S3 and CTS. */
	SW_AUX_TIMEOUT_PERIOD_MULTIPLIER = 4
};

struct i2c_engine *dal_i2caux_acquire_i2c_sw_engine(
	struct i2caux *i2caux,
	struct ddc *ddc)
{
	enum gpio_ddc_line line;
	struct i2c_engine *engine = NULL;

	if (get_hw_supported_ddc_line(ddc, &line))
		engine = i2caux->i2c_sw_engines[line];

	if (!engine)
		engine = i2caux->i2c_generic_sw_engine;

	if (!engine)
		return NULL;

	if (!engine->base.funcs->acquire(&engine->base, ddc))
		return NULL;

	return engine;
}

struct aux_engine *dal_i2caux_acquire_aux_engine(
	struct i2caux *i2caux,
	struct ddc *ddc)
{
	enum gpio_ddc_line line;
	struct aux_engine *engine;

	if (!get_hw_supported_ddc_line(ddc, &line))
		return NULL;

	engine = i2caux->aux_engines[line];

	if (!engine)
		return NULL;

	if (!engine->base.funcs->acquire(&engine->base, ddc))
		return NULL;

	return engine;
}

void dal_i2caux_release_engine(
	struct i2caux *i2caux,
	struct engine *engine)
{
	engine->funcs->release_engine(engine);

	dal_ddc_close(engine->ddc);

	engine->ddc = NULL;
}

void dal_i2caux_construct(
	struct i2caux *i2caux,
	struct dc_context *ctx)
{
	uint32_t i = 0;

	i2caux->ctx = ctx;
	do {
		i2caux->i2c_sw_engines[i] = NULL;
		i2caux->i2c_hw_engines[i] = NULL;
		i2caux->aux_engines[i] = NULL;

		++i;
	} while (i < GPIO_DDC_LINE_COUNT);

	i2caux->i2c_generic_sw_engine = NULL;
	i2caux->i2c_generic_hw_engine = NULL;

	i2caux->aux_timeout_period =
		SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD;

	if (ctx->dce_version >= DCE_VERSION_11_2) {
		i2caux->default_i2c_hw_speed = DEFAULT_I2C_HW_SPEED_100KHZ;
		i2caux->default_i2c_sw_speed = DEFAULT_I2C_SW_SPEED_100KHZ;
	} else {
		i2caux->default_i2c_hw_speed = DEFAULT_I2C_HW_SPEED;
		i2caux->default_i2c_sw_speed = DEFAULT_I2C_SW_SPEED;
	}
}

void dal_i2caux_destruct(
	struct i2caux *i2caux)
{
	uint32_t i = 0;

	if (i2caux->i2c_generic_hw_engine)
		i2caux->i2c_generic_hw_engine->funcs->destroy(
			&i2caux->i2c_generic_hw_engine);

	if (i2caux->i2c_generic_sw_engine)
		i2caux->i2c_generic_sw_engine->funcs->destroy(
			&i2caux->i2c_generic_sw_engine);

	do {
		if (i2caux->aux_engines[i])
			i2caux->aux_engines[i]->funcs->destroy(
				&i2caux->aux_engines[i]);

		if (i2caux->i2c_hw_engines[i])
			i2caux->i2c_hw_engines[i]->funcs->destroy(
				&i2caux->i2c_hw_engines[i]);

		if (i2caux->i2c_sw_engines[i])
			i2caux->i2c_sw_engines[i]->funcs->destroy(
				&i2caux->i2c_sw_engines[i]);

		++i;
	} while (i < GPIO_DDC_LINE_COUNT);
}

