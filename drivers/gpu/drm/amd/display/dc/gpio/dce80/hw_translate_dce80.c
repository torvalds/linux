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
#include "include/gpio_types.h"
#include "../hw_translate.h"

#include "hw_translate_dce80.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"
#include "smu/smu_7_0_1_d.h"

/*
 * @brief
 * Returns index of first bit (starting with LSB) which is set
 */
static uint32_t index_from_vector(
	uint32_t vector)
{
	uint32_t result = 0;
	uint32_t mask = 1;

	do {
		if (vector == mask)
			return result;

		++result;
		mask <<= 1;
	} while (mask);

	BREAK_TO_DEBUGGER();

	return GPIO_ENUM_UNKNOWN;
}

static bool offset_to_id(
	uint32_t offset,
	uint32_t mask,
	enum gpio_id *id,
	uint32_t *en)
{
	switch (offset) {
	/* GENERIC */
	case mmDC_GPIO_GENERIC_A:
		*id = GPIO_ID_GENERIC;
		switch (mask) {
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICA_A_MASK:
			*en = GPIO_GENERIC_A;
			return true;
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICB_A_MASK:
			*en = GPIO_GENERIC_B;
			return true;
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICC_A_MASK:
			*en = GPIO_GENERIC_C;
			return true;
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICD_A_MASK:
			*en = GPIO_GENERIC_D;
			return true;
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICE_A_MASK:
			*en = GPIO_GENERIC_E;
			return true;
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICF_A_MASK:
			*en = GPIO_GENERIC_F;
			return true;
		case DC_GPIO_GENERIC_A__DC_GPIO_GENERICG_A_MASK:
			*en = GPIO_GENERIC_G;
			return true;
		default:
			BREAK_TO_DEBUGGER();
			return false;
		}
	break;
	/* HPD */
	case mmDC_GPIO_HPD_A:
		*id = GPIO_ID_HPD;
		switch (mask) {
		case DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK:
			*en = GPIO_HPD_1;
			return true;
		case DC_GPIO_HPD_A__DC_GPIO_HPD2_A_MASK:
			*en = GPIO_HPD_2;
			return true;
		case DC_GPIO_HPD_A__DC_GPIO_HPD3_A_MASK:
			*en = GPIO_HPD_3;
			return true;
		case DC_GPIO_HPD_A__DC_GPIO_HPD4_A_MASK:
			*en = GPIO_HPD_4;
			return true;
		case DC_GPIO_HPD_A__DC_GPIO_HPD5_A_MASK:
			*en = GPIO_HPD_5;
			return true;
		case DC_GPIO_HPD_A__DC_GPIO_HPD6_A_MASK:
			*en = GPIO_HPD_6;
			return true;
		default:
			BREAK_TO_DEBUGGER();
			return false;
		}
	break;
	/* SYNCA */
	case mmDC_GPIO_SYNCA_A:
		*id = GPIO_ID_SYNC;
		switch (mask) {
		case DC_GPIO_SYNCA_A__DC_GPIO_HSYNCA_A_MASK:
			*en = GPIO_SYNC_HSYNC_A;
			return true;
		case DC_GPIO_SYNCA_A__DC_GPIO_VSYNCA_A_MASK:
			*en = GPIO_SYNC_VSYNC_A;
			return true;
		default:
			BREAK_TO_DEBUGGER();
			return false;
		}
	break;
	/* mmDC_GPIO_GENLK_MASK */
	case mmDC_GPIO_GENLK_A:
		*id = GPIO_ID_GSL;
		switch (mask) {
		case DC_GPIO_GENLK_A__DC_GPIO_GENLK_CLK_A_MASK:
			*en = GPIO_GSL_GENLOCK_CLOCK;
			return true;
		case DC_GPIO_GENLK_A__DC_GPIO_GENLK_VSYNC_A_MASK:
			*en = GPIO_GSL_GENLOCK_VSYNC;
			return true;
		case DC_GPIO_GENLK_A__DC_GPIO_SWAPLOCK_A_A_MASK:
			*en = GPIO_GSL_SWAPLOCK_A;
			return true;
		case DC_GPIO_GENLK_A__DC_GPIO_SWAPLOCK_B_A_MASK:
			*en = GPIO_GSL_SWAPLOCK_B;
			return true;
		default:
			BREAK_TO_DEBUGGER();
			return false;
		}
	break;
	/* GPIOPAD */
	case mmGPIOPAD_A:
		*id = GPIO_ID_GPIO_PAD;
		*en = index_from_vector(mask);
		return (*en <= GPIO_GPIO_PAD_MAX);
	/* DDC */
	/* we don't care about the GPIO_ID for DDC
	 * in DdcHandle it will use GPIO_ID_DDC_DATA/GPIO_ID_DDC_CLOCK
	 * directly in the create method */
	case mmDC_GPIO_DDC1_A:
		*en = GPIO_DDC_LINE_DDC1;
		return true;
	case mmDC_GPIO_DDC2_A:
		*en = GPIO_DDC_LINE_DDC2;
		return true;
	case mmDC_GPIO_DDC3_A:
		*en = GPIO_DDC_LINE_DDC3;
		return true;
	case mmDC_GPIO_DDC4_A:
		*en = GPIO_DDC_LINE_DDC4;
		return true;
	case mmDC_GPIO_DDC5_A:
		*en = GPIO_DDC_LINE_DDC5;
		return true;
	case mmDC_GPIO_DDC6_A:
		*en = GPIO_DDC_LINE_DDC6;
		return true;
	case mmDC_GPIO_DDCVGA_A:
		*en = GPIO_DDC_LINE_DDC_VGA;
		return true;
	/* GPIO_I2CPAD */
	case mmDC_GPIO_I2CPAD_A:
		*en = GPIO_DDC_LINE_I2C_PAD;
		return true;
	/* Not implemented */
	case mmDC_GPIO_PWRSEQ_A:
	case mmDC_GPIO_PAD_STRENGTH_1:
	case mmDC_GPIO_PAD_STRENGTH_2:
	case mmDC_GPIO_DEBUG:
		return false;
	/* UNEXPECTED */
	default:
		BREAK_TO_DEBUGGER();
		return false;
	}
}

