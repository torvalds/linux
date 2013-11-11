/*
 * Isochronous I/O functionality:
 *   - Isochronous DMA context management
 *   - Isochronous bus resource management (channels, bandwidth), client side
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

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/export.h>

#include <asm/byteorder.h>

#include "core.h"

/*
 * Isochronous DMA context management
 */

int fw_iso_buffer_alloc(struct fw_iso_buffer *buffer, int page_count)
{
	int i;

	buffer->page_count = 0;
	buffer->page_count_mapped = 0;
	buffer->pages = kmalloc(page_count * sizeof(buffer->pages[0]),
				GFP_KERNEL);
	if (buffer->pages == NULL)
		return -ENOMEM;

	for (i = 0; i < page_count; i++) {
		buffer->pages[i] = alloc_page(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO);
		if (buffer->pages[i] == NULL)
			break;
	}
	buffer->page_count = i;
	if (i < page_count) {
		fw_iso_buffer_destroy(buffer, NULL);
		return -ENOMEM;
	}

	return 0;
}

int fw_iso_buffer_map_dma(struct fw_iso_buffer *buffer, struct fw_card *card,
			  enum dma_data_direction direction)
{
	dma_addr_t address;
	int i;

	buffer->direction = direction;

	for (i = 0; i < buffer->page_count; i++) {
		address = dma_map_page(card->device, buffer->pages[i],
				       0, PAGE_SIZE, direction);
		if (dma_mapping_error(card->device, address))
			break;

		set_page_private(buffer->pages[i], address);
	}
	buffer->page_count_mapped = i;
	if (i < buffer->page_count)
		return -ENOMEM;

	return 0;
}

int fw_iso_buffer_init(struct fw_iso_buffer *buffer, struct fw_card *card,
		       int page_count, enum dma_data_direction direction)
{
	int ret;

	ret = fw_iso_buffer_alloc(buffer, page_count);
	if (ret < 0)
		return ret;

	ret = fw_iso_buffer_map_dma(buffer, card, direction);
	if (ret < 0)
		fw_iso_buffer_destroy(buffer, card);

	return ret;
}
EXPORT_SYMBOL(fw_iso_buffer_init);

int fw_iso_buffer_map_vma(struct fw_iso_buffer *buffer,
			  struct vm_area_struct *vma)
{
	unsigned long uaddr;
	int i, err;

	uaddr = vma->vm_start;
	for (i = 0; i < buffer->page_count; i++) {
		err = vm_insert_page(vma, uaddr, buffer->pages[i]);
		if (err)
			return err;

		uaddr += PAGE_SIZE;
	}

	return 0;
}

void fw_iso_buffer_destroy(struct fw_iso_buffer *buffer,
			   struct fw_card *card)
{
	int i;
	dma_addr_t address;

	for (i = 0; i < buffer->page_count_mapped; i++) {
		address = page_private(buffer->pages[i]);
		dma_unmap_page(card->device, address,
			       PAGE_SIZE, buffer->direction);
	}
	for (i = 0; i < buffer->page_count; i++)
		__free_page(buffer->pages[i]);

	kfree(buffer->pages);
	buffer->pages = NULL;
	buffer->page_count = 0;
	buffer->page_count_mapped = 0;
}
EXPORT_SYMBOL(fw_iso_buffer_destroy);

/* Convert DMA address to offset into virtually contiguous buffer. */
size_t fw_iso_buffer_lookup(struct fw_iso_buffer *buffer, dma_addr_t completed)
{
	size_t i;
	dma_addr_t address;
	ssize_t offset;

	for (i = 0; i < buffer->page_count; i++) {
		address = page_private(buffer->pages[i]);
		offset = (ssize_t)completed - (ssize_t)address;
		if (offset > 0 && offset <= PAGE_SIZE)
			return (i << PAGE_SHIFT) + offset;
	}

	return 0;
}

