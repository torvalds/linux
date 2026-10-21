/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "hw_translate_dcn32.h"

#include "dm_services.h"
#include "include/gpio_types.h"
#include "../hw_translate.h"

#include "dcn/dcn_3_2_0_offset.h"
#include "dcn/dcn_3_2_0_sh_mask.h"

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


static const struct gpio_id_offset_entry gpio_offsets[] = {
	/* GENERIC */
	GPIO_MASK_ENTRY(DC_GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A__DC_GPIO_GENERICA_A_MASK,
		GPIO_ID_GENERIC, GPIO_GENERIC_A),
	GPIO_MASK_ENTRY(DC_GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A__DC_GPIO_GENERICB_A_MASK,
		GPIO_ID_GENERIC, GPIO_GENERIC_B),
	GPIO_MASK_ENTRY(DC_GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A__DC_GPIO_GENERICC_A_MASK,
		GPIO_ID_GENERIC, GPIO_GENERIC_C),
	GPIO_MASK_ENTRY(DC_GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A__DC_GPIO_GENERICD_A_MASK,
		GPIO_ID_GENERIC, GPIO_GENERIC_D),
	GPIO_MASK_ENTRY(DC_GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A__DC_GPIO_GENERICE_A_MASK,
		GPIO_ID_GENERIC, GPIO_GENERIC_E),
	GPIO_MASK_ENTRY(DC_GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A__DC_GPIO_GENERICF_A_MASK,
		GPIO_ID_GENERIC, GPIO_GENERIC_F),
	/* HPD */
	GPIO_MASK_ENTRY(DC_GPIO_HPD_A,
		DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK,
		GPIO_ID_HPD, GPIO_HPD_1),
	GPIO_MASK_ENTRY(DC_GPIO_HPD_A,
		DC_GPIO_HPD_A__DC_GPIO_HPD2_A_MASK,
		GPIO_ID_HPD, GPIO_HPD_2),
	GPIO_MASK_ENTRY(DC_GPIO_HPD_A,
		DC_GPIO_HPD_A__DC_GPIO_HPD3_A_MASK,
		GPIO_ID_HPD, GPIO_HPD_3),
	GPIO_MASK_ENTRY(DC_GPIO_HPD_A,
		DC_GPIO_HPD_A__DC_GPIO_HPD4_A_MASK,
		GPIO_ID_HPD, GPIO_HPD_4),
	GPIO_MASK_ENTRY(DC_GPIO_HPD_A,
		DC_GPIO_HPD_A__DC_GPIO_HPD5_A_MASK,
		GPIO_ID_HPD, GPIO_HPD_5),
	/* GSL */
	GPIO_MASK_ENTRY(DC_GPIO_GENLK_A,
		DC_GPIO_GENLK_A__DC_GPIO_GENLK_CLK_A_MASK,
		GPIO_ID_GSL, GPIO_GSL_GENLOCK_CLOCK),
	GPIO_MASK_ENTRY(DC_GPIO_GENLK_A,
		DC_GPIO_GENLK_A__DC_GPIO_GENLK_VSYNC_A_MASK,
		GPIO_ID_GSL, GPIO_GSL_GENLOCK_VSYNC),
	GPIO_MASK_ENTRY(DC_GPIO_GENLK_A,
		DC_GPIO_GENLK_A__DC_GPIO_SWAPLOCK_A_A_MASK,
		GPIO_ID_GSL, GPIO_GSL_SWAPLOCK_A),
	GPIO_MASK_ENTRY(DC_GPIO_GENLK_A,
		DC_GPIO_GENLK_A__DC_GPIO_SWAPLOCK_B_A_MASK,
		GPIO_ID_GSL, GPIO_GSL_SWAPLOCK_B),
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

/*
 * GSL is intentionally omitted here.
 * id_to_offset() for GSL is not implemented on this ASIC.
 */
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
	/* GENERIC */
	GPIO_PIN_ENTRY(GPIO_ID_GENERIC, GPIO_GENERIC_A,
		DC_GPIO_GENERIC_A, DC_GPIO_GENERIC_A__DC_GPIO_GENERICA_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_GENERIC, GPIO_GENERIC_B,
		DC_GPIO_GENERIC_A, DC_GPIO_GENERIC_A__DC_GPIO_GENERICB_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_GENERIC, GPIO_GENERIC_C,
		DC_GPIO_GENERIC_A, DC_GPIO_GENERIC_A__DC_GPIO_GENERICC_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_GENERIC, GPIO_GENERIC_D,
		DC_GPIO_GENERIC_A, DC_GPIO_GENERIC_A__DC_GPIO_GENERICD_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_GENERIC, GPIO_GENERIC_E,
		DC_GPIO_GENERIC_A, DC_GPIO_GENERIC_A__DC_GPIO_GENERICE_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_GENERIC, GPIO_GENERIC_F,
		DC_GPIO_GENERIC_A, DC_GPIO_GENERIC_A__DC_GPIO_GENERICF_A_MASK),
	/* HPD */
	GPIO_PIN_ENTRY(GPIO_ID_HPD, GPIO_HPD_1,
		DC_GPIO_HPD_A, DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_HPD, GPIO_HPD_2,
		DC_GPIO_HPD_A, DC_GPIO_HPD_A__DC_GPIO_HPD2_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_HPD, GPIO_HPD_3,
		DC_GPIO_HPD_A, DC_GPIO_HPD_A__DC_GPIO_HPD3_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_HPD, GPIO_HPD_4,
		DC_GPIO_HPD_A, DC_GPIO_HPD_A__DC_GPIO_HPD4_A_MASK),
	GPIO_PIN_ENTRY(GPIO_ID_HPD, GPIO_HPD_5,
		DC_GPIO_HPD_A, DC_GPIO_HPD_A__DC_GPIO_HPD5_A_MASK),
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
 * dal_hw_translate_dcn32_init
 *
 * @brief
 * Initialize Hw translate function pointers.
 *
 * @param
 * struct hw_translate *tr - [out] struct of function pointers
 *
 */
void dal_hw_translate_dcn32_init(struct hw_translate *tr)
{
	tr->funcs = &funcs;
}

