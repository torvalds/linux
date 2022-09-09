/*
 * Copyright 2007 Dave Airlied
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors: Dave Airlied <airlied@linux.ie>
 *	    Ben Skeggs   <darktama@iinet.net.au>
 *	    Jeremy Kolb  <jkolb@brandeis.edu>
 */
#include "nouveau_bo.h"
#include "nouveau_dma.h"
#include "nouveau_mem.h"

#include <nvif/push906f.h>

#include <nvhw/class/cl9039.h>

int
nvc0_bo_move_m2mf(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_resource *old_reg, struct ttm_resource *new_reg)
{
	struct nvif_push *push = chan->chan.push;
	struct nouveau_mem *mem = nouveau_mem(old_reg);
	u64 src_offset = mem->vma[0].addr;
	u64 dst_offset = mem->vma[1].addr;
	u32 page_count = new_reg->num_pages;
	int ret;

	page_count = new_reg->num_pages;
	while (page_count) {
		int line_count = (page_count > 2047) ? 2047 : page_count;

		ret = PUSH_WAIT(push, 12);
		if (ret)
			return ret;

		PUSH_MTHD(push, NV9039, OFFSET_OUT_UPPER,
			  NVVAL(NV9039, OFFSET_OUT_UPPER, VALUE, upper_32_bits(dst_offset)),

					OFFSET_OUT, lower_32_bits(dst_offset));

		PUSH_MTHD(push, NV9039, OFFSET_IN_UPPER,
			  NVVAL(NV9039, OFFSET_IN_UPPER, VALUE, upper_32_bits(src_offset)),

					OFFSET_IN, lower_32_bits(src_offset),
					PITCH_IN, PAGE_SIZE,
					PITCH_OUT, PAGE_SIZE,
					LINE_LENGTH_IN, PAGE_SIZE,
					LINE_COUNT, line_count);

		PUSH_MTHD(push, NV9039, LAUNCH_DMA,
			  NVDEF(NV9039, LAUNCH_DMA, SRC_INLINE, FALSE) |
			  NVDEF(NV9039, LAUNCH_DMA, SRC_MEMORY_LAYOUT, PITCH) |
			  NVDEF(NV9039, LAUNCH_DMA, DST_MEMORY_LAYOUT, PITCH) |
			  NVDEF(NV9039, LAUNCH_DMA, COMPLETION_TYPE, FLUSH_DISABLE) |
			  NVDEF(NV9039, LAUNCH_DMA, INTERRUPT_TYPE, NONE) |
			  NVDEF(NV9039, LAUNCH_DMA, SEMAPHORE_STRUCT_SIZE, ONE_WORD));

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return 0;
}

int
nvc0_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	struct nvif_push *push = chan->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 2);
	if (ret)
		return ret;

	PUSH_MTHD(push, NV9039, SET_OBJECT, handle);
	return 0;
}
