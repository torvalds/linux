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

#include "include/irq_service_interface.h"
#include "include/logger_interface.h"

#include "dce110/irq_service_dce110.h"

#if defined(CONFIG_DRM_AMD_DC_SI)
#include "dce60/irq_service_dce60.h"
#endif

#include "dce80/irq_service_dce80.h"
#include "dce120/irq_service_dce120.h"
#include "dcn10/irq_service_dcn10.h"

#include "reg_helper.h"
#include "irq_service.h"

//HPD0_DC_HPD_INT_STATUS
#define HPD0_DC_HPD_INT_STATUS__DC_HPD_SENSE_DELAYED_MASK		0x00000010L
#define HPD0_DC_HPD_INT_CONTROL__DC_HPD_INT_POLARITY_MASK		0x00000100L
#define HPD0_DC_HPD_INT_STATUS__DC_HPD_SENSE_DELAYED__SHIFT		0x4
#define HPD0_DC_HPD_INT_CONTROL__DC_HPD_INT_POLARITY__SHIFT     0x8
//HPD1_DC_HPD_INT_STATUS
#define DC_HPD1_INT_STATUS__DC_HPD1_SENSE_DELAYED_MASK			0x10
#define DC_HPD1_INT_STATUS__DC_HPD1_SENSE_DELAYED__SHIFT		0x4
#define DC_HPD1_INT_CONTROL__DC_HPD1_INT_POLARITY_MASK			0x100
#define DC_HPD1_INT_CONTROL__DC_HPD1_INT_POLARITY__SHIFT		0x8


#define CTX \
		irq_service->ctx
#define DC_LOGGER \
	irq_service->ctx->logger

void dal_irq_service_construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data)
{
	if (!init_data || !init_data->ctx) {
		BREAK_TO_DEBUGGER();
		return;
	}

	irq_service->ctx = init_data->ctx;
}

void dal_irq_service_destroy(struct irq_service **irq_service)
{
	if (!irq_service || !*irq_service) {
		BREAK_TO_DEBUGGER();
		return;
	}

	kfree(*irq_service);

	*irq_service = NULL;
}

static const struct irq_source_info *find_irq_source_info(
	struct irq_service *irq_service,
	enum dc_irq_source source)
{
	if (source >= DAL_IRQ_SOURCES_NUMBER)
		return NULL;

	return &irq_service->info[source];
}

void dal_irq_service_set_generic(
	struct irq_service *irq_service,
	const struct irq_source_info *info,
	bool enable)
{
	uint32_t addr = info->enable_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);

	value = (value & ~info->enable_mask) |
		(info->enable_value[enable ? 0 : 1] & info->enable_mask);
	dm_write_reg(irq_service->ctx, addr, value);
}

bool dal_irq_service_set(
	struct irq_service *irq_service,
	enum dc_irq_source source,
	bool enable)
{
	const struct irq_source_info *info =
		find_irq_source_info(irq_service, source);

	if (!info) {
		DC_LOG_ERROR("%s: cannot find irq info table entry for %d\n",
			__func__,
			source);
		return false;
	}

	dal_irq_service_ack(irq_service, source);

	if (info->funcs && info->funcs->set) {
		if (info->funcs->set == dal_irq_service_dummy_set) {
			DC_LOG_WARNING("%s: src: %d, st: %d\n", __func__,
				       source, enable);
			ASSERT(0);
		}

		return info->funcs->set(irq_service, info, enable);
	}

	dal_irq_service_set_generic(irq_service, info, enable);

	return true;
}

void dal_irq_service_ack_generic(
	struct irq_service *irq_service,
	const struct irq_source_info *info)
{
	uint32_t addr = info->ack_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);

	value = (value & ~info->ack_mask) |
		(info->ack_value & info->ack_mask);
	dm_write_reg(irq_service->ctx, addr, value);
}

bool dal_irq_service_ack(
	struct irq_service *irq_service,
	enum dc_irq_source source)
{
	const struct irq_source_info *info =
		find_irq_source_info(irq_service, source);

	if (!info) {
		DC_LOG_ERROR("%s: cannot find irq info table entry for %d\n",
			__func__,
			source);
		return false;
	}

	if (info->funcs && info->funcs->ack) {
		if (info->funcs->ack == dal_irq_service_dummy_ack) {
			DC_LOG_WARNING("%s: src: %d\n", __func__, source);
			ASSERT(0);
		}

		return info->funcs->ack(irq_service, info);
	}

	dal_irq_service_ack_generic(irq_service, info);

	return true;
}

enum dc_irq_source dal_irq_service_to_irq_source(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id)
{
	return irq_service->funcs->to_dal_irq_source(
		irq_service,
		src_id,
		ext_id);
}

bool hpd0_ack(
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

bool hpd1_ack(
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
