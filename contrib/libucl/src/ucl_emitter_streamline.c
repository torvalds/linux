/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ucl.h"
#include "ucl_internal.h"
#include "ucl_chartable.h"

struct ucl_emitter_streamline_stack {
	bool is_array;
	bool empty;
	const ucl_object_t *obj;
	struct ucl_emitter_streamline_stack *next;
};

struct ucl_emitter_context_streamline {
	/* Inherited from the main context */
	/** Name of emitter (e.g. json, compact_json) */
	const char *name;
	/** Unique id (e.g. UCL_EMIT_JSON for standard emitters */
	int id;
	/** A set of output functions */
	const struct ucl_emitter_functions *func;
	/** A set of output operations */
	const struct ucl_emitter_operations *ops;
	/** Current amount of indent tabs */
	unsigned int indent;
	/** Top level object */
	const ucl_object_t *top;
	/** Optional comments */
	const ucl_object_t *comments;

	/* Streamline specific fields */
	struct ucl_emitter_streamline_stack *containers;
};

#define TO_STREAMLINE(ctx) (struct ucl_emitter_context_streamline *)(ctx)

struct ucl_emitter_context*
ucl_object_emit_streamline_new (const ucl_object_t *obj,
		enum ucl_emitter emit_type,
		struct ucl_emitter_functions *emitter)
{
	const struct ucl_emitter_context *ctx;
	struct ucl_emitter_context_streamline *sctx;

	ctx = ucl_emit_get_standard_context (emit_type);
	if (ctx == NULL) {
		return NULL;
	}

	sctx = calloc (1, sizeof (*sctx));
	if (sctx == NULL) {
		return NULL;
	}

	memcpy (sctx, ctx, sizeof (*ctx));
	sctx->func = emitter;
	sctx->top = obj;

	ucl_object_emit_streamline_start_container ((struct ucl_emitter_context *)sctx,
			obj);

	return (struct ucl_emitter_context *)sctx;
}

void
ucl_object_emit_streamline_start_container (struct ucl_emitter_context *ctx,
		const ucl_object_t *obj)
{
	struct ucl_emitter_context_streamline *sctx = TO_STREAMLINE(ctx);
	struct ucl_emitter_streamline_stack *st, *top;
	bool print_key = false;

	/* Check top object presence */
	if (sctx->top == NULL) {
		sctx->top = obj;
	}

	top = sctx->containers;
	st = malloc (sizeof (*st));
	if (st != NULL) {
		if (top != NULL && !top->is_array) {
			print_key = true;
		}
		st->empty = true;
		st->obj = obj;
		if (obj != NULL && obj->type == UCL_ARRAY) {
			st->is_array = true;
			sctx->ops->ucl_emitter_start_array (ctx, obj, print_key);
		}
		else {
			st->is_array = false;
			sctx->ops->ucl_emitter_start_object (ctx, obj, print_key);
		}
		LL_PREPEND (sctx->containers, st);
	}
}

void
ucl_object_emit_streamline_add_object (
		struct ucl_emitter_context *ctx, const ucl_object_t *obj)
{
	struct ucl_emitter_context_streamline *sctx = TO_STREAMLINE(ctx);
	bool is_array = false, is_first = false;

	if (sctx->containers != NULL) {
		if (sctx->containers->is_array) {
			is_array = true;
		}
		if (sctx->containers->empty) {
			is_first = true;
			sctx->containers->empty = false;
		}
	}

	sctx->ops->ucl_emitter_write_elt (ctx, obj, is_first, !is_array);
}

void
ucl_object_emit_streamline_end_container (struct ucl_emitter_context *ctx)
{
	struct ucl_emitter_context_streamline *sctx = TO_STREAMLINE(ctx);
	struct ucl_emitter_streamline_stack *st;

	if (sctx->containers != NULL) {
		st = sctx->containers;

		if (st->is_array) {
			sctx->ops->ucl_emitter_end_array (ctx, st->obj);
		}
		else {
			sctx->ops->ucl_emitter_end_object (ctx, st->obj);
		}
		sctx->containers = st->next;
		free (st);
	}
}

void
ucl_object_emit_streamline_finish (struct ucl_emitter_context *ctx)
{
	struct ucl_emitter_context_streamline *sctx = TO_STREAMLINE(ctx);

	while (sctx->containers != NULL) {
		ucl_object_emit_streamline_end_container (ctx);
	}

	free (sctx);
}
