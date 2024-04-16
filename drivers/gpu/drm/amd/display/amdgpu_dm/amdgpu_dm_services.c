/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include <linux/string.h>
#include <linux/acpi.h>

#include <drm/drm_probe_helper.h>
#include <drm/amdgpu_drm.h>
#include "dm_services.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_pm.h"
#include "amdgpu_dm_trace.h"

	unsigned long long
	dm_get_elapse_time_in_ns(struct dc_context *ctx,
				 unsigned long long current_time_stamp,
				 unsigned long long last_time_stamp)
{
	return current_time_stamp - last_time_stamp;
}

void dm_perf_trace_timestamp(const char *func_name, unsigned int line, struct dc_context *ctx)
{
	trace_amdgpu_dc_performance(ctx->perf_trace->read_count,
				    ctx->perf_trace->write_count,
				    &ctx->perf_trace->last_entry_read,
				    &ctx->perf_trace->last_entry_write,
				    func_name, line);
}

/**** power component interfaces ****/
