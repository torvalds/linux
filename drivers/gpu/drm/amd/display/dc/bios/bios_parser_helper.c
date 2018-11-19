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

#include "atom.h"

#include "include/bios_parser_types.h"
#include "bios_parser_helper.h"
#include "command_table_helper.h"
#include "command_table.h"
#include "bios_parser_types_internal.h"

uint8_t *bios_get_image(struct dc_bios *bp,
	uint32_t offset,
	uint32_t size)
{
	if (bp->bios && offset + size < bp->bios_size)
		return bp->bios + offset;
	else
		return NULL;
}

#include "reg_helper.h"

#define CTX \
	bios->ctx
#define REG(reg)\
	(bios->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
		ATOM_ ## field_name ## _SHIFT, ATOM_ ## field_name

bool bios_is_accelerated_mode(
	struct dc_bios *bios)
{
	uint32_t acc_mode;
	REG_GET(BIOS_SCRATCH_6, S6_ACC_MODE, &acc_mode);
	return (acc_mode == 1);
}


void bios_set_scratch_acc_mode_change(
	struct dc_bios *bios)
{
	REG_UPDATE(BIOS_SCRATCH_6, S6_ACC_MODE, 1);
}


void bios_set_scratch_critical_state(
	struct dc_bios *bios,
	bool state)
{
	uint32_t critial_state = state ? 1 : 0;
	REG_UPDATE(BIOS_SCRATCH_6, S6_CRITICAL_STATE, critial_state);
}

uint32_t bios_get_vga_enabled_displays(
	struct dc_bios *bios)
{
	uint32_t active_disp = 1;

	if (bios->regs->BIOS_SCRATCH_3) /*follow up with other asic, todo*/
		active_disp = REG_READ(BIOS_SCRATCH_3) & 0XFFFF;
	return active_disp;
}

bool bios_is_active_display(
		struct dc_bios *bios,
		enum signal_type signal,
		const struct connector_device_tag_info *device_tag)
{
	uint32_t active = 0;
	uint32_t connected = 0;
	uint32_t bios_scratch_0 = 0;
	uint32_t bios_scratch_3 = 0;

	switch (signal)	{
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		{
			if (device_tag->dev_id.device_type == DEVICE_TYPE_DFP) {
				switch (device_tag->dev_id.enum_id)	{
				case 1:
					{
						active    = ATOM_S3_DFP1_ACTIVE;
						connected = 0x0008;	//ATOM_DISPLAY_DFP1_CONNECT
					}
					break;

				case 2:
					{
						active    = ATOM_S3_DFP2_ACTIVE;
						connected = 0x0080; //ATOM_DISPLAY_DFP2_CONNECT
					}
					break;

				case 3:
					{
						active    = ATOM_S3_DFP3_ACTIVE;
						connected = 0x0200; //ATOM_DISPLAY_DFP3_CONNECT
					}
					break;

				case 4:
					{
						active    = ATOM_S3_DFP4_ACTIVE;
						connected = 0x0400;	//ATOM_DISPLAY_DFP4_CONNECT
					}
					break;

				case 5:
					{
						active    = ATOM_S3_DFP5_ACTIVE;
						connected = 0x0800; //ATOM_DISPLAY_DFP5_CONNECT
					}
					break;

				case 6:
					{
						active    = ATOM_S3_DFP6_ACTIVE;
						connected = 0x0040; //ATOM_DISPLAY_DFP6_CONNECT
					}
					break;

				default:
					break;
				}
				}
			}
			break;

	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		{
			active    = ATOM_S3_LCD1_ACTIVE;
			connected = 0x0002;	//ATOM_DISPLAY_LCD1_CONNECT
		}
		break;

	default:
		break;
	}


	if (bios->regs->BIOS_SCRATCH_0) /*follow up with other asic, todo*/
		bios_scratch_0 = REG_READ(BIOS_SCRATCH_0);
	if (bios->regs->BIOS_SCRATCH_3) /*follow up with other asic, todo*/
		bios_scratch_3 = REG_READ(BIOS_SCRATCH_3);

	bios_scratch_3 &= ATOM_S3_DEVICE_ACTIVE_MASK;
	if ((active & bios_scratch_3) && (connected & bios_scratch_0))
		return true;

	return false;
}

