// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "hw_translate_dcn42.h"

#include "dm_services.h"
#include "include/gpio_types.h"
#include "../hw_translate.h"


#include "dcn/dcn_4_2_0_offset.h"
#include "dcn/dcn_4_2_0_sh_mask.h"
#include "dpcs/dpcs_4_0_0_offset.h"
#include "dpcs/dpcs_4_0_0_sh_mask.h"

#define DCN_BASE__INST0_SEG2                       0x000034C0

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
#define block HPD
#define reg_num 0

#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#undef REG
#define REG(reg_name)\
		(BASE(reg ## reg_name ## _BASE_IDX) + reg ## reg_name)
#define SF_HPD(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix


/* macros to expend register list macro defined in HW object header file
 * end *********************/


static bool offset_to_id(
	uint32_t offset,
	uint32_t mask,
	enum gpio_id *id,
	uint32_t *en)
{
	switch (offset) {
	/* HPD */
	case REG(HPD0_DC_HPD_INT_STATUS):
		*id = GPIO_ID_HPD;
		*en = GPIO_HPD_1;
		return true;
	case REG(HPD1_DC_HPD_INT_STATUS):
		*id = GPIO_ID_HPD;
		*en = GPIO_HPD_2;
		return true;
	case REG(HPD2_DC_HPD_INT_STATUS):
		*id = GPIO_ID_HPD;
		*en = GPIO_HPD_3;
		return true;
	case REG(HPD3_DC_HPD_INT_STATUS):
		*id = GPIO_ID_HPD;
		*en = GPIO_HPD_4;
		return true;
	case REG(HPD4_DC_HPD_INT_STATUS):
		*id = GPIO_ID_HPD;
		*en = GPIO_HPD_5;
		return true;
	/* DDC */
	/* we don't care about the GPIO_ID for DDC
	 * in DdcHandle it will use GPIO_ID_DDC_DATA/GPIO_ID_DDC_CLOCK
	 * directly in the create method
	 */
	case REG(DC_GPIO_DDC1_A):
		*en = GPIO_DDC_LINE_DDC1;
		return true;
	case REG(DC_GPIO_DDC2_A):
		*en = GPIO_DDC_LINE_DDC2;
		return true;
	case REG(DC_GPIO_DDC3_A):
		*en = GPIO_DDC_LINE_DDC3;
		return true;
	case REG(DC_GPIO_DDC4_A):
		*en = GPIO_DDC_LINE_DDC4;
		return true;
	case REG(DC_GPIO_DDC5_A):
		*en = GPIO_DDC_LINE_DDC5;
		return true;
	case REG(DC_GPIO_DDCVGA_A):
		*en = GPIO_DDC_LINE_DDC_VGA;
		return true;
	default:
		ASSERT_CRITICAL(false);
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
		info->mask = DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK;
		switch (en) {
		case GPIO_DDC_LINE_DDC1:
			info->offset = REG(DC_GPIO_DDC1_A);
		break;
		case GPIO_DDC_LINE_DDC2:
			info->offset = REG(DC_GPIO_DDC2_A);
		break;
		case GPIO_DDC_LINE_DDC3:
			info->offset = REG(DC_GPIO_DDC3_A);
		break;
		case GPIO_DDC_LINE_DDC4:
			info->offset = REG(DC_GPIO_DDC4_A);
		break;
		case GPIO_DDC_LINE_DDC5:
			info->offset = REG(DC_GPIO_DDC5_A);
		break;
		case GPIO_DDC_LINE_DDC_VGA:
			info->offset = REG(DC_GPIO_DDCVGA_A);
		break;
		case GPIO_DDC_LINE_I2C_PAD:
		default:
			ASSERT_CRITICAL(false);
			result = false;
		}
	break;
	case GPIO_ID_DDC_CLOCK:
		info->mask = DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK;
		switch (en) {
		case GPIO_DDC_LINE_DDC1:
			info->offset = REG(DC_GPIO_DDC1_A);
		break;
		case GPIO_DDC_LINE_DDC2:
			info->offset = REG(DC_GPIO_DDC2_A);
		break;
		case GPIO_DDC_LINE_DDC3:
			info->offset = REG(DC_GPIO_DDC3_A);
		break;
		case GPIO_DDC_LINE_DDC4:
			info->offset = REG(DC_GPIO_DDC4_A);
		break;
		case GPIO_DDC_LINE_DDC5:
			info->offset = REG(DC_GPIO_DDC5_A);
		break;
		case GPIO_DDC_LINE_DDC_VGA:
			info->offset = REG(DC_GPIO_DDCVGA_A);
		break;
		case GPIO_DDC_LINE_I2C_PAD:
		default:
			ASSERT_CRITICAL(false);
			result = false;
		}
	break;
	case GPIO_ID_SYNC:
	case GPIO_ID_VIP_PAD:
	default:
		ASSERT_CRITICAL(false);
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


/* function table */
static const struct hw_translate_funcs funcs = {
	.offset_to_id = offset_to_id,
	.id_to_offset = id_to_offset,
};


/*
 * dal_hw_translate_dcn42_init
 *
 * @brief
 * Initialize Hw translate function pointers.
 *
 * @param
 * struct hw_translate *tr - [out] struct of function pointers
 *
 */
void dal_hw_translate_dcn42_init(struct hw_translate *tr)
{
	tr->funcs = &funcs;
}


