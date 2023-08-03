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

#include "irq_service_dce110.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include "dc.h"
#include "core_types.h"
#define DC_LOGGER \
	irq_service->ctx->logger

static bool hpd_ack(struct irq_service *irq_service,
		    const struct irq_source_info *info)
{
	uint32_t addr = info->status_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);
	uint32_t current_status = get_reg_field_value(value,
						      DC_HPD_INT_STATUS,
						      DC_HPD_SENSE_DELAYED);

	dal_irq_service_ack_generic(irq_service, info);

	value = dm_read_reg(irq_service->ctx, info->enable_reg);

	set_reg_field_value(value, current_status ? 0 : 1,
			    DC_HPD_INT_CONTROL,
			    DC_HPD_INT_POLARITY);

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
	[DC_IRQ_SOURCE_HPD1 + reg_num] = {\
		.enable_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.enable_mask = DC_HPD_INT_CONTROL__DC_HPD_INT_EN_MASK,\
		.enable_value = {\
			DC_HPD_INT_CONTROL__DC_HPD_INT_EN_MASK,\
			~DC_HPD_INT_CONTROL__DC_HPD_INT_EN_MASK\
		},\
		.ack_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.ack_mask = DC_HPD_INT_CONTROL__DC_HPD_INT_ACK_MASK,\
		.ack_value = DC_HPD_INT_CONTROL__DC_HPD_INT_ACK_MASK,\
		.status_reg = mmHPD ## reg_num ## _DC_HPD_INT_STATUS,\
		.funcs = &hpd_irq_info_funcs\
	}

#define hpd_rx_int_entry(reg_num)\
	[DC_IRQ_SOURCE_HPD1RX + reg_num] = {\
		.enable_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.enable_mask = DC_HPD_INT_CONTROL__DC_HPD_RX_INT_EN_MASK,\
		.enable_value = {\
			DC_HPD_INT_CONTROL__DC_HPD_RX_INT_EN_MASK,\
			~DC_HPD_INT_CONTROL__DC_HPD_RX_INT_EN_MASK },\
		.ack_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.ack_mask = DC_HPD_INT_CONTROL__DC_HPD_RX_INT_ACK_MASK,\
		.ack_value = DC_HPD_INT_CONTROL__DC_HPD_RX_INT_ACK_MASK,\
		.status_reg = mmHPD ## reg_num ## _DC_HPD_INT_STATUS,\
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

bool dal_irq_service_dummy_set(struct irq_service *irq_service,
			       const struct irq_source_info *info,
			       bool enable)
{
	DC_LOG_ERROR("%s: called for non-implemented irq source, src_id=%u, ext_id=%u\n",
		     __func__, info->src_id, info->ext_id);

	return false;
}

bool dal_irq_service_dummy_ack(struct irq_service *irq_service,
			       const struct irq_source_info *info)
{
	DC_LOG_ERROR("%s: called for non-implemented irq source, src_id=%u, ext_id=%u\n",
		     __func__, info->src_id, info->ext_id);

	return false;
}


bool dce110_vblank_set(struct irq_service *irq_service,
		       const struct irq_source_info *info,
		       bool enable)
{
	struct dc_context *dc_ctx = irq_service->ctx;
	struct dc *dc = irq_service->ctx->dc;
	enum dc_irq_source dal_irq_src =
			dc_interrupt_to_irq_source(irq_service->ctx->dc,
						   info->src_id,
						   info->ext_id);
	uint8_t pipe_offset = dal_irq_src - IRQ_TYPE_VBLANK;

	struct timing_generator *tg =
			dc->current_state->res_ctx.pipe_ctx[pipe_offset].stream_res.tg;

	if (enable) {
		if (!tg || !tg->funcs->arm_vert_intr(tg, 2)) {
			DC_ERROR("Failed to get VBLANK!\n");
			return false;
		}
	}

	dal_irq_service_set_generic(irq_service, info, enable);
	return true;
}

static struct irq_source_info_funcs dummy_irq_info_funcs = {
	.set = dal_irq_service_dummy_set,
	.ack = dal_irq_service_dummy_ack
};

static const struct irq_source_info
irq_source_info_dce110[DAL_IRQ_SOURCES_NUMBER] = {
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

enum dc_irq_source to_dal_irq_source_dce110(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id)
{
	switch (src_id) {
	case VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK1;
	case VISLANDS30_IV_SRCID_D2_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK2;
	case VISLANDS30_IV_SRCID_D3_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK3;
	case VISLANDS30_IV_SRCID_D4_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK4;
	case VISLANDS30_IV_SRCID_D5_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK5;
	case VISLANDS30_IV_SRCID_D6_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK6;
	case VISLANDS30_IV_SRCID_D1_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE1;
	case VISLANDS30_IV_SRCID_D2_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE2;
	case VISLANDS30_IV_SRCID_D3_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE3;
	case VISLANDS30_IV_SRCID_D4_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE4;
	case VISLANDS30_IV_SRCID_D5_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE5;
	case VISLANDS30_IV_SRCID_D6_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE6;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP1;
	case VISLANDS30_IV_SRCID_D2_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP2;
	case VISLANDS30_IV_SRCID_D3_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP3;
	case VISLANDS30_IV_SRCID_D4_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP4;
	case VISLANDS30_IV_SRCID_D5_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP5;
	case VISLANDS30_IV_SRCID_D6_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP6;

	case VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A:
		/* generic src_id for all HPD and HPDRX interrupts */
		switch (ext_id) {
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_A:
			return DC_IRQ_SOURCE_HPD1;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_B:
			return DC_IRQ_SOURCE_HPD2;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_C:
			return DC_IRQ_SOURCE_HPD3;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_D:
			return DC_IRQ_SOURCE_HPD4;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_E:
			return DC_IRQ_SOURCE_HPD5;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_F:
			return DC_IRQ_SOURCE_HPD6;
		case VISLANDS30_IV_EXTID_HPD_RX_A:
			return DC_IRQ_SOURCE_HPD1RX;
		case VISLANDS30_IV_EXTID_HPD_RX_B:
			return DC_IRQ_SOURCE_HPD2RX;
		case VISLANDS30_IV_EXTID_HPD_RX_C:
			return DC_IRQ_SOURCE_HPD3RX;
		case VISLANDS30_IV_EXTID_HPD_RX_D:
			return DC_IRQ_SOURCE_HPD4RX;
		case VISLANDS30_IV_EXTID_HPD_RX_E:
			return DC_IRQ_SOURCE_HPD5RX;
		case VISLANDS30_IV_EXTID_HPD_RX_F:
			return DC_IRQ_SOURCE_HPD6RX;
		default:
			return DC_IRQ_SOURCE_INVALID;
		}
		break;

	default:
		return DC_IRQ_SOURCE_INVALID;
	}
}

static const struct irq_service_funcs irq_service_funcs_dce110 = {
		.to_dal_irq_source = to_dal_irq_source_dce110
};

static void dce110_irq_construct(struct irq_service *irq_service,
		      struct irq_service_init_data *init_data)
{
	dal_irq_service_construct(irq_service, init_data);

	irq_service->info = irq_source_info_dce110;
	irq_service->funcs = &irq_service_funcs_dce110;
}

struct irq_service *
dal_irq_service_dce110_create(struct irq_service_init_data *init_data)
{
	struct irq_service *irq_service = kzalloc(sizeof(*irq_service),
						  GFP_KERNEL);

	if (!irq_service)
		return NULL;

	dce110_irq_construct(irq_service, init_data);
	return irq_service;
}
