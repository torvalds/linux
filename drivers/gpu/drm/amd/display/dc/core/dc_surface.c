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
static void dc_plane_construct(struct dc_context *ctx, struct dc_plane_state *plane_state)
{
	plane_state->ctx = ctx;

	plane_state->gamma_correction = dc_create_gamma();
	if (plane_state->gamma_correction != NULL)
		plane_state->gamma_correction->is_identity = true;

	plane_state->in_transfer_func = dc_create_transfer_func();
	if (plane_state->in_transfer_func != NULL) {
		plane_state->in_transfer_func->type = TF_TYPE_BYPASS;
	}
	plane_state->in_shaper_func = dc_create_transfer_func();
	if (plane_state->in_shaper_func != NULL) {
		plane_state->in_shaper_func->type = TF_TYPE_BYPASS;
	}

	plane_state->lut3d_func = dc_create_3dlut_func();

	plane_state->blend_tf = dc_create_transfer_func();
	if (plane_state->blend_tf != NULL) {
		plane_state->blend_tf->type = TF_TYPE_BYPASS;
	}

	plane_state->pre_multiplied_alpha = true;

}

static void dc_plane_destruct(struct dc_plane_state *plane_state)
{
	if (plane_state->gamma_correction != NULL) {
		dc_gamma_release(&plane_state->gamma_correction);
	}
	if (plane_state->in_transfer_func != NULL) {
		dc_transfer_func_release(
				plane_state->in_transfer_func);
		plane_state->in_transfer_func = NULL;
	}
	if (plane_state->in_shaper_func != NULL) {
		dc_transfer_func_release(
				plane_state->in_shaper_func);
		plane_state->in_shaper_func = NULL;
	}
	if (plane_state->lut3d_func != NULL) {
		dc_3dlut_func_release(
				plane_state->lut3d_func);
		plane_state->lut3d_func = NULL;
	}
	if (plane_state->blend_tf != NULL) {
		dc_transfer_func_release(
				plane_state->blend_tf);
		plane_state->blend_tf = NULL;
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
	struct dc_plane_state *plane_state = kvzalloc(sizeof(*plane_state),
							GFP_KERNEL);

	if (NULL == plane_state)
		return NULL;

	kref_init(&plane_state->refcount);
	dc_plane_construct(dc->ctx, plane_state);

	return plane_state;
}

/*
 *****************************************************************************
 *  Function: dc_plane_get_status
 *
 *  @brief
 *     Looks up the pipe context of plane_state and updates the pending status
 *     of the pipe context. Then returns plane_state->status
 *
 *  @param [in] plane_state: pointer to the plane_state to get the status of
 *****************************************************************************
 */
const struct dc_plane_status *dc_plane_get_status(
		const struct dc_plane_state *plane_state)
{
	const struct dc_plane_status *plane_status;
	struct dc  *dc;
	int i;

	if (!plane_state ||
		!plane_state->ctx ||
		!plane_state->ctx->dc) {
		ASSERT(0);
		return NULL; /* remove this if above assert never hit */
	}

	plane_status = &plane_state->status;
	dc = plane_state->ctx->dc;

	if (dc->current_state == NULL)
		return NULL;

	/* Find the current plane state and set its pending bit to false */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
				&dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state != plane_state)
			continue;

		pipe_ctx->plane_state->status.is_flip_pending = false;

		break;
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
				&dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state != plane_state)
			continue;

		dc->hwss.update_pending_status(pipe_ctx);
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
	dc_plane_destruct(plane_state);
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

static void dc_3dlut_func_free(struct kref *kref)
{
	struct dc_3dlut *lut = container_of(kref, struct dc_3dlut, refcount);

	kvfree(lut);
}

struct dc_3dlut *dc_create_3dlut_func(void)
{
	struct dc_3dlut *lut = kvzalloc(sizeof(*lut), GFP_KERNEL);

	if (lut == NULL)
		goto alloc_fail;

	kref_init(&lut->refcount);
	lut->state.raw = 0;

	return lut;

alloc_fail:
	return NULL;

}

void dc_3dlut_func_release(struct dc_3dlut *lut)
{
	kref_put(&lut->refcount, dc_3dlut_func_free);
}

void dc_3dlut_func_retain(struct dc_3dlut *lut)
{
	kref_get(&lut->refcount);
}


