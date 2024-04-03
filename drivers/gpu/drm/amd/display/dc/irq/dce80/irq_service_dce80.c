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

#include "irq_service_dce80.h"
#include "../dce110/irq_service_dce110.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include "dc_types.h"

static bool hpd_ack(
	struct irq_service *irq_service,
	const struct irq_source_info *info)
{
	uint32_t addr = info->status_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);
	uint32_t current_status =
		get_reg_field_value(
			value,
			DC_HPD1_INT_STATUS,
			DC_HPD1_SENSE_DELAYED);

	dal_irq_service_ack_generic(irq_service, info);

	value = dm_read_reg(irq_service->ctx, info->enable_reg);

	set_reg_field_value(
		value,
		current_status ? 0 : 1,
		DC_HPD1_INT_CONTROL,
		DC_HPD1_INT_POLARITY);

	dm_write_reg(irq_service->ctx, info->enable_reg, value);

	return true;
}

static struct irq_source_info_funcs hpd_irq_info_funcs  = {
	.set = NULL,
	.ack = hpd_ack
};

static struct irq_source_info_funcs hpd_rx_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static struct irq_source_info_funcs pflip_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static struct irq_source_info_funcs vblank_irq_info_funcs = {
	.set = dce110_vblank_set,
	.ack = NULL
};

static struct irq_source_info_funcs vupdate_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

#define hpd_int_entry(reg_num)\
	[DC_IRQ_SOURCE_INVALID + reg_num] = {\
		.enable_reg = mmDC_HPD ## reg_num ## _INT_CONTROL,\
		.enable_mask = DC_HPD1_INT_CONTROL__DC_HPD1_INT_EN_MASK,\
		.enable_value = {\
			DC_HPD1_INT_CONTROL__DC_HPD1_INT_EN_MASK,\
			~DC_HPD1_INT_CONTROL__DC_HPD1_INT_EN_MASK\
		},\
		.ack_reg = mmDC_HPD ## reg_num ## _INT_CONTROL,\
		.ack_mask = DC_HPD1_INT_CONTROL__DC_HPD1_INT_ACK_MASK,\
		.ack_value = DC_HPD1_INT_CONTROL__DC_HPD1_INT_ACK_MASK,\
		.status_reg = mmDC_HPD ## reg_num ## _INT_STATUS,\
		.funcs = &hpd_irq_info_funcs\
	}

#define hpd_rx_int_entry(reg_num)\
	[DC_IRQ_SOURCE_HPD6 + reg_num] = {\
		.enable_reg = mmDC_HPD ## reg_num ## _INT_CONTROL,\
		.enable_mask = DC_HPD1_INT_CONTROL__DC_HPD1_RX_INT_EN_MASK,\
		.enable_value = {\
				DC_HPD1_INT_CONTROL__DC_HPD1_RX_INT_EN_MASK,\
			~DC_HPD1_INT_CONTROL__DC_HPD1_RX_INT_EN_MASK },\
		.ack_reg = mmDC_HPD ## reg_num ## _INT_CONTROL,\
		.ack_mask = DC_HPD1_INT_CONTROL__DC_HPD1_RX_INT_ACK_MASK,\
		.ack_value = DC_HPD1_INT_CONTROL__DC_HPD1_RX_INT_ACK_MASK,\
		.status_reg = mmDC_HPD ## reg_num ## _INT_STATUS,\
		.funcs = &hpd_rx_irq_info_funcs\
	}

#define pflip_int_entry(reg_num)\
	[DC_IRQ_SOURCE_PFLIP1 + reg_num] = {\
		.enable_reg = mmDCP ## reg_num ## _GRPH_INTERRUPT_CONTROL,\
		.enable_mask =\
		GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK,\
		.enable_value = {\
			GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK,\
			~GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK},\
		.ack_reg = mmDCP ## reg_num ## _GRPH_INTERRUPT_STATUS,\
		.ack_mask = GRPH_INTERRUPT_STATUS__GRPH_PFLIP_INT_CLEAR_MASK,\
		.ack_value = GRPH_INTERRUPT_STATUS__GRPH_PFLIP_INT_CLEAR_MASK,\
		.status_reg = mmDCP ## reg_num ##_GRPH_INTERRUPT_STATUS,\
		.funcs = &pflip_irq_info_funcs\
 	}

