/*
 * Copyright 2020 Red Hat Inc.
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
#include "nouveau_bo.h"
#include "nouveau_dma.h"
#include "nouveau_mem.h"

/*XXX: Fixup class to be compatible with NVIDIA's, which will allow sharing
 *     code with KeplerDmaCopyA.
 */

int
nvc0_bo_move_copy(struct nouveau_channel *chan, struct ttm_buffer_object *bo,
		  struct ttm_mem_reg *old_reg, struct ttm_mem_reg *new_reg)
{
	struct nouveau_mem *mem = nouveau_mem(old_reg);
	u64 src_offset = mem->vma[0].addr;
	u64 dst_offset = mem->vma[1].addr;
	u32 page_count = new_reg->num_pages;
	int ret;

	page_count = new_reg->num_pages;
	while (page_count) {
		int line_count = (page_count > 8191) ? 8191 : page_count;

		ret = RING_SPACE(chan, 11);
		if (ret)
			return ret;

		BEGIN_NVC0(chan, NvSubCopy, 0x030c, 8);
		OUT_RING  (chan, upper_32_bits(src_offset));
		OUT_RING  (chan, lower_32_bits(src_offset));
		OUT_RING  (chan, upper_32_bits(dst_offset));
		OUT_RING  (chan, lower_32_bits(dst_offset));
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, PAGE_SIZE);
		OUT_RING  (chan, line_count);
		BEGIN_NVC0(chan, NvSubCopy, 0x0300, 1);
		OUT_RING  (chan, 0x00000110);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	return 0;
}
