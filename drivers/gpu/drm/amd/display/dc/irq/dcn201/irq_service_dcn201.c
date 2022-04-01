/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include "../dce110/irq_service_dce110.h"

#include "dcn/dcn_2_0_3_offset.h"
#include "dcn/dcn_2_0_3_sh_mask.h"

#include "cyan_skillfish_ip_offset.h"
#include "soc15_hw_ip.h"

#include "irq_service_dcn201.h"

#include "ivsrcid/dcn/irqsrcs_dcn_1_0.h"

static enum dc_irq_source to_dal_irq_source_dcn201(struct irq_service *irq_service,
						   uint32_t src_id,
						   uint32_t ext_id)
{
	switch (src_id) {
	case DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP:
		return DC_IRQ_SOURCE_VBLANK1;
	case DCN_1_0__SRCID__DC_D2_OTG_VSTARTUP:
		return DC_IRQ_SOURCE_VBLANK2;
	case DCN_1_0__SRCID__OTG1_VERTICAL_INTERRUPT0_CONTROL:
		return DC_IRQ_SOURCE_DC1_VLINE0;
	case DCN_1_0__SRCID__OTG2_VERTICAL_INTERRUPT0_CONTROL:
		return DC_IRQ_SOURCE_DC2_VLINE0;
	case DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT:
		return DC_IRQ_SOURCE_PFLIP1;
	case DCN_1_0__SRCID__HUBP1_FLIP_INTERRUPT:
		return DC_IRQ_SOURCE_PFLIP2;
	case DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT:
		return DC_IRQ_SOURCE_VUPDATE1;
	case DCN_1_0__SRCID__OTG1_IHC_V_UPDATE_NO_LOCK_INTERRUPT:
		return DC_IRQ_SOURCE_VUPDATE2;
	case DCN_1_0__SRCID__DC_HPD1_INT:
		/* generic src_id for all HPD and HPDRX interrupts */
		switch (ext_id) {
		case DCN_1_0__CTXID__DC_HPD1_INT:
			return DC_IRQ_SOURCE_HPD1;
		case DCN_1_0__CTXID__DC_HPD2_INT:
			return DC_IRQ_SOURCE_HPD2;
		case DCN_1_0__CTXID__DC_HPD1_RX_INT:
			return DC_IRQ_SOURCE_HPD1RX;
		case DCN_1_0__CTXID__DC_HPD2_RX_INT:
			return DC_IRQ_SOURCE_HPD2RX;
		default:
			return DC_IRQ_SOURCE_INVALID;
		}
		break;

	default:
		return DC_IRQ_SOURCE_INVALID;
	}
	return DC_IRQ_SOURCE_INVALID;
}

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
	.set = NULL,
	.ack = NULL
};

static const struct irq_source_info_funcs vline0_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};
static const struct irq_source_info_funcs vupdate_no_lock_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static const struct irq_source_info_funcs dmub_outbox_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

#undef BASE_INNER
#define BASE_INNER(seg) DMU_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

/* compile time expand base address. */
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
		IRQ_REG_ENTRY(HUBPREQ, reg_num,\
			DCSURF_SURFACE_FLIP_INTERRUPT, SURFACE_FLIP_INT_MASK,\
			DCSURF_SURFACE_FLIP_INTERRUPT, SURFACE_FLIP_CLEAR),\
		.funcs = &pflip_irq_info_funcs\
	}

#define vupdate_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VUPDATE1 + reg_num] = {\
		IRQ_REG_ENTRY(OTG, reg_num,\
			OTG_GLOBAL_SYNC_STATUS, VUPDATE_INT_EN,\
			OTG_GLOBAL_SYNC_STATUS, VUPDATE_EVENT_CLEAR),\
		.funcs = &vblank_irq_info_funcs\
	}

/* vupdate_no_lock_int_entry maps to DC_IRQ_SOURCE_VUPDATEx, to match semantic
 * of DCE's DC_IRQ_SOURCE_VUPDATEx.
 */
#define vupdate_no_lock_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VUPDATE1 + reg_num] = {\
		IRQ_REG_ENTRY(OTG, reg_num,\
			OTG_GLOBAL_SYNC_STATUS, VUPDATE_NO_LOCK_INT_EN,\
			OTG_GLOBAL_SYNC_STATUS, VUPDATE_NO_LOCK_EVENT_CLEAR),\
		.funcs = &vupdate_no_lock_irq_info_funcs\
	}
#define vblank_int_entry(reg_num)\
	[DC_IRQ_SOURCE_VBLANK1 + reg_num] = {\
		IRQ_REG_ENTRY(OTG, reg_num,\
			OTG_GLOBAL_SYNC_STATUS, VSTARTUP_INT_EN,\
			OTG_GLOBAL_SYNC_STATUS, VSTARTUP_EVENT_CLEAR),\
		.funcs = &vblank_irq_info_funcs\
	}

#define vline0_int_entry(reg_num)\
	[DC_IRQ_SOURCE_DC1_VLINE0 + reg_num] = {\
		IRQ_REG_ENTRY(OTG, reg_num,\
			OTG_VERTICAL_INTERRUPT0_CONTROL, OTG_VERTICAL_INTERRUPT0_INT_ENABLE,\
			OTG_VERTICAL_INTERRUPT0_CONTROL, OTG_VERTICAL_INTERRUPT0_CLEAR),\
		.funcs = &vline0_irq_info_funcs\
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
irq_source_info_dcn201[DAL_IRQ_SOURCES_NUMBER] = {
	[DC_IRQ_SOURCE_INVALID] = dummy_irq_entry(),
	hpd_int_entry(0),
	hpd_int_entry(1),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	hpd_rx_int_entry(0),
	hpd_rx_int_entry(1),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	i2c_int_entry(1),
	i2c_int_entry(2),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dp_sink_int_entry(1),
	dp_sink_int_entry(2),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	[DC_IRQ_SOURCE_TIMER] = dummy_irq_entry(),
	pflip_int_entry(0),
	pflip_int_entry(1),
	pflip_int_entry(2),
	pflip_int_entry(3),
	[DC_IRQ_SOURCE_PFLIP5] = dummy_irq_entry(),
	[DC_IRQ_SOURCE_PFLIP6] = dummy_irq_entry(),
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
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	[DC_IRQ_SOURCE_DMCU_SCP] = dummy_irq_entry(),
	[DC_IRQ_SOURCE_VBIOS_SW] = dummy_irq_entry(),
	vupdate_no_lock_int_entry(0),
	vupdate_no_lock_int_entry(1),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	vblank_int_entry(0),
	vblank_int_entry(1),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	vline0_int_entry(0),
	vline0_int_entry(1),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
	dummy_irq_entry(),
};

static const struct irq_service_funcs irq_service_funcs_dcn201 = {
		.to_dal_irq_source = to_dal_irq_source_dcn201
};

static void dcn201_irq_construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data)
{
	dal_irq_service_construct(irq_service, init_data);

	irq_service->info = irq_source_info_dcn201;
	irq_service->funcs = &irq_service_funcs_dcn201;
}

struct irq_service *dal_irq_service_dcn201_create(
	struct irq_service_init_data *init_data)
{
	struct irq_service *irq_service = kzalloc(sizeof(*irq_service),
						  GFP_KERNEL);

	if (!irq_service)
		return NULL;

	dcn201_irq_construct(irq_service, init_data);
	return irq_service;
}
