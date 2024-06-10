/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright 2021 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <linux/vmalloc.h>
#include "vmwgfx_devcaps.h"

#include "vmwgfx_drv.h"


struct svga_3d_compat_cap {
	SVGA3dFifoCapsRecordHeader header;
	SVGA3dFifoCapPair pairs[SVGA3D_DEVCAP_MAX];
};


static u32 vmw_mask_legacy_multisample(unsigned int cap, u32 fmt_value)
{
	/*
	 * A version of user-space exists which use MULTISAMPLE_MASKABLESAMPLES
	 * to check the sample count supported by virtual device. Since there
	 * never was support for multisample count for backing MOB return 0.
	 *
	 * MULTISAMPLE_MASKABLESAMPLES devcap is marked as deprecated by virtual
	 * device.
	 */
	if (cap == SVGA3D_DEVCAP_DEAD5)
		return 0;

	return fmt_value;
}

static int vmw_fill_compat_cap(struct vmw_private *dev_priv, void *bounce,
			       size_t size)
{
	struct svga_3d_compat_cap *compat_cap =
		(struct svga_3d_compat_cap *) bounce;
	unsigned int i;
	size_t pair_offset = offsetof(struct svga_3d_compat_cap, pairs);
	unsigned int max_size;

	if (size < pair_offset)
		return -EINVAL;

	max_size = (size - pair_offset) / sizeof(SVGA3dFifoCapPair);

	if (max_size > SVGA3D_DEVCAP_MAX)
		max_size = SVGA3D_DEVCAP_MAX;

	compat_cap->header.length =
		(pair_offset + max_size * sizeof(SVGA3dFifoCapPair)) / sizeof(u32);
	compat_cap->header.type = SVGA3D_FIFO_CAPS_RECORD_DEVCAPS;

	for (i = 0; i < max_size; ++i) {
		compat_cap->pairs[i][0] = i;
		compat_cap->pairs[i][1] = vmw_mask_legacy_multisample
			(i, dev_priv->devcaps[i]);
	}

	return 0;
}

int vmw_devcaps_create(struct vmw_private *vmw)
{
	bool gb_objects = !!(vmw->capabilities & SVGA_CAP_GBOBJECTS);
	uint32_t i;

	if (gb_objects) {
		vmw->devcaps = vzalloc(sizeof(uint32_t) * SVGA3D_DEVCAP_MAX);
		if (!vmw->devcaps)
			return -ENOMEM;
		for (i = 0; i < SVGA3D_DEVCAP_MAX; ++i) {
			vmw_write(vmw, SVGA_REG_DEV_CAP, i);
			vmw->devcaps[i] = vmw_read(vmw, SVGA_REG_DEV_CAP);
		}
	}
	return 0;
}

void vmw_devcaps_destroy(struct vmw_private *vmw)
{
	vfree(vmw->devcaps);
	vmw->devcaps = NULL;
}


uint32 vmw_devcaps_size(const struct vmw_private *vmw,
			bool gb_aware)
{
	bool gb_objects = !!(vmw->capabilities & SVGA_CAP_GBOBJECTS);
	if (gb_objects && gb_aware)
		return SVGA3D_DEVCAP_MAX * sizeof(uint32_t);
	else if (gb_objects)
		return  sizeof(struct svga_3d_compat_cap) +
				sizeof(uint32_t);
	else if (vmw->fifo_mem != NULL)
		return (SVGA_FIFO_3D_CAPS_LAST - SVGA_FIFO_3D_CAPS + 1) *
				sizeof(uint32_t);
	else
		return 0;
}

int vmw_devcaps_copy(struct vmw_private *vmw, bool gb_aware,
		     void *dst, uint32_t dst_size)
{
	int ret;
	bool gb_objects = !!(vmw->capabilities & SVGA_CAP_GBOBJECTS);
	if (gb_objects && gb_aware) {
		memcpy(dst, vmw->devcaps, dst_size);
	} else if (gb_objects) {
		ret = vmw_fill_compat_cap(vmw, dst, dst_size);
		if (unlikely(ret != 0))
			return ret;
	} else if (vmw->fifo_mem) {
		u32 *fifo_mem = vmw->fifo_mem;
		memcpy(dst, &fifo_mem[SVGA_FIFO_3D_CAPS], dst_size);
	} else
		return -EINVAL;
	return 0;
}
