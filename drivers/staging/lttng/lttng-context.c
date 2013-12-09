/*
 * lttng-context.c
 *
 * LTTng trace/channel/event context management.
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "lttng-events.h"
#include "lttng-tracer.h"

int lttng_find_context(struct lttng_ctx *ctx, const char *name)
{
	unsigned int i;

	for (i = 0; i < ctx->nr_fields; i++) {
		/* Skip allocated (but non-initialized) contexts */
		if (!ctx->fields[i].event_field.name)
			continue;
		if (!strcmp(ctx->fields[i].event_field.name, name))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_find_context);

/*
 * Note: as we append context information, the pointer location may change.
 */
struct lttng_ctx_field *lttng_append_context(struct lttng_ctx **ctx_p)
{
	struct lttng_ctx_field *field;
	struct lttng_ctx *ctx;

	if (!*ctx_p) {
		*ctx_p = kzalloc(sizeof(struct lttng_ctx), GFP_KERNEL);
		if (!*ctx_p)
			return NULL;
	}
	ctx = *ctx_p;
	if (ctx->nr_fields + 1 > ctx->allocated_fields) {
		struct lttng_ctx_field *new_fields;

		ctx->allocated_fields = max_t(size_t, 1, 2 * ctx->allocated_fields);
		new_fields = kzalloc(ctx->allocated_fields * sizeof(struct lttng_ctx_field), GFP_KERNEL);
		if (!new_fields)
			return NULL;
		if (ctx->fields)
			memcpy(new_fields, ctx->fields, sizeof(*ctx->fields) * ctx->nr_fields);
		kfree(ctx->fields);
		ctx->fields = new_fields;
	}
	field = &ctx->fields[ctx->nr_fields];
	ctx->nr_fields++;
	return field;
}
EXPORT_SYMBOL_GPL(lttng_append_context);

/*
 * Remove last context field.
 */
void lttng_remove_context_field(struct lttng_ctx **ctx_p,
				struct lttng_ctx_field *field)
{
	struct lttng_ctx *ctx;

	ctx = *ctx_p;
	ctx->nr_fields--;
	WARN_ON_ONCE(&ctx->fields[ctx->nr_fields] != field);
	memset(&ctx->fields[ctx->nr_fields], 0, sizeof(struct lttng_ctx_field));
}
EXPORT_SYMBOL_GPL(lttng_remove_context_field);

void lttng_destroy_context(struct lttng_ctx *ctx)
{
	int i;

	if (!ctx)
		return;
	for (i = 0; i < ctx->nr_fields; i++) {
		if (ctx->fields[i].destroy)
			ctx->fields[i].destroy(&ctx->fields[i]);
	}
	kfree(ctx->fields);
	kfree(ctx);
}
