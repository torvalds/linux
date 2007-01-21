/*						-*- c-basic-offset: 8 -*-
 *
 * fw-iso.c - Isochronous IO
 * Copyright (C) 2006 Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include "fw-transaction.h"
#include "fw-topology.h"
#include "fw-device.h"

static int
setup_iso_buffer(struct fw_iso_context *ctx, size_t size,
		 enum dma_data_direction direction)
{
	struct page *page;
	int i;
	void *p;

	ctx->buffer_size = PAGE_ALIGN(size);
	if (size == 0)
		return 0;

	ctx->buffer = vmalloc_32_user(ctx->buffer_size);
	if (ctx->buffer == NULL)
		return -ENOMEM;

	ctx->page_count = ctx->buffer_size >> PAGE_SHIFT;
	ctx->pages =
	    kzalloc(ctx->page_count * sizeof(ctx->pages[0]), GFP_KERNEL);
	if (ctx->pages == NULL) {
		vfree(ctx->buffer);
		return -ENOMEM;
	}

	p = ctx->buffer;
	for (i = 0; i < ctx->page_count; i++, p += PAGE_SIZE) {
		page = vmalloc_to_page(p);
		ctx->pages[i] = dma_map_page(ctx->card->device,
					     page, 0, PAGE_SIZE, direction);
	}

	return 0;
}

static void destroy_iso_buffer(struct fw_iso_context *ctx)
{
	int i;

	for (i = 0; i < ctx->page_count; i++)
		dma_unmap_page(ctx->card->device, ctx->pages[i],
			       PAGE_SIZE, DMA_TO_DEVICE);

	kfree(ctx->pages);
	vfree(ctx->buffer);
}

struct fw_iso_context *fw_iso_context_create(struct fw_card *card, int type,
					     size_t buffer_size,
					     fw_iso_callback_t callback,
					     void *callback_data)
{
	struct fw_iso_context *ctx;
	int retval;

	ctx = card->driver->allocate_iso_context(card, type);
	if (IS_ERR(ctx))
		return ctx;

	ctx->card = card;
	ctx->type = type;
	ctx->callback = callback;
	ctx->callback_data = callback_data;

	retval = setup_iso_buffer(ctx, buffer_size, DMA_TO_DEVICE);
	if (retval < 0) {
		card->driver->free_iso_context(ctx);
		return ERR_PTR(retval);
	}

	return ctx;
}
EXPORT_SYMBOL(fw_iso_context_create);

void fw_iso_context_destroy(struct fw_iso_context *ctx)
{
	struct fw_card *card = ctx->card;

	destroy_iso_buffer(ctx);

	card->driver->free_iso_context(ctx);
}
EXPORT_SYMBOL(fw_iso_context_destroy);

int
fw_iso_context_send(struct fw_iso_context *ctx,
		    int channel, int speed, int cycle)
{
	ctx->channel = channel;
	ctx->speed = speed;

	return ctx->card->driver->send_iso(ctx, cycle);
}
EXPORT_SYMBOL(fw_iso_context_send);

int
fw_iso_context_queue(struct fw_iso_context *ctx,
		     struct fw_iso_packet *packet, void *payload)
{
	struct fw_card *card = ctx->card;

	return card->driver->queue_iso(ctx, packet, payload);
}
EXPORT_SYMBOL(fw_iso_context_queue);
