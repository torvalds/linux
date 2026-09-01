// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "include/gpio_types.h"
#include "../hw_factory.h"

#include "../hw_gpio.h"
#include "../hw_ddc.h"
#include "../hw_hpd.h"
#include "../hw_generic.h"

#include "dcn/dcn_6_0_0_offset.h"
#include "dcn/dcn_6_0_0_sh_mask.h"
#include "dpcs/dpcs_6_0_0_offset.h"
#include "dpcs/dpcs_6_0_0_sh_mask.h"

#include "reg_helper.h"
#include "../hpd_regs.h"
#include "hw_factory_dcn60.h"

#define DCN_BASE__INST0_SEG2                       0x000034C0

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
#define block HPD
#define reg_num 0

#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define REG(reg_name)\
		BASE(reg ## reg_name ## _BASE_IDX) + reg ## reg_name

#define SF_HPD(reg_name, field_name, post_fix)\
	.field_name = HPD0_ ## reg_name ## __ ## field_name ## post_fix

#define REGI(reg_name, block, id)\
	BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
				reg ## block ## id ## _ ## reg_name

#define SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

/* macros to expend register list macro defined in HW object header file
 * end *********************/

// DC_GPIO_HPD_* registers are gone.
#undef HPD_REG_LIST
#define HPD_REG_LIST(id) \
	.int_status = REGI(DC_HPD_INT_STATUS, HPD, id),\
	.toggle_filt_cntl = REGI(DC_HPD_TOGGLE_FILT_CNTL, HPD, id)

#define hpd_regs(id) \
{\
	HPD_REG_LIST(id)\
}

static const struct hpd_registers hpd_regs[] = {
	hpd_regs(0),
	hpd_regs(1),
	hpd_regs(2),
	hpd_regs(3),
};

static const struct hpd_sh_mask hpd_shift = {
		HPD_MASK_SH_LIST(__SHIFT)
};

static const struct hpd_sh_mask hpd_mask = {
		HPD_MASK_SH_LIST(_MASK)
};

#include "../ddc_regs.h"

 /* set field name */
#define SF_DDC(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DDC_REG_LIST_DCN6(id)\
	.ddc_setup = REG(DC_I2C_DDC ## id ## _SETUP),\
	.phy_aux_cntl = REG(PHY_AUX_CNTL),\
	.dc_gpio_aux_ctrl_5 = REG(DC_GPIO_AUX_CTRL_5)

#define I3CPAD_REG_LIST_DCN6(id) \
	.dc_i3cpad_control0 = REG(DC_I3C ## id ## _DC_I3CPAD_CONTROL0),\
	.dc_i3cpad_control1 = REG(DC_I3C ## id ## _DC_I3CPAD_CONTROL1)

#define ddc_regs_dcn6(id, i3cpad_id) \
{\
	DDC_REG_LIST_DCN6(id), \
	I3CPAD_REG_LIST_DCN6(i3cpad_id) \
}

static const struct ddc_registers ddc_regs[] = {
	ddc_regs_dcn6(1, 0),
	ddc_regs_dcn6(2, 1)
};

#define DDC_MASK_SH_LIST_DCN6(mask_sh) \
		{SF_DDC(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_ENABLE, mask_sh),\
		SF_DDC(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_EDID_DETECT_ENABLE, mask_sh),\
		SF_DDC(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_EDID_DETECT_MODE, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_DDCCLK_MASK, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_DDCDATA_MASK, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_CLK_A, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_DATA_A, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_CLK_EN, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_DATA_EN, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_CLK_Y, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_DATA_Y, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL0, DC_I3CPAD_PD_EN, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL1, DC_I3CPAD_STR, mask_sh),\
		SF_DDC(DC_I3C0_DC_I3CPAD_CONTROL1, DC_I3CPAD_RXSEL, mask_sh)}

static const struct ddc_sh_mask ddc_shift[] = {
	DDC_MASK_SH_LIST_DCN6(__SHIFT),
	DDC_MASK_SH_LIST_DCN6(__SHIFT)
};

static const struct ddc_sh_mask ddc_mask[] = {
	DDC_MASK_SH_LIST_DCN6(_MASK),
	DDC_MASK_SH_LIST_DCN6(_MASK)
};

#include "../generic_regs.h"

/* set field name */
#define SF_GENERIC(reg_name, field_name, post_fix)\
	.field_name = 0

static const struct generic_registers generic_regs[] = {
	{{ 0 }},
	{{ 0 }},
};

static const struct generic_sh_mask generic_shift[] = {
	{ 0 },
	{ 0 },
};

static const struct generic_sh_mask generic_mask[] = {
	{ 0 },
	{ 0 },
};

static void dcn60_define_generic_registers(struct hw_gpio_pin *pin, uint32_t en)
{
	struct hw_generic *generic = HW_GENERIC_FROM_BASE(pin);

	generic->regs = &generic_regs[en];
	generic->shifts = &generic_shift[en];
	generic->masks = &generic_mask[en];
	generic->base.regs = &generic_regs[en].gpio;
}

static void dcn60_define_ddc_registers(
	struct hw_gpio_pin *pin,
	uint32_t en)
{
	struct hw_ddc *ddc = HW_DDC_FROM_BASE(pin);

	switch (pin->id) {
	case GPIO_ID_DDC_DATA:
	case GPIO_ID_DDC_CLOCK:
		ddc->regs = &ddc_regs[en];
		ddc->base.regs = &ddc_regs[en].gpio;
		break;
	default:
		ASSERT_CRITICAL(false);
		return;
	}

	ddc->shifts = &ddc_shift[en];
	ddc->masks = &ddc_mask[en];

}

static void dcn60_define_hpd_registers(struct hw_gpio_pin *pin, uint32_t en)
{
	struct hw_hpd *hpd = HW_HPD_FROM_BASE(pin);

	hpd->regs = &hpd_regs[en];
	hpd->shifts = &hpd_shift;
	hpd->masks = &hpd_mask;
	hpd->base.regs = &hpd_regs[en].gpio;
}

/* function table */
static const struct hw_factory_funcs funcs = {
	.init_ddc_data = dal_hw_ddc_init_i3cpad,
	.init_generic = dal_hw_generic_init,
	.init_hpd = dal_hw_hpd_init,
	.get_ddc_pin = dal_hw_ddc_get_pin,
	.get_hpd_pin = dal_hw_hpd_get_pin,
	.get_generic_pin = dal_hw_generic_get_pin,
	.define_hpd_registers = dcn60_define_hpd_registers,
	.define_ddc_registers = dcn60_define_ddc_registers,
	.define_generic_registers = dcn60_define_generic_registers
};

/*
 * dal_hw_factory_dcn60_init
 *
 * @brief
 * Initialize HW factory function pointers and pin info
 *
 * @param
 * struct hw_factory *factory - [out] struct of function pointers
 */
void dal_hw_factory_dcn60_init(struct hw_factory *factory)
{
	factory->number_of_pins[GPIO_ID_DDC_DATA] = 2;
	factory->number_of_pins[GPIO_ID_DDC_CLOCK] = 2;
	factory->number_of_pins[GPIO_ID_GENERIC] = 2;
	factory->number_of_pins[GPIO_ID_HPD] = 4;
	factory->number_of_pins[GPIO_ID_GPIO_PAD] = 0;
	factory->number_of_pins[GPIO_ID_VIP_PAD] = 0;
	factory->number_of_pins[GPIO_ID_SYNC] = 0;
	factory->number_of_pins[GPIO_ID_GSL] = 0;

	factory->funcs = &funcs;
}
