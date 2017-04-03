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
#include "include/logger_interface.h"

#include "vega10/DC/dce_12_0_offset.h"
#include "vega10/DC/dce_12_0_sh_mask.h"
#include "vega10/soc15ip.h"

#include "../dce110/dce110_ipp.h"


#define DCP_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_soc15(ipp110->base.ctx, ipp110->offsets.dcp_offset, reg_name, n, __VA_ARGS__)

#define DCP_REG_SET_N(reg_name, n, ...)	\
		generic_reg_set_soc15(ipp110->base.ctx, ipp110->offsets.dcp_offset, reg_name, n, __VA_ARGS__)

#define DCP_REG_UPDATE(reg, field, val)	\
		DCP_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)

#define DCP_REG_UPDATE_2(reg, field1, val1, field2, val2)	\
		DCP_REG_UPDATE_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define DCP_REG_UPDATE_3(reg, field1, val1, field2, val2, field3, val3)	\
		DCP_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

#define DCP_REG_SET(reg, field, val)	\
		DCP_REG_SET_N(reg, 1, FD(reg##__##field), val)

#define DCP_REG_SET_2(reg, field1, val1, field2, val2)	\
		DCP_REG_SET_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define DCP_REG_SET_3(reg, field1, val1, field2, val2, field3, val3)	\
		DCP_REG_SET_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

/* TODO: DAL3 does not implement cursor memory control
 * MCIF_MEM_CONTROL, DMIF_CURSOR_MEM_CONTROL
 */
static void lock(
	struct dce110_ipp *ipp110, bool lock)
{
	DCP_REG_UPDATE(DCP0_CUR_UPDATE, CURSOR_UPDATE_LOCK, lock);
}

static bool program_control(
	struct dce110_ipp *ipp110,
	enum dc_cursor_color_format color_format,
	bool enable_magnification,
	bool inverse_transparent_clamping)
{
	uint32_t mode = 0;

	switch (color_format) {
	case CURSOR_MODE_MONO:
		mode = 0;
		break;
	case CURSOR_MODE_COLOR_1BIT_AND:
		mode = 1;
		break;
	case CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA:
		mode = 2;
		break;
	case CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA:
		mode = 3;
		break;
	default:
		return false;
	}

	DCP_REG_UPDATE_3(
		DCP0_CUR_CONTROL,
		CURSOR_MODE, mode,
		CURSOR_2X_MAGNIFY, enable_magnification,
		CUR_INV_TRANS_CLAMP, inverse_transparent_clamping);

	if (color_format == CURSOR_MODE_MONO) {
		DCP_REG_SET_3(
			DCP0_CUR_COLOR1,
			CUR_COLOR1_BLUE, 0,
			CUR_COLOR1_GREEN, 0,
			CUR_COLOR1_RED, 0);

		DCP_REG_SET_3(
			DCP0_CUR_COLOR2,
			CUR_COLOR2_BLUE, 0xff,
			CUR_COLOR2_GREEN, 0xff,
			CUR_COLOR2_RED, 0xff);
	}
	return true;
}

static void program_address(
	struct dce110_ipp *ipp110,
	PHYSICAL_ADDRESS_LOC address)
{
	/* SURFACE_ADDRESS_HIGH: Higher order bits (39:32) of hardware cursor
	 * surface base address in byte. It is 4K byte aligned.
	 * The correct way to program cursor surface address is to first write
	 * to CUR_SURFACE_ADDRESS_HIGH, and then write to CUR_SURFACE_ADDRESS
	 */

	DCP_REG_SET(
		DCP0_CUR_SURFACE_ADDRESS_HIGH,
		CURSOR_SURFACE_ADDRESS_HIGH, address.high_part);

	DCP_REG_SET(
		DCP0_CUR_SURFACE_ADDRESS,
		CURSOR_SURFACE_ADDRESS, address.low_part);
}

void dce120_ipp_cursor_set_position(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_position *position,
	const struct dc_cursor_mi_param *param)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	/* lock cursor registers */
	lock(ipp110, true);

	/* Flag passed in structure differentiates cursor enable/disable. */
	/* Update if it differs from cached state. */
	DCP_REG_UPDATE(DCP0_CUR_CONTROL, CURSOR_EN, position->enable);

	DCP_REG_SET_2(
		DCP0_CUR_POSITION,
		CURSOR_X_POSITION, position->x,
		CURSOR_Y_POSITION, position->y);

	DCP_REG_SET_2(
		DCP0_CUR_HOT_SPOT,
		CURSOR_HOT_SPOT_X, position->x_hotspot,
		CURSOR_HOT_SPOT_Y, position->y_hotspot);

	/* unlock cursor registers */
	lock(ipp110, false);
}

bool dce120_ipp_cursor_set_attributes(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_attributes *attributes)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);
	/* Lock cursor registers */
	lock(ipp110, true);

	/* Program cursor control */
	program_control(
		ipp110,
		attributes->color_format,
		attributes->attribute_flags.bits.ENABLE_MAGNIFICATION,
		attributes->attribute_flags.bits.INVERSE_TRANSPARENT_CLAMPING);

	/*
	 * Program cursor size -- NOTE: HW spec specifies that HW register
	 * stores size as (height - 1, width - 1)
	 */
	DCP_REG_SET_2(
		DCP0_CUR_SIZE,
		CURSOR_WIDTH, attributes->width-1,
		CURSOR_HEIGHT, attributes->height-1);

	/* Program cursor surface address */
	program_address(ipp110, attributes->address);

	/* Unlock Cursor registers. */
	lock(ipp110, false);

	return true;
}

