/*
 * Isochronous IO functionality
 *
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

int
fw_iso_buffer_init(struct fw_iso_buffer *buffer, struct fw_card *card,
		   int page_count, enum dma_data_direction direction)
{
	int i, j, retval = -ENOMEM;
	dma_addr_t address;

	buffer->page_count = page_count;
	buffer->direction = direction;

	buffer->pages = kmalloc(page_count * sizeof(buffer->pages[0]),
				GFP_KERNEL);
	if (buffer->pages == NULL)
		goto out;

	for (i = 0; i < buffer->page_count; i++) {
		buffer->pages[i] = alloc_page(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO);
		if (buffer->pages[i] == NULL)
			goto out_pages;

		address = dma_map_page(card->device, buffer->pages[i],
				       0, PAGE_SIZE, direction);
		if (dma_mapping_error(address)) {
			__free_page(buffer->pages[i]);
			goto out_pages;
		}
		set_page_private(buffer->pages[i], address);
	}

	return 0;

 out_pages:
	for (j = 0; j < i; j++) {
		address = page_private(buffer->pages[j]);
		dma_unmap_page(card->device, address,
			       PAGE_SIZE, DMA_TO_DEVICE);
		__free_page(buffer->pages[j]);
	}
	kfree(buffer->pages);
 out:
	buffer->pages = NULL;
	return retval;
}

int fw_iso_buffer_map(struct fw_iso_buffer *buffer, struct vm_area_struct *vma)
{
	unsigned long uaddr;
	int i, retval;

	uaddr = vma->vm_start;
	for (i = 0; i < buffer->page_count; i++) {
		retval = vm_insert_page(vma, uaddr, buffer->pages[i]);
		if (retval)
			return retval;
		uaddr += PAGE_SIZE;
	}

	return 0;
}

void fw_iso_buffer_destroy(struct fw_iso_buffer *buffer,
			   struct fw_card *card)
{
	int i;
	dma_addr_t address;

	for (i = 0; i < buffer->page_count; i++) {
		address = page_private(buffer->pages[i]);
		dma_unmap_page(card->device, address,
			       PAGE_SIZE, DMA_TO_DEVICE);
		__free_page(buffer->pages[i]);
	}

	kfree(buffer->pages);
	buffer->pages = NULL;
}

struct fw_iso_context *
fw_iso_context_create(struct fw_card *card, int type,
		      int channel, int speed, size_t header_size,
		      fw_iso_callback_t callback, void *callback_data)
{
	struct fw_iso_context *ctx;

	ctx = card->driver->allocate_iso_context(card, type, header_size);
	if (IS_ERR(ctx))
		return ctx;

	ctx->card = card;
	ctx->type = type;
	ctx->channel = channel;
	ctx->speed = speed;
	ctx->header_size = header_size;
	ctx->callback = callback;
	ctx->callback_data = callback_data;

	return ctx;
}

void fw_iso_context_destroy(struct fw_iso_context *ctx)
{
	struct fw_card *card = ctx->card;

	card->driver->free_iso_context(ctx);
}

int
fw_iso_context_start(struct fw_iso_context *ctx, int cycle, int sync, int tags)
{
	return ctx->card->driver->start_iso(ctx, cycle, sync, tags);
}

int
fw_iso_context_queue(struct fw_iso_context *ctx,
		     struct fw_iso_packet *packet,
		     struct fw_iso_buffer *buffer,
		     unsigned long payload)
{
	struct fw_card *card = ctx->card;

	return card->driver->queue_iso(ctx, packet, buffer, payload);
}

int
fw_iso_context_stop(struct fw_iso_context *ctx)
{
	return ctx->card->driver->stop_iso(ctx);
}
