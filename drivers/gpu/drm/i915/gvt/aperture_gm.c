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
 *    Dexuan Cui
 *
 * Contributors:
 *    Pei Zhang <pei.zhang@intel.com>
 *    Min He <min.he@intel.com>
 *    Niu Bing <bing.niu@intel.com>
 *    Yulei Zhang <yulei.zhang@intel.com>
 *    Zhenyu Wang <zhenyuw@linux.intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include "i915_drv.h"
#include "i915_gem_fence_reg.h"
#include "gvt.h"

static int alloc_gm(struct intel_vgpu *vgpu, bool high_gm)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	unsigned int flags;
	u64 start, end, size;
	struct drm_mm_node *node;
	int ret;

	if (high_gm) {
		node = &vgpu->gm.high_gm_node;
		size = vgpu_hidden_sz(vgpu);
		start = ALIGN(gvt_hidden_gmadr_base(gvt), I915_GTT_PAGE_SIZE);
		end = ALIGN(gvt_hidden_gmadr_end(gvt), I915_GTT_PAGE_SIZE);
		flags = PIN_HIGH;
	} else {
		node = &vgpu->gm.low_gm_node;
		size = vgpu_aperture_sz(vgpu);
		start = ALIGN(gvt_aperture_gmadr_base(gvt), I915_GTT_PAGE_SIZE);
		end = ALIGN(gvt_aperture_gmadr_end(gvt), I915_GTT_PAGE_SIZE);
		flags = PIN_MAPPABLE;
	}

	mutex_lock(&dev_priv->drm.struct_mutex);
	mmio_hw_access_pre(dev_priv);
	ret = i915_gem_gtt_insert(&dev_priv->ggtt.vm, node,
				  size, I915_GTT_PAGE_SIZE,
				  I915_COLOR_UNEVICTABLE,
				  start, end, flags);
	mmio_hw_access_post(dev_priv);
	mutex_unlock(&dev_priv->drm.struct_mutex);
	if (ret)
		gvt_err("fail to alloc %s gm space from host\n",
			high_gm ? "high" : "low");

	return ret;
}

static int alloc_vgpu_gm(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	ret = alloc_gm(vgpu, false);
	if (ret)
		return ret;

	ret = alloc_gm(vgpu, true);
	if (ret)
		goto out_free_aperture;

	gvt_dbg_core("vgpu%d: alloc low GM start %llx size %llx\n", vgpu->id,
		     vgpu_aperture_offset(vgpu), vgpu_aperture_sz(vgpu));

	gvt_dbg_core("vgpu%d: alloc high GM start %llx size %llx\n", vgpu->id,
		     vgpu_hidden_offset(vgpu), vgpu_hidden_sz(vgpu));

	return 0;
out_free_aperture:
	mutex_lock(&dev_priv->drm.struct_mutex);
	drm_mm_remove_node(&vgpu->gm.low_gm_node);
	mutex_unlock(&dev_priv->drm.struct_mutex);
	return ret;
}

static void free_vgpu_gm(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	mutex_lock(&dev_priv->drm.struct_mutex);
	drm_mm_remove_node(&vgpu->gm.low_gm_node);
	drm_mm_remove_node(&vgpu->gm.high_gm_node);
	mutex_unlock(&dev_priv->drm.struct_mutex);
}

/**
 * intel_vgpu_write_fence - write fence registers owned by a vGPU
 * @vgpu: vGPU instance
 * @fence: vGPU fence register number
 * @value: Fence register value to be written
 *
 * This function is used to write fence registers owned by a vGPU. The vGPU
 * fence register number will be translated into HW fence register number.
 *
 */
void intel_vgpu_write_fence(struct intel_vgpu *vgpu,
		u32 fence, u64 value)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	struct i915_fence_reg *reg;
	i915_reg_t fence_reg_lo, fence_reg_hi;

	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	if (WARN_ON(fence >= vgpu_fence_sz(vgpu)))
		return;

	reg = vgpu->fence.regs[fence];
	if (WARN_ON(!reg))
		return;

	fence_reg_lo = FENCE_REG_GEN6_LO(reg->id);
	fence_reg_hi = FENCE_REG_GEN6_HI(reg->id);

	I915_WRITE(fence_reg_lo, 0);
	POSTING_READ(fence_reg_lo);

	I915_WRITE(fence_reg_hi, upper_32_bits(value));
	I915_WRITE(fence_reg_lo, lower_32_bits(value));
	POSTING_READ(fence_reg_lo);
}

static void _clear_vgpu_fence(struct intel_vgpu *vgpu)
{
	int i;

	for (i = 0; i < vgpu_fence_sz(vgpu); i++)
		intel_vgpu_write_fence(vgpu, i, 0);
}

static void free_vgpu_fence(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	struct i915_fence_reg *reg;
	u32 i;

	if (WARN_ON(!vgpu_fence_sz(vgpu)))
		return;

	intel_runtime_pm_get(dev_priv);

	mutex_lock(&dev_priv->drm.struct_mutex);
	_clear_vgpu_fence(vgpu);
	for (i = 0; i < vgpu_fence_sz(vgpu); i++) {
		reg = vgpu->fence.regs[i];
		i915_unreserve_fence(reg);
		vgpu->fence.regs[i] = NULL;
	}
	mutex_unlock(&dev_priv->drm.struct_mutex);

	intel_runtime_pm_put_unchecked(dev_priv);
}

