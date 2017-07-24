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
static bool construct(struct dc_context *ctx, struct dc_surface *surface)
{
	surface->ctx = ctx;
	memset(&surface->hdr_static_ctx,
			0, sizeof(struct dc_hdr_static_metadata));
	return true;
}

static void destruct(struct dc_surface *surface)
{
	if (surface->gamma_correction != NULL) {
		dc_gamma_release(&surface->gamma_correction);
	}
	if (surface->in_transfer_func != NULL) {
		dc_transfer_func_release(
				surface->in_transfer_func);
		surface->in_transfer_func = NULL;
	}
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
void enable_surface_flip_reporting(struct dc_surface *surface,
		uint32_t controller_id)
{
	surface->irq_source = controller_id + DC_IRQ_SOURCE_PFLIP1 - 1;
	/*register_flip_interrupt(surface);*/
}

struct dc_surface *dc_create_surface(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct dc_surface *surface = dm_alloc(sizeof(*surface));

	if (NULL == surface)
		goto alloc_fail;

	if (false == construct(core_dc->ctx, surface))
		goto construct_fail;

	++surface->ref_count;

	return surface;

construct_fail:
	dm_free(surface);

alloc_fail:
	return NULL;
}

const struct dc_surface_status *dc_surface_get_status(
		const struct dc_surface *dc_surface)
{
	const struct dc_surface_status *surface_status;
	struct core_dc *core_dc;
	int i;

	if (!dc_surface ||
		!dc_surface->ctx ||
		!dc_surface->ctx->dc) {
		ASSERT(0);
		return NULL; /* remove this if above assert never hit */
	}

	surface_status = &dc_surface->status;
	core_dc = DC_TO_CORE(dc_surface->ctx->dc);

	if (core_dc->current_context == NULL)
		return NULL;

	for (i = 0; i < core_dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx =
				&core_dc->current_context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->surface != dc_surface)
			continue;

		core_dc->hwss.update_pending_status(pipe_ctx);
	}

	return surface_status;
}

void dc_surface_retain(struct dc_surface *surface)
{
	ASSERT(surface->ref_count > 0);
	++surface->ref_count;
}

void dc_surface_release(struct dc_surface *surface)
{
	ASSERT(surface->ref_count > 0);
	--surface->ref_count;

	if (surface->ref_count == 0) {
		destruct(surface);
		dm_free(surface);
	}
}

void dc_gamma_retain(struct dc_gamma *gamma)
{
	ASSERT(gamma->ref_count > 0);
	++gamma->ref_count;
}

void dc_gamma_release(struct dc_gamma **gamma)
{
	ASSERT((*gamma)->ref_count > 0);
	--(*gamma)->ref_count;

	if ((*gamma)->ref_count == 0)
		dm_free((*gamma));

	*gamma = NULL;
}

struct dc_gamma *dc_create_gamma()
{
	struct dc_gamma *gamma = dm_alloc(sizeof(*gamma));

	if (gamma == NULL)
		goto alloc_fail;

	++gamma->ref_count;

	return gamma;

alloc_fail:
	return NULL;
}

void dc_transfer_func_retain(struct dc_transfer_func *tf)
{
	ASSERT(tf->ref_count > 0);
	++tf->ref_count;
}

void dc_transfer_func_release(struct dc_transfer_func *tf)
{
	ASSERT(tf->ref_count > 0);
	--tf->ref_count;

	if (tf->ref_count == 0)
		dm_free(tf);
}

struct dc_transfer_func *dc_create_transfer_func()
{
	struct dc_transfer_func *tf = dm_alloc(sizeof(*tf));

	if (tf == NULL)
		goto alloc_fail;

	++tf->ref_count;

	return tf;

alloc_fail:
	return NULL;
}


