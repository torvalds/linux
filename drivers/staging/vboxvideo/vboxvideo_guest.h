/*
 * Copyright (C) 2006-2017 Oracle Corporation
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
 */

#ifndef __VBOXVIDEO_GUEST_H__
#define __VBOXVIDEO_GUEST_H__

#include <linux/genalloc.h>
#include "vboxvideo.h"

/**
 * Structure grouping the context needed for sending graphics acceleration
 * information to the host via VBVA.  Each screen has its own VBVA buffer.
 */
struct vbva_buf_ctx {
	/** Offset of the buffer in the VRAM section for the screen */
	u32 buffer_offset;
	/** Length of the buffer in bytes */
	u32 buffer_length;
	/** Set if we wrote to the buffer faster than the host could read it */
	bool buffer_overflow;
	/** VBVA record that we are currently preparing for the host, or NULL */
	struct vbva_record *record;
	/**
	 * Pointer to the VBVA buffer mapped into the current address space.
	 * Will be NULL if VBVA is not enabled.
	 */
	struct vbva_buffer *vbva;
};

/**
 * @name Base HGSMI APIs
 * @{
 */
int hgsmi_report_flags_location(struct gen_pool *ctx, u32 location);
int hgsmi_send_caps_info(struct gen_pool *ctx, u32 caps);
int hgsmi_test_query_conf(struct gen_pool *ctx);
int hgsmi_query_conf(struct gen_pool *ctx, u32 index, u32 *value_ret);
int hgsmi_update_pointer_shape(struct gen_pool *ctx, u32 flags,
			       u32 hot_x, u32 hot_y, u32 width, u32 height,
			       u8 *pixels, u32 len);
int hgsmi_cursor_position(struct gen_pool *ctx, bool report_position,
			  u32 x, u32 y, u32 *x_host, u32 *y_host);
/** @}  */

/**
 * @name VBVA APIs
 * @{
 */
bool vbva_enable(struct vbva_buf_ctx *vbva_ctx, struct gen_pool *ctx,
		 struct vbva_buffer *vbva, s32 screen);
void vbva_disable(struct vbva_buf_ctx *vbva_ctx, struct gen_pool *ctx,
		  s32 screen);
bool vbva_buffer_begin_update(struct vbva_buf_ctx *vbva_ctx,
			      struct gen_pool *ctx);
void vbva_buffer_end_update(struct vbva_buf_ctx *vbva_ctx);
bool vbva_write(struct vbva_buf_ctx *vbva_ctx, struct gen_pool *ctx,
		const void *p, u32 len);
void vbva_setup_buffer_context(struct vbva_buf_ctx *vbva_ctx,
			       u32 buffer_offset, u32 buffer_length);
/** @}  */

/**
 * @name Modesetting APIs
 * @{
 */
void hgsmi_process_display_info(struct gen_pool *ctx, u32 display,
				s32 origin_x, s32 origin_y, u32 start_offset,
				u32 pitch, u32 width, u32 height,
				u16 bpp, u16 flags);
int hgsmi_update_input_mapping(struct gen_pool *ctx, s32 origin_x, s32 origin_y,
			       u32 width, u32 height);
int hgsmi_get_mode_hints(struct gen_pool *ctx, unsigned int screens,
			 struct vbva_modehint *hints);
/** @}  */

#endif