static int alloc_vgpu_fence(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	struct i915_fence_reg *reg;
	int i;

	intel_runtime_pm_get(dev_priv);

	/* Request fences from host */
	mutex_lock(&dev_priv->drm.struct_mutex);

	for (i = 0; i < vgpu_fence_sz(vgpu); i++) {
		reg = i915_reserve_fence(dev_priv);
		if (IS_ERR(reg))
			goto out_free_fence;

		vgpu->fence.regs[i] = reg;
	}

	_clear_vgpu_fence(vgpu);

	mutex_unlock(&dev_priv->drm.struct_mutex);
	intel_runtime_pm_put_unchecked(dev_priv);
	return 0;
out_free_fence:
	gvt_vgpu_err("Failed to alloc fences\n");
	/* Return fences to host, if fail */
	for (i = 0; i < vgpu_fence_sz(vgpu); i++) {
		reg = vgpu->fence.regs[i];
		if (!reg)
			continue;
		i915_unreserve_fence(reg);
		vgpu->fence.regs[i] = NULL;
	}
	mutex_unlock(&dev_priv->drm.struct_mutex);
	intel_runtime_pm_put_unchecked(dev_priv);
	return -ENOSPC;
}

static void free_resource(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;

	gvt->gm.vgpu_allocated_low_gm_size -= vgpu_aperture_sz(vgpu);
	gvt->gm.vgpu_allocated_high_gm_size -= vgpu_hidden_sz(vgpu);
	gvt->fence.vgpu_allocated_fence_num -= vgpu_fence_sz(vgpu);
}

static int alloc_resource(struct intel_vgpu *vgpu,
		struct intel_vgpu_creation_params *param)
{
	struct intel_gvt *gvt = vgpu->gvt;
	unsigned long request, avail, max, taken;
	const char *item;

	if (!param->low_gm_sz || !param->high_gm_sz || !param->fence_sz) {
		gvt_vgpu_err("Invalid vGPU creation params\n");
		return -EINVAL;
	}

	item = "low GM space";
	max = gvt_aperture_sz(gvt) - HOST_LOW_GM_SIZE;
	taken = gvt->gm.vgpu_allocated_low_gm_size;
	avail = max - taken;
	request = MB_TO_BYTES(param->low_gm_sz);

	if (request > avail)
		goto no_enough_resource;

	vgpu_aperture_sz(vgpu) = ALIGN(request, I915_GTT_PAGE_SIZE);

	item = "high GM space";
	max = gvt_hidden_sz(gvt) - HOST_HIGH_GM_SIZE;
	taken = gvt->gm.vgpu_allocated_high_gm_size;
	avail = max - taken;
	request = MB_TO_BYTES(param->high_gm_sz);

	if (request > avail)
		goto no_enough_resource;

	vgpu_hidden_sz(vgpu) = ALIGN(request, I915_GTT_PAGE_SIZE);

	item = "fence";
	max = gvt_fence_sz(gvt) - HOST_FENCE;
	taken = gvt->fence.vgpu_allocated_fence_num;
	avail = max - taken;
	request = param->fence_sz;

	if (request > avail)
		goto no_enough_resource;

	vgpu_fence_sz(vgpu) = request;

	gvt->gm.vgpu_allocated_low_gm_size += MB_TO_BYTES(param->low_gm_sz);
	gvt->gm.vgpu_allocated_high_gm_size += MB_TO_BYTES(param->high_gm_sz);
	gvt->fence.vgpu_allocated_fence_num += param->fence_sz;
	return 0;

no_enough_resource:
	gvt_err("fail to allocate resource %s\n", item);
	gvt_err("request %luMB avail %luMB max %luMB taken %luMB\n",
		BYTES_TO_MB(request), BYTES_TO_MB(avail),
		BYTES_TO_MB(max), BYTES_TO_MB(taken));
	return -ENOSPC;
}

/**
 * inte_gvt_free_vgpu_resource - free HW resource owned by a vGPU
 * @vgpu: a vGPU
 *
 * This function is used to free the HW resource owned by a vGPU.
 *
 */
void intel_vgpu_free_resource(struct intel_vgpu *vgpu)
{
	free_vgpu_gm(vgpu);
	free_vgpu_fence(vgpu);
	free_resource(vgpu);
}

/**
 * intel_vgpu_reset_resource - reset resource state owned by a vGPU
 * @vgpu: a vGPU
 *
 * This function is used to reset resource state owned by a vGPU.
 *
 */
void intel_vgpu_reset_resource(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	intel_runtime_pm_get(dev_priv);
	_clear_vgpu_fence(vgpu);
	intel_runtime_pm_put_unchecked(dev_priv);
}

/**
 * intel_alloc_vgpu_resource - allocate HW resource for a vGPU
 * @vgpu: vGPU
 * @param: vGPU creation params
 *
 * This function is used to allocate HW resource for a vGPU. User specifies
 * the resource configuration through the creation params.
 *
 * Returns:
 * zero on success, negative error code if failed.
 *
 */
int intel_vgpu_alloc_resource(struct intel_vgpu *vgpu,
		struct intel_vgpu_creation_params *param)
{
	int ret;

	ret = alloc_resource(vgpu, param);
	if (ret)
		return ret;

	ret = alloc_vgpu_gm(vgpu);
	if (ret)
		goto out_free_resource;

	ret = alloc_vgpu_fence(vgpu);
	if (ret)
		goto out_free_vgpu_gm;

	return 0;

out_free_vgpu_gm:
	free_vgpu_gm(vgpu);
out_free_resource:
	free_resource(vgpu);
	return ret;
}
