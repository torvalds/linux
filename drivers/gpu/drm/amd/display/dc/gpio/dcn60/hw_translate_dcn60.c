// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "hw_translate_dcn60.h"

#include "dm_services.h"
#include "include/gpio_types.h"
#include "../hw_translate.h"

#include "dcn/dcn_6_0_0_offset.h"
#include "dcn/dcn_6_0_0_sh_mask.h"
#include "dpcs/dpcs_6_0_0_offset.h"
#include "dpcs/dpcs_6_0_0_sh_mask.h"

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
		BASE(reg ## reg_name ## _BASE_IDX) + reg ## reg_name
#define SF_HPD(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

/* macros to expend register list macro defined in HW object header file
 * end *********************/

static bool dcn60_offset_to_id(
	uint32_t offset,
	uint32_t mask,
	enum gpio_id *id,
	uint32_t *en)
{
	(void)mask;
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
	/* DDC */
	/* we don't care about the GPIO_ID for DDC
	 * it will use GPIO_ID_DDC_DATA/GPIO_ID_DDC_CLOCK
	 * directly in the create method
	 */
	case REG(DC_I3C0_DC_I3CPAD_CONTROL0):
		*en = GPIO_DDC_LINE_DDC1;
		return true;
	case REG(DC_I3C1_DC_I3CPAD_CONTROL0):
		*en = GPIO_DDC_LINE_DDC2;
		return true;

	/* UNEXPECTED */
	default:
		ASSERT_CRITICAL(false);
		return false;
	}
}

static bool dcn60_id_to_offset(
	enum gpio_id id,
	uint32_t en,
	struct gpio_pin_info *info)
{
	bool result = true;

	switch (id) {
	case GPIO_ID_DDC_DATA:
		switch (en) {
		case GPIO_DDC_LINE_DDC1:
			info->offset = REG(DC_I3C0_DC_I3CPAD_CONTROL0);
		break;
		case GPIO_DDC_LINE_DDC2:
			info->offset = REG(DC_I3C1_DC_I3CPAD_CONTROL0);
		break;
		default:
			ASSERT_CRITICAL(false);
			result = false;
		}
	break;
	case GPIO_ID_DDC_CLOCK:
		switch (en) {
		case GPIO_DDC_LINE_DDC1:
			info->offset = REG(DC_I3C0_DC_I3CPAD_CONTROL0);
		break;
		case GPIO_DDC_LINE_DDC2:
			info->offset = REG(DC_I3C1_DC_I3CPAD_CONTROL0);
		break;
		default:
			ASSERT_CRITICAL(false);
			result = false;
		}
	break;
	case GPIO_ID_HPD:
		switch (en) {
		case GPIO_HPD_1:
			info->offset = REG(HPD0_DC_HPD_INT_STATUS);
			info->mask = HPD0_DC_HPD_INT_STATUS__DC_HPD_SENSE_MASK;
			break;
		case GPIO_HPD_2:
			info->offset = REG(HPD1_DC_HPD_INT_STATUS);
			info->mask = HPD0_DC_HPD_INT_STATUS__DC_HPD_SENSE_MASK;
			break;
		case GPIO_HPD_3:
			info->offset = REG(HPD2_DC_HPD_INT_STATUS);
			info->mask = HPD0_DC_HPD_INT_STATUS__DC_HPD_SENSE_MASK;
			break;
		case GPIO_HPD_4:
			info->offset = REG(HPD3_DC_HPD_INT_STATUS);
			info->mask = HPD0_DC_HPD_INT_STATUS__DC_HPD_SENSE_MASK;
			break;
		default:
			ASSERT_CRITICAL(false);
			result = false;
		}
	break;
	default:
		ASSERT_CRITICAL(false);
		result = false;
	}

	return result;
}

/* function table */
static const struct hw_translate_funcs funcs = {
	.offset_to_id = dcn60_offset_to_id,
	.id_to_offset = dcn60_id_to_offset,
};

/*
 * dal_hw_translate_dcn60_init
 *
 * @brief
 * Initialize Hw translate function pointers.
 *
 * @param
 * struct hw_translate *tr - [out] struct of function pointers
 *
 */
void dal_hw_translate_dcn60_init(struct hw_translate *tr)
{
	tr->funcs = &funcs;
}