#define vupdate_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VUPDATE1 + reg_num] = {\
		.enable_reg = mmCRTC ## reg_num ## _CRTC_INTERRUPT_CONTROL,\
		.enable_mask =\
		CRTC_INTERRUPT_CONTROL__CRTC_V_UPDATE_INT_MSK_MASK,\
		.enable_value = {\
			CRTC_INTERRUPT_CONTROL__CRTC_V_UPDATE_INT_MSK_MASK,\
			~CRTC_INTERRUPT_CONTROL__CRTC_V_UPDATE_INT_MSK_MASK},\
		.ack_reg = mmCRTC ## reg_num ## _CRTC_V_UPDATE_INT_STATUS,\
		.ack_mask =\
		CRTC_V_UPDATE_INT_STATUS__CRTC_V_UPDATE_INT_CLEAR_MASK,\
		.ack_value =\
		CRTC_V_UPDATE_INT_STATUS__CRTC_V_UPDATE_INT_CLEAR_MASK,\
		.funcs = &vupdate_irq_info_funcs\
	}

#define vblank_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VBLANK1 + reg_num] = {\
		.enable_reg = mmCRTC ## reg_num ## _CRTC_VERTICAL_INTERRUPT0_CONTROL,\
		.enable_mask =\
		CRTC_VERTICAL_INTERRUPT0_CONTROL__CRTC_VERTICAL_INTERRUPT0_INT_ENABLE_MASK,\
		.enable_value = {\
			CRTC_VERTICAL_INTERRUPT0_CONTROL__CRTC_VERTICAL_INTERRUPT0_INT_ENABLE_MASK,\
			~CRTC_VERTICAL_INTERRUPT0_CONTROL__CRTC_VERTICAL_INTERRUPT0_INT_ENABLE_MASK},\
		.ack_reg = mmCRTC ## reg_num ## _CRTC_VERTICAL_INTERRUPT0_CONTROL,\
		.ack_mask =\
		CRTC_VERTICAL_INTERRUPT0_CONTROL__CRTC_VERTICAL_INTERRUPT0_CLEAR_MASK,\
		.ack_value =\
		CRTC_VERTICAL_INTERRUPT0_CONTROL__CRTC_VERTICAL_INTERRUPT0_CLEAR_MASK,\
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


static struct irq_source_info_funcs dummy_irq_info_funcs = {
	.set = dal_irq_service_dummy_set,
	.ack = dal_irq_service_dummy_ack
};

static const struct irq_source_info
irq_source_info_dce80[DAL_IRQ_SOURCES_NUMBER] = {
	[DC_IRQ_SOURCE_INVALID] = dummy_irq_entry(),
	hpd_int_entry(1),
	hpd_int_entry(2),
	hpd_int_entry(3),
	hpd_int_entry(4),
	hpd_int_entry(5),
	hpd_int_entry(6),
	hpd_rx_int_entry(1),
	hpd_rx_int_entry(2),
	hpd_rx_int_entry(3),
	hpd_rx_int_entry(4),
	hpd_rx_int_entry(5),
	hpd_rx_int_entry(6),
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

static const struct irq_service_funcs irq_service_funcs_dce80 = {
		.to_dal_irq_source = to_dal_irq_source_dce110
};

static void dce80_irq_construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data)
{
	dal_irq_service_construct(irq_service, init_data);

	irq_service->info = irq_source_info_dce80;
	irq_service->funcs = &irq_service_funcs_dce80;
}

struct irq_service *dal_irq_service_dce80_create(
	struct irq_service_init_data *init_data)
{
	struct irq_service *irq_service = kzalloc(sizeof(*irq_service),
						  GFP_KERNEL);

	if (!irq_service)
		return NULL;

	dce80_irq_construct(irq_service, init_data);
	return irq_service;
}


