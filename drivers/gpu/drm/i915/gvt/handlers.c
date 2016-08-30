/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Kevin Tian <kevin.tian@intel.com>
 *    Eddie Dong <eddie.dong@intel.com>
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Pei Zhang <pei.zhang@intel.com>
 *    Niu Bing <bing.niu@intel.com>
 *    Ping Gao <ping.a.gao@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *

 */

#include "i915_drv.h"

/* Register contains RO bits */
#define F_RO		(1 << 0)
/* Register contains graphics address */
#define F_GMADR		(1 << 1)
/* Mode mask registers with high 16 bits as the mask bits */
#define F_MODE_MASK	(1 << 2)
/* This reg can be accessed by GPU commands */
#define F_CMD_ACCESS	(1 << 3)
/* This reg has been accessed by a VM */
#define F_ACCESSED	(1 << 4)
/* This reg has been accessed through GPU commands */
#define F_CMD_ACCESSED	(1 << 5)
/* This reg could be accessed by unaligned address */
#define F_UNALIGN	(1 << 6)

unsigned long intel_gvt_get_device_type(struct intel_gvt *gvt)
{
	if (IS_BROADWELL(gvt->dev_priv))
		return D_BDW;
	else if (IS_SKYLAKE(gvt->dev_priv))
		return D_SKL;

	return 0;
}

bool intel_gvt_match_device(struct intel_gvt *gvt,
		unsigned long device)
{
	return intel_gvt_get_device_type(gvt) & device;
}

static int new_mmio_info(struct intel_gvt *gvt,
		u32 offset, u32 flags, u32 size,
		u32 addr_mask, u32 ro_mask, u32 device,
		void *read, void *write)
{
	struct intel_gvt_mmio_info *info, *p;
	u32 start, end, i;

	if (!intel_gvt_match_device(gvt, device))
		return 0;

	if (WARN_ON(!IS_ALIGNED(offset, 4)))
		return -EINVAL;

	start = offset;
	end = offset + size;

	for (i = start; i < end; i += 4) {
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			return -ENOMEM;

		info->offset = i;
		p = intel_gvt_find_mmio_info(gvt, info->offset);
		if (p)
			gvt_err("dup mmio definition offset %x\n",
				info->offset);
		info->size = size;
		info->length = (i + 4) < end ? 4 : (end - i);
		info->addr_mask = addr_mask;
		info->device = device;
		info->read = read;
		info->write = write;
		gvt->mmio.mmio_attribute[info->offset / 4] = flags;
		INIT_HLIST_NODE(&info->node);
		hash_add(gvt->mmio.mmio_info_table, &info->node, info->offset);
	}
	return 0;
}

#define MMIO_F(reg, s, f, am, rm, d, r, w) do { \
	ret = new_mmio_info(gvt, INTEL_GVT_MMIO_OFFSET(reg), \
		f, s, am, rm, d, r, w); \
	if (ret) \
		return ret; \
} while (0)

#define MMIO_D(reg, d) \
	MMIO_F(reg, 4, 0, 0, 0, d, NULL, NULL)

#define MMIO_DH(reg, d, r, w) \
	MMIO_F(reg, 4, 0, 0, 0, d, r, w)

#define MMIO_DFH(reg, d, f, r, w) \
	MMIO_F(reg, 4, f, 0, 0, d, r, w)

#define MMIO_GM(reg, d, r, w) \
	MMIO_F(reg, 4, F_GMADR, 0xFFFFF000, 0, d, r, w)

#define MMIO_RO(reg, d, f, rm, r, w) \
	MMIO_F(reg, 4, F_RO | f, 0, rm, d, r, w)

#define MMIO_RING_F(prefix, s, f, am, rm, d, r, w) do { \
	MMIO_F(prefix(RENDER_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(BLT_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(GEN6_BSD_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(VEBOX_RING_BASE), s, f, am, rm, d, r, w); \
} while (0)

#define MMIO_RING_D(prefix, d) \
	MMIO_RING_F(prefix, 4, 0, 0, 0, d, NULL, NULL)

#define MMIO_RING_DFH(prefix, d, f, r, w) \
	MMIO_RING_F(prefix, 4, f, 0, 0, d, r, w)

#define MMIO_RING_GM(prefix, d, r, w) \
	MMIO_RING_F(prefix, 4, F_GMADR, 0xFFFF0000, 0, d, r, w)

#define MMIO_RING_RO(prefix, d, f, rm, r, w) \
	MMIO_RING_F(prefix, 4, F_RO | f, 0, rm, d, r, w)

static int init_generic_mmio_info(struct intel_gvt *gvt)
{
	int ret;

	MMIO_F(0, 0, 0, 0, 0, D_ALL, NULL, NULL);
	return 0;
}

static int init_broadwell_mmio_info(struct intel_gvt *gvt)
{
	int ret;

	MMIO_F(0, 0, 0, 0, 0, D_ALL, NULL, NULL);
	return 0;
}

/**
 * intel_gvt_find_mmio_info - find MMIO information entry by aligned offset
 * @gvt: GVT device
 * @offset: register offset
 *
 * This function is used to find the MMIO information entry from hash table
 *
 * Returns:
 * pointer to MMIO information entry, NULL if not exists
 */
struct intel_gvt_mmio_info *intel_gvt_find_mmio_info(struct intel_gvt *gvt,
	unsigned int offset)
{
	struct intel_gvt_mmio_info *e;

	WARN_ON(!IS_ALIGNED(offset, 4));

	hash_for_each_possible(gvt->mmio.mmio_info_table, e, node, offset) {
		if (e->offset == offset)
			return e;
	}
	return NULL;
}

/**
 * intel_gvt_clean_mmio_info - clean up MMIO information table for GVT device
 * @gvt: GVT device
 *
 * This function is called at the driver unloading stage, to clean up the MMIO
 * information table of GVT device
 *
 */
void intel_gvt_clean_mmio_info(struct intel_gvt *gvt)
{
	struct hlist_node *tmp;
	struct intel_gvt_mmio_info *e;
	int i;

	hash_for_each_safe(gvt->mmio.mmio_info_table, i, tmp, e, node)
		kfree(e);

	vfree(gvt->mmio.mmio_attribute);
	gvt->mmio.mmio_attribute = NULL;
}

/**
 * intel_gvt_setup_mmio_info - setup MMIO information table for GVT device
 * @gvt: GVT device
 *
 * This function is called at the initialization stage, to setup the MMIO
 * information table for GVT device
 *
 * Returns:
 * zero on success, negative if failed.
 */
int intel_gvt_setup_mmio_info(struct intel_gvt *gvt)
{
	struct intel_gvt_device_info *info = &gvt->device_info;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	gvt->mmio.mmio_attribute = vzalloc(info->mmio_size);
	if (!gvt->mmio.mmio_attribute)
		return -ENOMEM;

	ret = init_generic_mmio_info(gvt);
	if (ret)
		goto err;

	if (IS_BROADWELL(dev_priv)) {
		ret = init_broadwell_mmio_info(gvt);
		if (ret)
			goto err;
	}
	return 0;
err:
	intel_gvt_clean_mmio_info(gvt);
	return ret;
}
