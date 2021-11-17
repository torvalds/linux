// SPDX-License-Identifier: MIT
/* Copyright (C) 2006-2017 Oracle Corporation */

#include <linux/vbox_err.h>
#include "vbox_drv.h"
#include "vboxvideo_guest.h"
#include "hgsmi_channels.h"

/*
 * There is a hardware ring buffer in the graphics device video RAM, formerly
 * in the VBox VMMDev PCI memory space.
 * All graphics commands go there serialized by vbva_buffer_begin_update.
 * and vbva_buffer_end_update.
 *
 * free_offset is writing position. data_offset is reading position.
 * free_offset == data_offset means buffer is empty.
 * There must be always gap between data_offset and free_offset when data
 * are in the buffer.
 * Guest only changes free_offset, host changes data_offset.
 */

static u32 vbva_buffer_available(const struct vbva_buffer *vbva)
{
	s32 diff = vbva->data_offset - vbva->free_offset;

	return diff > 0 ? diff : vbva->data_len + diff;
}

static void vbva_buffer_place_data_at(struct vbva_buf_ctx *vbva_ctx,
				      const void *p, u32 len, u32 offset)
{
	struct vbva_buffer *vbva = vbva_ctx->vbva;
	u32 bytes_till_boundary = vbva->data_len - offset;
	u8 *dst = &vbva->data[offset];
	s32 diff = len - bytes_till_boundary;

	if (diff <= 0) {
		/* Chunk will not cross buffer boundary. */
		memcpy(dst, p, len);
	} else {
		/* Chunk crosses buffer boundary. */
		memcpy(dst, p, bytes_till_boundary);
		memcpy(&vbva->data[0], (u8 *)p + bytes_till_boundary, diff);
	}
}

static void vbva_buffer_flush(struct gen_pool *ctx)
{
	struct vbva_flush *p;

	p = hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA, VBVA_FLUSH);
	if (!p)
		return;

	p->reserved = 0;

	hgsmi_buffer_submit(ctx, p);
	hgsmi_buffer_free(ctx, p);
}

bool vbva_write(struct vbva_buf_ctx *vbva_ctx, struct gen_pool *ctx,
		const void *p, u32 len)
{
	struct vbva_record *record;
	struct vbva_buffer *vbva;
	u32 available;

	vbva = vbva_ctx->vbva;
	record = vbva_ctx->record;

	if (!vbva || vbva_ctx->buffer_overflow ||
	    !record || !(record->len_and_flags & VBVA_F_RECORD_PARTIAL))
		return false;

	available = vbva_buffer_available(vbva);

	while (len > 0) {
		u32 chunk = len;

		if (chunk >= available) {
			vbva_buffer_flush(ctx);
			available = vbva_buffer_available(vbva);
		}

		if (chunk >= available) {
			if (WARN_ON(available <= vbva->partial_write_tresh)) {
				vbva_ctx->buffer_overflow = true;
				return false;
			}
			chunk = available - vbva->partial_write_tresh;
		}

		vbva_buffer_place_data_at(vbva_ctx, p, chunk,
					  vbva->free_offset);

		vbva->free_offset = (vbva->free_offset + chunk) %
				    vbva->data_len;
		record->len_and_flags += chunk;
		available -= chunk;
		len -= chunk;
		p += chunk;
	}

	return true;
}

static bool vbva_inform_host(struct vbva_buf_ctx *vbva_ctx,
			     struct gen_pool *ctx, s32 screen, bool enable)
{
	struct vbva_enable_ex *p;
	bool ret;

	p = hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA, VBVA_ENABLE);
	if (!p)
		return false;

	p->base.flags = enable ? VBVA_F_ENABLE : VBVA_F_DISABLE;
	p->base.offset = vbva_ctx->buffer_offset;
	p->base.result = VERR_NOT_SUPPORTED;
	if (screen >= 0) {
		p->base.flags |= VBVA_F_EXTENDED | VBVA_F_ABSOFFSET;
		p->screen_id = screen;
	}

	hgsmi_buffer_submit(ctx, p);

	if (enable)
		ret = p->base.result >= 0;
	else
		ret = true;

	hgsmi_buffer_free(ctx, p);

	return ret;
}

bool vbva_enable(struct vbva_buf_ctx *vbva_ctx, struct gen_pool *ctx,
		 struct vbva_buffer *vbva, s32 screen)
{
	bool ret = false;

	memset(vbva, 0, sizeof(*vbva));
	vbva->partial_write_tresh = 256;
	vbva->data_len = vbva_ctx->buffer_length - sizeof(struct vbva_buffer);
	vbva_ctx->vbva = vbva;

	ret = vbva_inform_host(vbva_ctx, ctx, screen, true);
	if (!ret)
		vbva_disable(vbva_ctx, ctx, screen);

	return ret;
}

void vbva_disable(struct vbva_buf_ctx *vbva_ctx, struct gen_pool *ctx,
		  s32 screen)
{
	vbva_ctx->buffer_overflow = false;
	vbva_ctx->record = NULL;
	vbva_ctx->vbva = NULL;

	vbva_inform_host(vbva_ctx, ctx, screen, false);
}

bool vbva_buffer_begin_update(struct vbva_buf_ctx *vbva_ctx,
			      struct gen_pool *ctx)
{
	struct vbva_record *record;
	u32 next;

	if (!vbva_ctx->vbva ||
	    !(vbva_ctx->vbva->host_flags.host_events & VBVA_F_MODE_ENABLED))
		return false;

	WARN_ON(vbva_ctx->buffer_overflow || vbva_ctx->record);

	next = (vbva_ctx->vbva->record_free_index + 1) % VBVA_MAX_RECORDS;

	/* Flush if all slots in the records queue are used */
	if (next == vbva_ctx->vbva->record_first_index)
		vbva_buffer_flush(ctx);

	/* If even after flush there is no place then fail the request */
	if (next == vbva_ctx->vbva->record_first_index)
		return false;

	record = &vbva_ctx->vbva->records[vbva_ctx->vbva->record_free_index];
	record->len_and_flags = VBVA_F_RECORD_PARTIAL;
	vbva_ctx->vbva->record_free_index = next;
	/* Remember which record we are using. */
	vbva_ctx->record = record;

	return true;
}

void vbva_buffer_end_update(struct vbva_buf_ctx *vbva_ctx)
{
	struct vbva_record *record = vbva_ctx->record;

	WARN_ON(!vbva_ctx->vbva || !record ||
		!(record->len_and_flags & VBVA_F_RECORD_PARTIAL));

	/* Mark the record completed. */
	record->len_and_flags &= ~VBVA_F_RECORD_PARTIAL;

	vbva_ctx->buffer_overflow = false;
	vbva_ctx->record = NULL;
}

void vbva_setup_buffer_context(struct vbva_buf_ctx *vbva_ctx,
			       u32 buffer_offset, u32 buffer_length)
{
	vbva_ctx->buffer_offset = buffer_offset;
	vbva_ctx->buffer_length = buffer_length;
}
