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
#include "core_types.h"
#include "transform.h"
#include "dpp.h"

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void construct(struct dc_context *ctx, struct dc_plane_state *plane_state)
{
	plane_state->ctx = ctx;
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

struct dc_plane_state *dc_create_plane_state(struct dc *dc)
{
	struct dc *core_dc = dc;

	struct dc_plane_state *plane_state = kvzalloc(sizeof(*plane_state),
						      GFP_KERNEL);

	if (NULL == plane_state)
		return NULL;

	kref_init(&plane_state->refcount);
	construct(core_dc->ctx, plane_state);

	return plane_state;
}

const struct dc_plane_status *dc_plane_get_status(
		const struct dc_plane_state *plane_state)
{
	const struct dc_plane_status *plane_status;
	struct dc  *core_dc;
	int i;

	if (!plane_state ||
		!plane_state->ctx ||
		!plane_state->ctx->dc) {
		ASSERT(0);
		return NULL; /* remove this if above assert never hit */
	}

	plane_status = &plane_state->status;
	core_dc = plane_state->ctx->dc;

	if (core_dc->current_state == NULL)
		return NULL;

	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
				&core_dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state != plane_state)
			continue;

		core_dc->hwss.update_pending_status(pipe_ctx);
	}

	return plane_status;
}

void dc_plane_state_retain(struct dc_plane_state *plane_state)
{
	kref_get(&plane_state->refcount);
}

static void dc_plane_state_free(struct kref *kref)
{
	struct dc_plane_state *plane_state = container_of(kref, struct dc_plane_state, refcount);
	destruct(plane_state);
	kvfree(plane_state);
}

void dc_plane_state_release(struct dc_plane_state *plane_state)
{
	kref_put(&plane_state->refcount, dc_plane_state_free);
}

void dc_gamma_retain(struct dc_gamma *gamma)
{
	kref_get(&gamma->refcount);
}

static void dc_gamma_free(struct kref *kref)
{
	struct dc_gamma *gamma = container_of(kref, struct dc_gamma, refcount);
	kvfree(gamma);
}

void dc_gamma_release(struct dc_gamma **gamma)
{
	kref_put(&(*gamma)->refcount, dc_gamma_free);
	*gamma = NULL;
}

struct dc_gamma *dc_create_gamma(void)
{
	struct dc_gamma *gamma = kvzalloc(sizeof(*gamma), GFP_KERNEL);

	if (gamma == NULL)
		goto alloc_fail;

	kref_init(&gamma->refcount);
	return gamma;

alloc_fail:
	return NULL;
}

void dc_transfer_func_retain(struct dc_transfer_func *tf)
{
	kref_get(&tf->refcount);
}

static void dc_transfer_func_free(struct kref *kref)
{
	struct dc_transfer_func *tf = container_of(kref, struct dc_transfer_func, refcount);
	kvfree(tf);
}

void dc_transfer_func_release(struct dc_transfer_func *tf)
{
	kref_put(&tf->refcount, dc_transfer_func_free);
}

struct dc_transfer_func *dc_create_transfer_func(void)
{
	struct dc_transfer_func *tf = kvzalloc(sizeof(*tf), GFP_KERNEL);

	if (tf == NULL)
		goto alloc_fail;

	kref_init(&tf->refcount);

	return tf;

alloc_fail:
	return NULL;
}