struct fw_iso_context *fw_iso_context_create(struct fw_card *card,
		int type, int channel, int speed, size_t header_size,
		fw_iso_callback_t callback, void *callback_data)
{
	struct fw_iso_context *ctx;

	ctx = card->driver->allocate_iso_context(card,
						 type, channel, header_size);
	if (IS_ERR(ctx))
		return ctx;

	ctx->card = card;
	ctx->type = type;
	ctx->channel = channel;
	ctx->speed = speed;
	ctx->header_size = header_size;
	ctx->callback.sc = callback;
	ctx->callback_data = callback_data;

	return ctx;
}
EXPORT_SYMBOL(fw_iso_context_create);

void fw_iso_context_destroy(struct fw_iso_context *ctx)
{
	ctx->card->driver->free_iso_context(ctx);
}
EXPORT_SYMBOL(fw_iso_context_destroy);

int fw_iso_context_start(struct fw_iso_context *ctx,
			 int cycle, int sync, int tags)
{
	return ctx->card->driver->start_iso(ctx, cycle, sync, tags);
}
EXPORT_SYMBOL(fw_iso_context_start);

int fw_iso_context_set_channels(struct fw_iso_context *ctx, u64 *channels)
{
	return ctx->card->driver->set_iso_channels(ctx, channels);
}

int fw_iso_context_queue(struct fw_iso_context *ctx,
			 struct fw_iso_packet *packet,
			 struct fw_iso_buffer *buffer,
			 unsigned long payload)
{
	return ctx->card->driver->queue_iso(ctx, packet, buffer, payload);
}
EXPORT_SYMBOL(fw_iso_context_queue);

void fw_iso_context_queue_flush(struct fw_iso_context *ctx)
{
	ctx->card->driver->flush_queue_iso(ctx);
}
EXPORT_SYMBOL(fw_iso_context_queue_flush);

int fw_iso_context_flush_completions(struct fw_iso_context *ctx)
{
	return ctx->card->driver->flush_iso_completions(ctx);
}
EXPORT_SYMBOL(fw_iso_context_flush_completions);

int fw_iso_context_stop(struct fw_iso_context *ctx)
{
	return ctx->card->driver->stop_iso(ctx);
}
EXPORT_SYMBOL(fw_iso_context_stop);

/*
 * Isochronous bus resource management (channels, bandwidth), client side
 */

static int manage_bandwidth(struct fw_card *card, int irm_id, int generation,
			    int bandwidth, bool allocate)
{
	int try, new, old = allocate ? BANDWIDTH_AVAILABLE_INITIAL : 0;
	__be32 data[2];

	/*
	 * On a 1394a IRM with low contention, try < 1 is enough.
	 * On a 1394-1995 IRM, we need at least try < 2.
	 * Let's just do try < 5.
	 */
	for (try = 0; try < 5; try++) {
		new = allocate ? old - bandwidth : old + bandwidth;
		if (new < 0 || new > BANDWIDTH_AVAILABLE_INITIAL)
			return -EBUSY;

		data[0] = cpu_to_be32(old);
		data[1] = cpu_to_be32(new);
		switch (fw_run_transaction(card, TCODE_LOCK_COMPARE_SWAP,
				irm_id, generation, SCODE_100,
				CSR_REGISTER_BASE + CSR_BANDWIDTH_AVAILABLE,
				data, 8)) {
		case RCODE_GENERATION:
			/* A generation change frees all bandwidth. */
			return allocate ? -EAGAIN : bandwidth;

		case RCODE_COMPLETE:
			if (be32_to_cpup(data) == old)
				return bandwidth;

			old = be32_to_cpup(data);
			/* Fall through. */
		}
	}

	return -EIO;
}