static bool id_to_offset(
	enum gpio_id id,
	uint32_t en,
	struct gpio_pin_info *info)
{
	bool result = true;

	switch (id) {
	case GPIO_ID_DDC_DATA:
		info->mask = DC_GPIO_DDC6_A__DC_GPIO_DDC6DATA_A_MASK;
		switch (en) {
		case GPIO_DDC_LINE_DDC1:
			info->offset = mmDC_GPIO_DDC1_A;
		break;
		case GPIO_DDC_LINE_DDC2:
			info->offset = mmDC_GPIO_DDC2_A;
		break;
		case GPIO_DDC_LINE_DDC3:
			info->offset = mmDC_GPIO_DDC3_A;
		break;
		case GPIO_DDC_LINE_DDC4:
			info->offset = mmDC_GPIO_DDC4_A;
		break;
		case GPIO_DDC_LINE_DDC5:
			info->offset = mmDC_GPIO_DDC5_A;
		break;
		case GPIO_DDC_LINE_DDC6:
			info->offset = mmDC_GPIO_DDC6_A;
		break;
		case GPIO_DDC_LINE_DDC_VGA:
			info->offset = mmDC_GPIO_DDCVGA_A;
		break;
		case GPIO_DDC_LINE_I2C_PAD:
			info->offset = mmDC_GPIO_I2CPAD_A;
		break;
		default:
			BREAK_TO_DEBUGGER();
			result = false;
		}
	break;
	case GPIO_ID_DDC_CLOCK:
		info->mask = DC_GPIO_DDC6_A__DC_GPIO_DDC6CLK_A_MASK;
		switch (en) {
		case GPIO_DDC_LINE_DDC1:
			info->offset = mmDC_GPIO_DDC1_A;
		break;
		case GPIO_DDC_LINE_DDC2:
			info->offset = mmDC_GPIO_DDC2_A;
		break;
		case GPIO_DDC_LINE_DDC3:
			info->offset = mmDC_GPIO_DDC3_A;
		break;
		case GPIO_DDC_LINE_DDC4:
			info->offset = mmDC_GPIO_DDC4_A;
		break;
		case GPIO_DDC_LINE_DDC5:
			info->offset = mmDC_GPIO_DDC5_A;
		break;
		case GPIO_DDC_LINE_DDC6:
			info->offset = mmDC_GPIO_DDC6_A;
		break;
		case GPIO_DDC_LINE_DDC_VGA:
			info->offset = mmDC_GPIO_DDCVGA_A;
		break;
		case GPIO_DDC_LINE_I2C_PAD:
			info->offset = mmDC_GPIO_I2CPAD_A;
		break;
		default:
			BREAK_TO_DEBUGGER();
			result = false;
		}
	break;
	case GPIO_ID_GENERIC:
		info->offset = mmDC_GPIO_GENERIC_A;
		switch (en) {
		case GPIO_GENERIC_A:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICA_A_MASK;
		break;
		case GPIO_GENERIC_B:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICB_A_MASK;
		break;
		case GPIO_GENERIC_C:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICC_A_MASK;
		break;
		case GPIO_GENERIC_D:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICD_A_MASK;
		break;
		case GPIO_GENERIC_E:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICE_A_MASK;
		break;
		case GPIO_GENERIC_F:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICF_A_MASK;
		break;
		case GPIO_GENERIC_G:
			info->mask = DC_GPIO_GENERIC_A__DC_GPIO_GENERICG_A_MASK;
		break;
		default:
			BREAK_TO_DEBUGGER();
			result = false;
		}
	break;
	case GPIO_ID_HPD:
		info->offset = mmDC_GPIO_HPD_A;
		switch (en) {
		case GPIO_HPD_1:
			info->mask = DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK;
		break;
		case GPIO_HPD_2:
			info->mask = DC_GPIO_HPD_A__DC_GPIO_HPD2_A_MASK;
		break;
		case GPIO_HPD_3:
			info->mask = DC_GPIO_HPD_A__DC_GPIO_HPD3_A_MASK;
		break;
		case GPIO_HPD_4:
			info->mask = DC_GPIO_HPD_A__DC_GPIO_HPD4_A_MASK;
		break;
		case GPIO_HPD_5:
			info->mask = DC_GPIO_HPD_A__DC_GPIO_HPD5_A_MASK;
		break;
		case GPIO_HPD_6:
			info->mask = DC_GPIO_HPD_A__DC_GPIO_HPD6_A_MASK;
		break;
		default:
			BREAK_TO_DEBUGGER();
			result = false;
		}
	break;
	case GPIO_ID_SYNC:
		switch (en) {
		case GPIO_SYNC_HSYNC_A:
			info->offset = mmDC_GPIO_SYNCA_A;
			info->mask = DC_GPIO_SYNCA_A__DC_GPIO_HSYNCA_A_MASK;
		break;
		case GPIO_SYNC_VSYNC_A:
			info->offset = mmDC_GPIO_SYNCA_A;
			info->mask = DC_GPIO_SYNCA_A__DC_GPIO_VSYNCA_A_MASK;
		break;
		case GPIO_SYNC_HSYNC_B:
		case GPIO_SYNC_VSYNC_B:
		default:
			BREAK_TO_DEBUGGER();
			result = false;
		}
	break;
	case GPIO_ID_GSL:
		switch (en) {
		case GPIO_GSL_GENLOCK_CLOCK:
			info->offset = mmDC_GPIO_GENLK_A;
			info->mask = DC_GPIO_GENLK_A__DC_GPIO_GENLK_CLK_A_MASK;
		break;
		case GPIO_GSL_GENLOCK_VSYNC:
			info->offset = mmDC_GPIO_GENLK_A;
			info->mask =
				DC_GPIO_GENLK_A__DC_GPIO_GENLK_VSYNC_A_MASK;
		break;
		case GPIO_GSL_SWAPLOCK_A:
			info->offset = mmDC_GPIO_GENLK_A;
			info->mask = DC_GPIO_GENLK_A__DC_GPIO_SWAPLOCK_A_A_MASK;
		break;
		case GPIO_GSL_SWAPLOCK_B:
			info->offset = mmDC_GPIO_GENLK_A;
			info->mask = DC_GPIO_GENLK_A__DC_GPIO_SWAPLOCK_B_A_MASK;
		break;
		default:
			BREAK_TO_DEBUGGER();
			result = false;
		}
	break;
	case GPIO_ID_GPIO_PAD:
		info->offset = mmGPIOPAD_A;
		info->mask = (1 << en);
		result = (info->mask <= GPIO_GPIO_PAD_MAX);
	break;
	case GPIO_ID_VIP_PAD:
	default:
		BREAK_TO_DEBUGGER();
		result = false;
	}

	if (result) {
		info->offset_y = info->offset + 2;
		info->offset_en = info->offset + 1;
		info->offset_mask = info->offset - 1;

		info->mask_y = info->mask;
		info->mask_en = info->mask;
		info->mask_mask = info->mask;
	}

	return result;
}

static const struct hw_translate_funcs funcs = {
		.offset_to_id = offset_to_id,
		.id_to_offset = id_to_offset,
};

void dal_hw_translate_dce80_init(
	struct hw_translate *translate)
{
	translate->funcs = &funcs;
}
