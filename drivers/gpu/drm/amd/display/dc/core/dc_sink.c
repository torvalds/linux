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

#include <linux/slab.h>

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"

/*******************************************************************************
 * Private functions
 ******************************************************************************/

static void destruct(struct dc_sink *sink)
{
	if (sink->dc_container_id) {
		kfree(sink->dc_container_id);
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
	sink->sink_id = init_params->link->ctx->dc_sink_id_count;
	// increment dc_sink_id_count because we don't want two sinks with same ID
	// unless they are actually the same
	init_params->link->ctx->dc_sink_id_count++;

	return true;
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/

void dc_sink_retain(struct dc_sink *sink)
{
	kref_get(&sink->refcount);
}

static void dc_sink_free(struct kref *kref)
{
	struct dc_sink *sink = container_of(kref, struct dc_sink, refcount);
	destruct(sink);
	kfree(sink);
}

void dc_sink_release(struct dc_sink *sink)
{
	kref_put(&sink->refcount, dc_sink_free);
}

struct dc_sink *dc_sink_create(const struct dc_sink_init_data *init_params)
{
	struct dc_sink *sink = kzalloc(sizeof(*sink), GFP_KERNEL);

	if (NULL == sink)
		goto alloc_fail;

	if (false == construct(sink, init_params))
		goto construct_fail;

	kref_init(&sink->refcount);

	return sink;

construct_fail:
	kfree(sink);

alloc_fail:
	return NULL;
}

/*******************************************************************************
 * Protected functions - visible only inside of DC (not visible in DM)
 ******************************************************************************/