static int manage_channel(struct fw_card *card, int irm_id, int generation,
		u32 channels_mask, u64 offset, bool allocate)
{
	__be32 bit, all, old;
	__be32 data[2];
	int channel, ret = -EIO, retry = 5;

	old = all = allocate ? cpu_to_be32(~0) : 0;

	for (channel = 0; channel < 32; channel++) {
		if (!(channels_mask & 1 << channel))
			continue;

		ret = -EBUSY;

		bit = cpu_to_be32(1 << (31 - channel));
		if ((old & bit) != (all & bit))
			continue;

		data[0] = old;
		data[1] = old ^ bit;
		switch (fw_run_transaction(card, TCODE_LOCK_COMPARE_SWAP,
					   irm_id, generation, SCODE_100,
					   offset, data, 8)) {
		case RCODE_GENERATION:
			/* A generation change frees all channels. */
			return allocate ? -EAGAIN : channel;

		case RCODE_COMPLETE:
			if (data[0] == old)
				return channel;

			old = data[0];

			/* Is the IRM 1394a-2000 compliant? */
			if ((data[0] & bit) == (data[1] & bit))
				continue;

			/* 1394-1995 IRM, fall through to retry. */
		default:
			if (retry) {
				retry--;
				channel--;
			} else {
				ret = -EIO;
			}
		}
	}

	return ret;
}

static void deallocate_channel(struct fw_card *card, int irm_id,
			       int generation, int channel)
{
	u32 mask;
	u64 offset;

	mask = channel < 32 ? 1 << channel : 1 << (channel - 32);
	offset = channel < 32 ? CSR_REGISTER_BASE + CSR_CHANNELS_AVAILABLE_HI :
				CSR_REGISTER_BASE + CSR_CHANNELS_AVAILABLE_LO;

	manage_channel(card, irm_id, generation, mask, offset, false);
}

/**
 * fw_iso_resource_manage() - Allocate or deallocate a channel and/or bandwidth
 *
 * In parameters: card, generation, channels_mask, bandwidth, allocate
 * Out parameters: channel, bandwidth
 * This function blocks (sleeps) during communication with the IRM.
 *
 * Allocates or deallocates at most one channel out of channels_mask.
 * channels_mask is a bitfield with MSB for channel 63 and LSB for channel 0.
 * (Note, the IRM's CHANNELS_AVAILABLE is a big-endian bitfield with MSB for
 * channel 0 and LSB for channel 63.)
 * Allocates or deallocates as many bandwidth allocation units as specified.
 *
 * Returns channel < 0 if no channel was allocated or deallocated.
 * Returns bandwidth = 0 if no bandwidth was allocated or deallocated.
 *
 * If generation is stale, deallocations succeed but allocations fail with
 * channel = -EAGAIN.
 *
 * If channel allocation fails, no bandwidth will be allocated either.
 * If bandwidth allocation fails, no channel will be allocated either.
 * But deallocations of channel and bandwidth are tried independently
 * of each other's success.
 */
void fw_iso_resource_manage(struct fw_card *card, int generation,
			    u64 channels_mask, int *channel, int *bandwidth,
			    bool allocate)
{
	u32 channels_hi = channels_mask;	/* channels 31...0 */
	u32 channels_lo = channels_mask >> 32;	/* channels 63...32 */
	int irm_id, ret, c = -EINVAL;

	spin_lock_irq(&card->lock);
	irm_id = card->irm_node->node_id;
	spin_unlock_irq(&card->lock);

	if (channels_hi)
		c = manage_channel(card, irm_id, generation, channels_hi,
				CSR_REGISTER_BASE + CSR_CHANNELS_AVAILABLE_HI,
				allocate);
	if (channels_lo && c < 0) {
		c = manage_channel(card, irm_id, generation, channels_lo,
				CSR_REGISTER_BASE + CSR_CHANNELS_AVAILABLE_LO,
				allocate);
		if (c >= 0)
			c += 32;
	}
	*channel = c;

	if (allocate && channels_mask != 0 && c < 0)
		*bandwidth = 0;

	if (*bandwidth == 0)
		return;

	ret = manage_bandwidth(card, irm_id, generation, *bandwidth, allocate);
	if (ret < 0)
		*bandwidth = 0;

	if (allocate && ret < 0) {
		if (c >= 0)
			deallocate_channel(card, irm_id, generation, c);
		*channel = ret;
	}
}
EXPORT_SYMBOL(fw_iso_resource_manage);
