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

int
nvc0_bo_move_m2mf(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_reg, struct ttm_mem_reg *new_reg)
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

		PUSH_NVSQ(push, NV9039, 0x0238, upper_32_bits(dst_offset),
					0x023c, lower_32_bits(dst_offset));
		PUSH_NVSQ(push, NV9039, 0x030c, upper_32_bits(src_offset),
					0x0310, lower_32_bits(src_offset),
					0x0314, PAGE_SIZE, /* src_pitch */
					0x0318, PAGE_SIZE, /* dst_pitch */
					0x031c, PAGE_SIZE, /* line_length */
					0x0320, line_count);
		PUSH_NVSQ(push, NV9039, 0x0300, 0x00100110);

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

	PUSH_NVSQ(push, NV9039, 0x0000, handle);
	return 0;
}
