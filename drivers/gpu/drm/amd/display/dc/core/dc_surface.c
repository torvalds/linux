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

/* DC interface (public) */
#include "dm_services.h"
#include "dc.h"

/* DC core (private) */
#include "core_dc.h"
#include "transform.h"

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static bool construct(struct dc_context *ctx, struct dc_plane_state *plane_state)
{
	plane_state->ctx = ctx;
	memset(&plane_state->hdr_static_ctx,
			0, sizeof(struct dc_hdr_static_metadata));
	return true;
}

static void destruct(struct dc_plane_state *plane_state)
{
	if (plane_state->gamma_correction != NULL) {
		dc_gamma_release(&plane_state->gamma_correction);
	}
	if (plane_state->in_transfer_func != NULL) {
		dc_transfer_func_release(
				plane_state->in_transfer_func);
		plane_state->in_transfer_func = NULL;
	}
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
void enable_surface_flip_reporting(struct dc_plane_state *plane_state,
		uint32_t controller_id)
{
	plane_state->irq_source = controller_id + DC_IRQ_SOURCE_PFLIP1 - 1;
	/*register_flip_interrupt(surface);*/
}

struct dc_plane_state *dc_create_plane_state(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct dc_plane_state *plane_state = dm_alloc(sizeof(*plane_state));

	if (NULL == plane_state)
		goto alloc_fail;

	if (false == construct(core_dc->ctx, plane_state))
		goto construct_fail;

	atomic_inc(&plane_state->ref_count);

	return plane_state;

construct_fail:
	dm_free(plane_state);

alloc_fail:
	return NULL;
}

const struct dc_plane_status *dc_plane_get_status(
		const struct dc_plane_state *plane_state)
{
	const struct dc_plane_status *plane_status;
	struct core_dc *core_dc;
	int i;

	if (!plane_state ||
		!plane_state->ctx ||
		!plane_state->ctx->dc) {
		ASSERT(0);
		return NULL; /* remove this if above assert never hit */
	}

	plane_status = &plane_state->status;
	core_dc = DC_TO_CORE(plane_state->ctx->dc);

	if (core_dc->current_context == NULL)
		return NULL;

	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
				&core_dc->current_context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state != plane_state)
			continue;

		core_dc->hwss.update_pending_status(pipe_ctx);
	}

	return plane_status;
}

void dc_plane_state_retain(struct dc_plane_state *plane_state)
{
	ASSERT(atomic_read(&plane_state->ref_count) > 0);
	atomic_inc(&plane_state->ref_count);
}

void dc_plane_state_release(struct dc_plane_state *plane_state)
{
	ASSERT(atomic_read(&plane_state->ref_count) > 0);
	atomic_dec(&plane_state->ref_count);

	if (atomic_read(&plane_state->ref_count) == 0) {
		destruct(plane_state);
		dm_free(plane_state);
	}
}

void dc_gamma_retain(struct dc_gamma *gamma)
{
	ASSERT(atomic_read(&gamma->ref_count) > 0);
	atomic_inc(&gamma->ref_count);
}

void dc_gamma_release(struct dc_gamma **gamma)
{
	ASSERT(atomic_read(&(*gamma)->ref_count) > 0);
	atomic_dec(&(*gamma)->ref_count);

	if (atomic_read(&(*gamma)->ref_count) == 0)
		dm_free((*gamma));

	*gamma = NULL;
}

struct dc_gamma *dc_create_gamma()
{
	struct dc_gamma *gamma = dm_alloc(sizeof(*gamma));

	if (gamma == NULL)
		goto alloc_fail;

	atomic_inc(&gamma->ref_count);

	return gamma;

alloc_fail:
	return NULL;
}

void dc_transfer_func_retain(struct dc_transfer_func *tf)
{
	ASSERT(atomic_read(&tf->ref_count) > 0);
	atomic_inc(&tf->ref_count);
}

void dc_transfer_func_release(struct dc_transfer_func *tf)
{
	ASSERT(atomic_read(&tf->ref_count) > 0);
	atomic_dec(&tf->ref_count);

	if (atomic_read(&tf->ref_count) == 0)
		dm_free(tf);
}

struct dc_transfer_func *dc_create_transfer_func()
{
	struct dc_transfer_func *tf = dm_alloc(sizeof(*tf));

	if (tf == NULL)
		goto alloc_fail;

	atomic_inc(&tf->ref_count);

	return tf;

alloc_fail:
	return NULL;
}


