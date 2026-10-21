/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
 */

#ifndef __RAS_NBIO_H__
#define __RAS_NBIO_H__
#include "ras.h"

struct ras_core_context;

struct ras_nbio_ip_func {
	int (*handle_ras_controller_intr_no_bifring)(struct ras_core_context *ras_core);
	int (*handle_ras_err_event_athub_intr_no_bifring)(struct ras_core_context *ras_core);
	uint32_t (*get_memory_partition_mode)(struct ras_core_context *ras_core);
};

struct ras_nbio {
	uint32_t nbio_ip_version;
	const struct ras_nbio_ip_func *ip_func;
	const struct ras_nbio_sys_func *sys_func;
};

int ras_nbio_hw_init(struct ras_core_context *ras_core);
int ras_nbio_hw_fini(struct ras_core_context *ras_core);
bool ras_nbio_handle_irq_error(struct ras_core_context *ras_core, void *data);
#endif
