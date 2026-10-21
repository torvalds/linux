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


static const struct gpio_id_offset_entry gpio_offsets[] = {
	/* HPD */
	GPIO_ENTRY(HPD0_DC_HPD_INT_STATUS, GPIO_ID_HPD, GPIO_HPD_1),
	GPIO_ENTRY(HPD1_DC_HPD_INT_STATUS, GPIO_ID_HPD, GPIO_HPD_2),
	GPIO_ENTRY(HPD2_DC_HPD_INT_STATUS, GPIO_ID_HPD, GPIO_HPD_3),
	GPIO_ENTRY(HPD3_DC_HPD_INT_STATUS, GPIO_ID_HPD, GPIO_HPD_4),
	GPIO_ENTRY(HPD4_DC_HPD_INT_STATUS, GPIO_ID_HPD, GPIO_HPD_5),
};


/* DDC */
static const struct gpio_ddc_offset_entry ddc_offset_map[] = {
	{ REG(DC_GPIO_DDC1_A), GPIO_DDC_LINE_DDC1 },
	{ REG(DC_GPIO_DDC2_A), GPIO_DDC_LINE_DDC2 },
	{ REG(DC_GPIO_DDC3_A), GPIO_DDC_LINE_DDC3 },
	{ REG(DC_GPIO_DDC4_A), GPIO_DDC_LINE_DDC4 },
	{ REG(DC_GPIO_DDC5_A), GPIO_DDC_LINE_DDC5 },
	{ REG(DC_GPIO_DDCVGA_A), GPIO_DDC_LINE_DDC_VGA },
};


static const struct gpio_pin_entry gpio_pins[] = {
	/* DDC */
	GPIO_PIN_ENTRY(GPIO_ID_DDC_DATA, GPIO_DDC_LINE_DDC1,
		DC_GPIO_DDC1_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_DATA, GPIO_DDC_LINE_DDC2,
		DC_GPIO_DDC2_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_DATA, GPIO_DDC_LINE_DDC3,
		DC_GPIO_DDC3_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_DATA, GPIO_DDC_LINE_DDC4,
		DC_GPIO_DDC4_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_DATA, GPIO_DDC_LINE_DDC5,
		DC_GPIO_DDC5_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_DATA, GPIO_DDC_LINE_DDC_VGA,
		DC_GPIO_DDCVGA_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_CLOCK, GPIO_DDC_LINE_DDC1,
		DC_GPIO_DDC1_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_CLOCK, GPIO_DDC_LINE_DDC2,
		DC_GPIO_DDC2_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_CLOCK, GPIO_DDC_LINE_DDC3,
		DC_GPIO_DDC3_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_CLOCK, GPIO_DDC_LINE_DDC4,
		DC_GPIO_DDC4_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_CLOCK, GPIO_DDC_LINE_DDC5,
		DC_GPIO_DDC5_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_DDC_CLOCK, GPIO_DDC_LINE_DDC_VGA,
		DC_GPIO_DDCVGA_A, DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK),
};


static bool offset_to_id(
	uint32_t offset,
	uint32_t mask,
	enum gpio_id *id,
	uint32_t *en)
{
	if (dal_hw_translate_gpio_ddc_offset_to_id(
			ddc_offset_map,
			ARRAY_SIZE(ddc_offset_map),
			offset, en))
		return true;

	if (dal_hw_translate_gpio_offset_to_id(
			gpio_offsets,
			ARRAY_SIZE(gpio_offsets),
			offset, mask, id, en))
		return true;

	ASSERT_CRITICAL(false);
	return false;
}


static bool id_to_offset(
	enum gpio_id id,
	uint32_t en,
	struct gpio_pin_info *info)
{
	if (dal_hw_translate_id_to_offset(
			gpio_pins,
			ARRAY_SIZE(gpio_pins),
			id, en, info))
		return true;

	ASSERT_CRITICAL(false);
	return false;
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


