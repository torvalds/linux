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

#ifndef __DAL_AUX_ENGINE_DCE110_H__
#define __DAL_AUX_ENGINE_DCE110_H__

#include "i2caux_interface.h"
#include "inc/hw/aux_engine.h"

#define AUX_COMMON_REG_LIST(id)\
	SRI(AUX_CONTROL, DP_AUX, id), \
	SRI(AUX_ARB_CONTROL, DP_AUX, id), \
	SRI(AUX_SW_DATA, DP_AUX, id), \
	SRI(AUX_SW_CONTROL, DP_AUX, id), \
	SRI(AUX_INTERRUPT_CONTROL, DP_AUX, id), \
	SRI(AUX_SW_STATUS, DP_AUX, id), \
	SR(AUXN_IMPCAL), \
	SR(AUXP_IMPCAL)

struct dce110_aux_registers {
	uint32_t AUX_CONTROL;
	uint32_t AUX_ARB_CONTROL;
	uint32_t AUX_SW_DATA;
	uint32_t AUX_SW_CONTROL;
	uint32_t AUX_INTERRUPT_CONTROL;
	uint32_t AUX_SW_STATUS;
	uint32_t AUXN_IMPCAL;
	uint32_t AUXP_IMPCAL;

	uint32_t AUX_RESET_MASK;
};

enum {	/* This is the timeout as defined in DP 1.2a,
	 * 2.3.4 "Detailed uPacket TX AUX CH State Description".
	 */
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
	 * SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD = 2400usec.
	 * This timeout is especially important for
	 * converters, resume from S3, and CTS.
	 */
	SW_AUX_TIMEOUT_PERIOD_MULTIPLIER = 6
};

struct dce_aux {
	uint32_t inst;
	struct ddc *ddc;
	struct dc_context *ctx;
	/* following values are expressed in milliseconds */
	uint32_t delay;
	uint32_t max_defer_write_retry;

	bool acquire_reset;
};

struct aux_engine_dce110 {
	struct dce_aux base;
	const struct dce110_aux_registers *regs;
	struct {
		uint32_t aux_control;
		uint32_t aux_arb_control;
		uint32_t aux_sw_data;
		uint32_t aux_sw_control;
		uint32_t aux_interrupt_control;
		uint32_t aux_sw_status;
	} addr;
	uint32_t timeout_period;
};

struct aux_engine_dce110_init_data {
	uint32_t engine_id;
	uint32_t timeout_period;
	struct dc_context *ctx;
	const struct dce110_aux_registers *regs;
};

struct dce_aux *dce110_aux_engine_construct(
		struct aux_engine_dce110 *aux_engine110,
		struct dc_context *ctx,
		uint32_t inst,
		uint32_t timeout_period,
		const struct dce110_aux_registers *regs);

void dce110_engine_destroy(struct dce_aux **engine);

bool dce110_aux_engine_acquire(
	struct dce_aux *aux_engine,
	struct ddc *ddc);

int dce_aux_transfer_raw(struct ddc_service *ddc,
		struct aux_payload *cmd,
		enum aux_channel_operation_result *operation_result);

bool dce_aux_transfer_with_retries(struct ddc_service *ddc,
		struct aux_payload *cmd);
#endif
