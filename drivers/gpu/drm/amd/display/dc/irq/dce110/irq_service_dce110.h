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

#ifndef __DAL_IRQ_SERVICE_DCE110_H__
#define __DAL_IRQ_SERVICE_DCE110_H__

#include "../irq_service.h"

struct irq_service *dal_irq_service_dce110_create(
	struct irq_service_init_data *init_data);

enum dc_irq_source to_dal_irq_source_dce110(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id);

bool dal_irq_service_dummy_set(
	struct irq_service *irq_service,
	const struct irq_source_info *info,
	bool enable);

bool dal_irq_service_dummy_ack(
	struct irq_service *irq_service,
	const struct irq_source_info *info);

bool dce110_vblank_set(
	struct irq_service *irq_service,
	const struct irq_source_info *info,
	bool enable);

#endif
