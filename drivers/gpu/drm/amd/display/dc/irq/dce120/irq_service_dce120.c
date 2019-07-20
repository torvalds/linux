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

#include "irq_service_dce120.h"
#include "../dce110/irq_service_dce110.h"

#include "dce/dce_12_0_offset.h"
#include "dce/dce_12_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"

#include "ivsrcid/ivsrcid_vislands30.h"

static bool hpd_ack(
	struct irq_service *irq_service,
	const struct irq_source_info *info)
{
	uint32_t addr = info->status_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);
	uint32_t current_status =
		get_reg_field_value(
			value,
			HPD0_DC_HPD_INT_STATUS,
			DC_HPD_SENSE_DELAYED);

	dal_irq_service_ack_generic(irq_service, info);

	value = dm_read_reg(irq_service->ctx, info->enable_reg);

	set_reg_field_value(
		value,
		current_status ? 0 : 1,
		HPD0_DC_HPD_INT_CONTROL,
		DC_HPD_INT_POLARITY);

	dm_write_reg(irq_service->ctx, info->enable_reg, value);

	return true;
}

static const struct irq_source_info_funcs hpd_irq_info_funcs = {
	.set = NULL,
	.ack = hpd_ack
};

static const struct irq_source_info_funcs hpd_rx_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static const struct irq_source_info_funcs pflip_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static const struct irq_source_info_funcs vblank_irq_info_funcs = {
	.set = dce110_vblank_set,
	.ack = NULL
};

static const struct irq_source_info_funcs vupdate_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

#define BASE_INNER(seg) \
	DCE_BASE__INST0_SEG ## seg

#define BASE(seg) \
	BASE_INNER(seg)

#define SRI(reg_name, block, id)\
	BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
			mm ## block ## id ## _ ## reg_name


#define IRQ_REG_ENTRY(block, reg_num, reg1, mask1, reg2, mask2)\
	.enable_reg = SRI(reg1, block, reg_num),\
	.enable_mask = \
		block ## reg_num ## _ ## reg1 ## __ ## mask1 ## _MASK,\
	.enable_value = {\
		block ## reg_num ## _ ## reg1 ## __ ## mask1 ## _MASK,\
		~block ## reg_num ## _ ## reg1 ## __ ## mask1 ## _MASK \
	},\
	.ack_reg = SRI(reg2, block, reg_num),\
	.ack_mask = \
		block ## reg_num ## _ ## reg2 ## __ ## mask2 ## _MASK,\
	.ack_value = \
		block ## reg_num ## _ ## reg2 ## __ ## mask2 ## _MASK \

#define hpd_int_entry(reg_num)\
	[DC_IRQ_SOURCE_HPD1 + reg_num] = {\
		IRQ_REG_ENTRY(HPD, reg_num,\
			DC_HPD_INT_CONTROL, DC_HPD_INT_EN,\
			DC_HPD_INT_CONTROL, DC_HPD_INT_ACK),\
		.status_reg = SRI(DC_HPD_INT_STATUS, HPD, reg_num),\
		.funcs = &hpd_irq_info_funcs\
	}

#define hpd_rx_int_entry(reg_num)\
	[DC_IRQ_SOURCE_HPD1RX + reg_num] = {\
		IRQ_REG_ENTRY(HPD, reg_num,\
			DC_HPD_INT_CONTROL, DC_HPD_RX_INT_EN,\
			DC_HPD_INT_CONTROL, DC_HPD_RX_INT_ACK),\
		.status_reg = SRI(DC_HPD_INT_STATUS, HPD, reg_num),\
		.funcs = &hpd_rx_irq_info_funcs\
	}
#define pflip_int_entry(reg_num)\
	[DC_IRQ_SOURCE_PFLIP1 + reg_num] = {\
		IRQ_REG_ENTRY(DCP, reg_num, \
			GRPH_INTERRUPT_CONTROL, GRPH_PFLIP_INT_MASK, \
			GRPH_INTERRUPT_STATUS, GRPH_PFLIP_INT_CLEAR),\
		.status_reg = SRI(GRPH_INTERRUPT_STATUS, DCP, reg_num),\
		.funcs = &pflip_irq_info_funcs\
	}

#define vupdate_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VUPDATE1 + reg_num] = {\
		IRQ_REG_ENTRY(CRTC, reg_num,\
			CRTC_INTERRUPT_CONTROL, CRTC_V_UPDATE_INT_MSK,\
			CRTC_V_UPDATE_INT_STATUS, CRTC_V_UPDATE_INT_CLEAR),\
		.funcs = &vupdate_irq_info_funcs\
	}

#define vblank_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VBLANK1 + reg_num] = {\
		IRQ_REG_ENTRY(CRTC, reg_num,\
				CRTC_VERTICAL_INTERRUPT0_CONTROL, CRTC_VERTICAL_INTERRUPT0_INT_ENABLE,\
				CRTC_VERTICAL_INTERRUPT0_CONTROL, CRTC_VERTICAL_INTERRUPT0_CLEAR),\
		.funcs = &vblank_irq_info_funcs,\
		.src_id = VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0 + reg_num\
	}

