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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "dce110_ipp.h"

#define CURSOR_COLOR_BLACK 0x00000000
#define CURSOR_COLOR_WHITE 0xFFFFFFFF

#define DCP_REG(reg)\
	(reg + ipp110->offsets.dcp_offset)

static void enable(
	struct dce110_ipp *ipp110,
	bool enable);

static void lock(
	struct dce110_ipp *ipp110,
	bool enable);

static void program_position(
	struct dce110_ipp *ipp110,
	uint32_t x,
	uint32_t y);

static bool program_control(
	struct dce110_ipp *ipp110,
	enum dc_cursor_color_format color_format,
	bool enable_magnification,
	bool inverse_transparent_clamping);

static void program_hotspot(
	struct dce110_ipp *ipp110,
	uint32_t x,
	uint32_t y);

static void program_size(
	struct dce110_ipp *ipp110,
	uint32_t width,
	uint32_t height);

static void program_address(
	struct dce110_ipp *ipp110,
	PHYSICAL_ADDRESS_LOC address);

void dce110_ipp_cursor_set_position(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_position *position)
{
	struct dce110_ipp *ipp110 = TO_DCE110_IPP(ipp);

	/* lock cursor registers */
	lock(ipp110, true);

	/* Flag passed in structure differentiates cursor enable/disable. */
	/* Update if it differs from cached state. */
	enable(ipp110, position->enable);

	program_position(ipp110, position->x, position->y);

	if (position->hot_spot_enable)
		program_hotspot(
				ipp110,
				position->x_hotspot,
				position->y_hotspot);

	/* unlock cursor registers */
	lock(ipp110, false);
}

bool dce110_ipp_cursor_set_attributes(
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

	/* Program hot spot coordinates */
	program_hotspot(ipp110, attributes->x_hot, attributes->y_hot);

	/*
	 * Program cursor size -- NOTE: HW spec specifies that HW register
	 * stores size as (height - 1, width - 1)
	 */
	program_size(ipp110, attributes->width-1, attributes->height-1);

	/* Program cursor surface address */
	program_address(ipp110, attributes->address);

	/* Unlock Cursor registers. */
	lock(ipp110, false);

	return true;
}

static void enable(
	struct dce110_ipp *ipp110, bool enable)
{
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmCUR_CONTROL);

	value = dm_read_reg(ipp110->base.ctx, addr);
	set_reg_field_value(value, enable, CUR_CONTROL, CURSOR_EN);
	dm_write_reg(ipp110->base.ctx, addr, value);
}

static void lock(
	struct dce110_ipp *ipp110, bool lock)
{
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmCUR_UPDATE);

	value = dm_read_reg(ipp110->base.ctx, addr);
	set_reg_field_value(value, lock, CUR_UPDATE, CURSOR_UPDATE_LOCK);
	dm_write_reg(ipp110->base.ctx, addr, value);
}

static void program_position(
	struct dce110_ipp *ipp110,
	uint32_t x,
	uint32_t y)
{
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmCUR_POSITION);

	value = dm_read_reg(ipp110->base.ctx, addr);
	set_reg_field_value(value, x, CUR_POSITION, CURSOR_X_POSITION);
	set_reg_field_value(value, y, CUR_POSITION, CURSOR_Y_POSITION);
	dm_write_reg(ipp110->base.ctx, addr, value);
}

static bool program_control(
	struct dce110_ipp *ipp110,
	enum dc_cursor_color_format color_format,
	bool enable_magnification,
	bool inverse_transparent_clamping)
{
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmCUR_CONTROL);
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

	set_reg_field_value(value, mode, CUR_CONTROL, CURSOR_MODE);
	set_reg_field_value(value, enable_magnification,
			CUR_CONTROL, CURSOR_2X_MAGNIFY);
	set_reg_field_value(value, inverse_transparent_clamping,
			CUR_CONTROL, CUR_INV_TRANS_CLAMP);
	dm_write_reg(ipp110->base.ctx, addr, value);

	if (color_format == CURSOR_MODE_MONO) {
		addr = DCP_REG(mmCUR_COLOR1);
		dm_write_reg(ipp110->base.ctx, addr, CURSOR_COLOR_BLACK);
		addr = DCP_REG(mmCUR_COLOR2);
		dm_write_reg(ipp110->base.ctx, addr, CURSOR_COLOR_WHITE);
	}
	return true;
}

static void program_hotspot(
	struct dce110_ipp *ipp110,
	uint32_t x,
	uint32_t y)
{
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmCUR_HOT_SPOT);

	value = dm_read_reg(ipp110->base.ctx, addr);
	set_reg_field_value(value, x, CUR_HOT_SPOT, CURSOR_HOT_SPOT_X);
	set_reg_field_value(value, y, CUR_HOT_SPOT, CURSOR_HOT_SPOT_Y);
	dm_write_reg(ipp110->base.ctx, addr, value);
}

static void program_size(
	struct dce110_ipp *ipp110,
	uint32_t width,
	uint32_t height)
{
	uint32_t value = 0;
	uint32_t addr = DCP_REG(mmCUR_SIZE);

	value = dm_read_reg(ipp110->base.ctx, addr);
	set_reg_field_value(value, width, CUR_SIZE, CURSOR_WIDTH);
	set_reg_field_value(value, height, CUR_SIZE, CURSOR_HEIGHT);
	dm_write_reg(ipp110->base.ctx, addr, value);
}

static void program_address(
	struct dce110_ipp *ipp110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t addr = DCP_REG(mmCUR_SURFACE_ADDRESS_HIGH);
	/* SURFACE_ADDRESS_HIGH: Higher order bits (39:32) of hardware cursor
	 * surface base address in byte. It is 4K byte aligned.
	 * The correct way to program cursor surface address is to first write
	 * to CUR_SURFACE_ADDRESS_HIGH, and then write to CUR_SURFACE_ADDRESS */

	dm_write_reg(ipp110->base.ctx, addr, address.high_part);

	addr = DCP_REG(mmCUR_SURFACE_ADDRESS);
	dm_write_reg(ipp110->base.ctx, addr, address.low_part);
}

