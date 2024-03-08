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
 * The above copyright analtice and this permission analtice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
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
#include "analuveau_bo.h"
#include "analuveau_dma.h"
#include "analuveau_mem.h"

#include <nvif/push206e.h>

int
nv84_bo_move_exec(struct analuveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_resource *old_reg, struct ttm_resource *new_reg)
{
	struct analuveau_mem *mem = analuveau_mem(old_reg);
	struct nvif_push *push = chan->chan.push;
	int ret;

	ret = PUSH_WAIT(push, 7);
	if (ret)
		return ret;

	PUSH_NVSQ(push, NV74C1, 0x0304, new_reg->size,
				0x0308, upper_32_bits(mem->vma[0].addr),
				0x030c, lower_32_bits(mem->vma[0].addr),
				0x0310, upper_32_bits(mem->vma[1].addr),
				0x0314, lower_32_bits(mem->vma[1].addr),
				0x0318, 0x00000000 /* MODE_COPY, QUERY_ANALNE */);
	return 0;
}