#define dummy_irq_entry() \
	{\
		.funcs = &dummy_irq_info_funcs\
	}

#define i2c_int_entry(reg_num) \
	[DC_IRQ_SOURCE_I2C_DDC ## reg_num] = dummy_irq_entry()

#define dp_sink_int_entry(reg_num) \
	[DC_IRQ_SOURCE_DPSINK ## reg_num] = dummy_irq_entry()

#define gpio_pad_int_entry(reg_num) \
	[DC_IRQ_SOURCE_GPIOPAD ## reg_num] = dummy_irq_entry()

#define dc_underflow_int_entry(reg_num) \
	[DC_IRQ_SOURCE_DC ## reg_num ## UNDERFLOW] = dummy_irq_entry()

static const struct irq_source_info_funcs dummy_irq_info_funcs = {
	.set = dal_irq_service_dummy_set,
	.ack = dal_irq_service_dummy_ack
};

static const struct irq_source_info
irq_source_info_dce120[DAL_IRQ_SOURCES_NUMBER] = {
	[DC_IRQ_SOURCE_INVALID] = dummy_irq_entry(),
	hpd_int_entry(0),
	hpd_int_entry(1),
	hpd_int_entry(2),
	hpd_int_entry(3),
	hpd_int_entry(4),
	hpd_int_entry(5),
	hpd_rx_int_entry(0),
	hpd_rx_int_entry(1),
	hpd_rx_int_entry(2),
	hpd_rx_int_entry(3),
	hpd_rx_int_entry(4),
	hpd_rx_int_entry(5),
	i2c_int_entry(1),
	i2c_int_entry(2),
	i2c_int_entry(3),
	i2c_int_entry(4),
	i2c_int_entry(5),
	i2c_int_entry(6),
	dp_sink_int_entry(1),
	dp_sink_int_entry(2),
	dp_sink_int_entry(3),
	dp_sink_int_entry(4),
	dp_sink_int_entry(5),
	dp_sink_int_entry(6),
	[DC_IRQ_SOURCE_TIMER] = dummy_irq_entry(),
	pflip_int_entry(0),
	pflip_int_entry(1),
	pflip_int_entry(2),
	pflip_int_entry(3),
	pflip_int_entry(4),
	pflip_int_entry(5),
	[DC_IRQ_SOURCE_PFLIP_UNDERLAY0] = dummy_irq_entry(),
	gpio_pad_int_entry(0),
	gpio_pad_int_entry(1),
	gpio_pad_int_entry(2),
	gpio_pad_int_entry(3),
	gpio_pad_int_entry(4),
	gpio_pad_int_entry(5),
	gpio_pad_int_entry(6),
	gpio_pad_int_entry(7),
	gpio_pad_int_entry(8),
	gpio_pad_int_entry(9),
	gpio_pad_int_entry(10),
	gpio_pad_int_entry(11),
	gpio_pad_int_entry(12),
	gpio_pad_int_entry(13),
	gpio_pad_int_entry(14),
	gpio_pad_int_entry(15),
	gpio_pad_int_entry(16),
	gpio_pad_int_entry(17),
	gpio_pad_int_entry(18),
	gpio_pad_int_entry(19),
	gpio_pad_int_entry(20),
	gpio_pad_int_entry(21),
	gpio_pad_int_entry(22),
	gpio_pad_int_entry(23),
	gpio_pad_int_entry(24),
	gpio_pad_int_entry(25),
	gpio_pad_int_entry(26),
	gpio_pad_int_entry(27),
	gpio_pad_int_entry(28),
	gpio_pad_int_entry(29),
	gpio_pad_int_entry(30),
	dc_underflow_int_entry(1),
	dc_underflow_int_entry(2),
	dc_underflow_int_entry(3),
	dc_underflow_int_entry(4),
	dc_underflow_int_entry(5),
	dc_underflow_int_entry(6),
	[DC_IRQ_SOURCE_DMCU_SCP] = dummy_irq_entry(),
	[DC_IRQ_SOURCE_VBIOS_SW] = dummy_irq_entry(),
	vupdate_int_entry(0),
	vupdate_int_entry(1),
	vupdate_int_entry(2),
	vupdate_int_entry(3),
	vupdate_int_entry(4),
	vupdate_int_entry(5),
	vblank_int_entry(0),
	vblank_int_entry(1),
	vblank_int_entry(2),
	vblank_int_entry(3),
	vblank_int_entry(4),
	vblank_int_entry(5),
};

static const struct irq_service_funcs irq_service_funcs_dce120 = {
		.to_dal_irq_source = to_dal_irq_source_dce110
};

static void construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data)
{
	dal_irq_service_construct(irq_service, init_data);

	irq_service->info = irq_source_info_dce120;
	irq_service->funcs = &irq_service_funcs_dce120;
}

struct irq_service *dal_irq_service_dce120_create(
	struct irq_service_init_data *init_data)
{
	struct irq_service *irq_service = kzalloc(sizeof(*irq_service),
						  GFP_KERNEL);

	if (!irq_service)
		return NULL;

	construct(irq_service, init_data);
	return irq_service;
}
