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
#include "dm_helpers.h"
#include "core_types.h"

/*******************************************************************************
 * Private functions
 ******************************************************************************/

static void destruct(struct dc_sink *sink)
{
	if (sink->dc_container_id) {
		dm_free(sink->dc_container_id);
		sink->dc_container_id = NULL;
	}
}

static bool construct(struct dc_sink *sink, const struct dc_sink_init_data *init_params)
{

	struct dc_link *link = init_params->link;

	if (!link)
		return false;

	sink->sink_signal = init_params->sink_signal;
	sink->link = link;
	sink->ctx = link->ctx;
	sink->dongle_max_pix_clk = init_params->dongle_max_pix_clk;
	sink->converter_disable_audio = init_params->converter_disable_audio;
	sink->dc_container_id = NULL;

	return true;
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

void dc_sink_retain(struct dc_sink *sink)
{
	ASSERT(atomic_read(&sink->ref_count) > 0);
	atomic_inc(&sink->ref_count);
}

void dc_sink_release(struct dc_sink *sink)
{
	ASSERT(atomic_read(&sink->ref_count) > 0);
	atomic_dec(&sink->ref_count);

	if (atomic_read(&sink->ref_count) == 0) {
		destruct(sink);
		dm_free(sink);
	}
}

struct dc_sink *dc_sink_create(const struct dc_sink_init_data *init_params)
{
	struct dc_sink *sink = dm_alloc(sizeof(*sink));

	if (NULL == sink)
		goto alloc_fail;

	if (false == construct(sink, init_params))
		goto construct_fail;

	atomic_inc(&sink->ref_count);

	return sink;

construct_fail:
	dm_free(sink);

alloc_fail:
	return NULL;
}

bool dc_sink_get_container_id(struct dc_sink *dc_sink, struct dc_container_id *container_id)
{
	if (dc_sink && container_id && dc_sink->dc_container_id) {
		memmove(&container_id->guid, &dc_sink->dc_container_id->guid,
			sizeof(container_id->guid));
		memmove(&container_id->portId, &dc_sink->dc_container_id->portId,
			sizeof(container_id->portId));
		container_id->manufacturerName = dc_sink->dc_container_id->manufacturerName;
		container_id->productCode = dc_sink->dc_container_id->productCode;
		return true;
	}
	return false;
}

bool dc_sink_set_container_id(struct dc_sink *dc_sink, const struct dc_container_id *container_id)
{
	if (dc_sink && container_id) {
		if (!dc_sink->dc_container_id)
			dc_sink->dc_container_id = dm_alloc(sizeof(*dc_sink->dc_container_id));

		if (dc_sink->dc_container_id) {
			memmove(&dc_sink->dc_container_id->guid, &container_id->guid,
				sizeof(container_id->guid));
			memmove(&dc_sink->dc_container_id->portId, &container_id->portId,
				sizeof(container_id->portId));
			dc_sink->dc_container_id->manufacturerName = container_id->manufacturerName;
			dc_sink->dc_container_id->productCode = container_id->productCode;
			return true;
		}
	}
	return false;
}

/*******************************************************************************
 * Protected functions - visible only inside of DC (not visible in DM)
 ******************************************************************************/
