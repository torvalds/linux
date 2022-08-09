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

#ifndef __DAL_IRQ_SERVICE_H__
#define __DAL_IRQ_SERVICE_H__

#include "include/irq_service_interface.h"

#include "irq_types.h"

struct irq_service;
struct irq_source_info;

struct irq_source_info_funcs {
	bool (*set)(
		struct irq_service *irq_service,
		const struct irq_source_info *info,
		bool enable);
	bool (*ack)(
		struct irq_service *irq_service,
		const struct irq_source_info *info);
};

struct irq_source_info {
	uint32_t src_id;
	uint32_t ext_id;
	uint32_t enable_reg;
	uint32_t enable_mask;
	uint32_t enable_value[2];
	uint32_t ack_reg;
	uint32_t ack_mask;
	uint32_t ack_value;
	uint32_t status_reg;
	const struct irq_source_info_funcs *funcs;
};

struct irq_service_funcs {
	enum dc_irq_source (*to_dal_irq_source)(
			struct irq_service *irq_service,
			uint32_t src_id,
			uint32_t ext_id);
};

struct irq_service {
	struct dc_context *ctx;
	const struct irq_source_info *info;
	const struct irq_service_funcs *funcs;
};

const struct irq_source_info *find_irq_source_info(
	struct irq_service *irq_service,
	enum dc_irq_source source);

void dal_irq_service_construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data);

void dal_irq_service_ack_generic(
	struct irq_service *irq_service,
	const struct irq_source_info *info);

void dal_irq_service_set_generic(
	struct irq_service *irq_service,
	const struct irq_source_info *info,
	bool enable);

#endif
