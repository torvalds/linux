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
#include "nouveau_drv.h"
#include "nouveau_mem.h"

#include <nvif/push206e.h>

#include <nvhw/class/cl5039.h>

int
nv50_bo_move_m2mf(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_reg, struct ttm_mem_reg *new_reg)
{
	struct nouveau_mem *mem = nouveau_mem(old_reg);
	struct nvif_push *push = chan->chan.push;
	u64 length = (new_reg->num_pages << PAGE_SHIFT);
	u64 src_offset = mem->vma[0].addr;
	u64 dst_offset = mem->vma[1].addr;
	int src_tiled = !!mem->kind;
	int dst_tiled = !!nouveau_mem(new_reg)->kind;
	int ret;

	while (length) {
		u32 amount, stride, height;

		ret = PUSH_WAIT(push, 18 + 6 * (src_tiled + dst_tiled));
		if (ret)
			return ret;

		amount  = min(length, (u64)(4 * 1024 * 1024));
		stride  = 16 * 4;
		height  = amount / stride;

		if (src_tiled) {
			PUSH_NVSQ(push, NV5039, 0x0200, 0,
						0x0204, 0,
						0x0208, stride,
						0x020c, height,
						0x0210, 1,
						0x0214, 0,
						0x0218, 0);
		} else {
			PUSH_NVSQ(push, NV5039, 0x0200, 1);
		}

		if (dst_tiled) {
			PUSH_NVSQ(push, NV5039, 0x021c, 0,
						0x0220, 0,
						0x0224, stride,
						0x0228, height,
						0x022c, 1,
						0x0230, 0,
						0x0234, 0);
		} else {
			PUSH_NVSQ(push, NV5039, 0x021c, 1);
		}

		PUSH_NVSQ(push, NV5039, 0x0238, upper_32_bits(src_offset),
					0x023c, upper_32_bits(dst_offset));
		PUSH_NVSQ(push, NV5039, 0x030c, lower_32_bits(src_offset),
					0x0310, lower_32_bits(dst_offset),
					0x0314, stride,
					0x0318, stride,
					0x031c, stride,
					0x0320, height,
					0x0324, 0x00000101,
					0x0328, 0x00000000);
		PUSH_NVSQ(push, NV5039, 0x0100, 0x00000000);

		length -= amount;
		src_offset += amount;
		dst_offset += amount;
	}

	return 0;
}

int
nv50_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	struct nvif_push *push = chan->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 6);
	if (ret)
		return ret;

	PUSH_MTHD(push, NV5039, SET_OBJECT, handle);
	PUSH_MTHD(push, NV5039, SET_CONTEXT_DMA_NOTIFY, chan->drm->ntfy.handle,
				SET_CONTEXT_DMA_BUFFER_IN, chan->vram.handle,
				SET_CONTEXT_DMA_BUFFER_OUT, chan->vram.handle);
	return 0;
}
