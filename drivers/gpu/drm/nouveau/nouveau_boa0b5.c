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

#include <nvhw/class/cla0b5.h>

int
nve0_bo_move_copy(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_resource *old_reg, struct ttm_resource *new_reg)
{
	struct nouveau_mem *mem = nouveau_mem(old_reg);
	struct nvif_push *push = chan->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 10);
	if (ret)
		return ret;

	PUSH_MTHD(push, NVA0B5, OFFSET_IN_UPPER,
		  NVVAL(NVA0B5, OFFSET_IN_UPPER, UPPER, upper_32_bits(mem->vma[0].addr)),

				OFFSET_IN_LOWER, lower_32_bits(mem->vma[0].addr),

				OFFSET_OUT_UPPER,
		  NVVAL(NVA0B5, OFFSET_OUT_UPPER, UPPER, upper_32_bits(mem->vma[1].addr)),

				OFFSET_OUT_LOWER, lower_32_bits(mem->vma[1].addr),
				PITCH_IN, PAGE_SIZE,
				PITCH_OUT, PAGE_SIZE,
				LINE_LENGTH_IN, PAGE_SIZE,
				LINE_COUNT, new_reg->num_pages);

	PUSH_IMMD(push, NVA0B5, LAUNCH_DMA,
		  NVDEF(NVA0B5, LAUNCH_DMA, DATA_TRANSFER_TYPE, NON_PIPELINED) |
		  NVDEF(NVA0B5, LAUNCH_DMA, FLUSH_ENABLE, TRUE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SEMAPHORE_TYPE, NONE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, INTERRUPT_TYPE, NONE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SRC_MEMORY_LAYOUT, PITCH) |
		  NVDEF(NVA0B5, LAUNCH_DMA, DST_MEMORY_LAYOUT, PITCH) |
		  NVDEF(NVA0B5, LAUNCH_DMA, MULTI_LINE_ENABLE, TRUE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, REMAP_ENABLE, FALSE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, BYPASS_L2, USE_PTE_SETTING) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SRC_TYPE, VIRTUAL) |
		  NVDEF(NVA0B5, LAUNCH_DMA, DST_TYPE, VIRTUAL));
	return 0;
}

int
nve0_bo_move_init(struct nouveau_channel *chan, u32 handle)
{
	struct nvif_push *push = chan->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 2);
	if (ret)
		return ret;

	PUSH_NVSQ(push, NVA0B5, 0x0000, handle & 0x0000ffff);
	return 0;
}
