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
 *    Eddie Dong <eddie.dong@intel.com>
 *    Kevin Tian <kevin.tian@intel.com>
 *
 * Contributors:
 *    Ping Gao <ping.a.gao@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"
#include "i915_pvinfo.h"

static void clean_vgpu_mmio(struct intel_vgpu *vgpu)
{
	vfree(vgpu->mmio.vreg);
	vgpu->mmio.vreg = vgpu->mmio.sreg = NULL;
}

int setup_vgpu_mmio(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_device_info *info = &gvt->device_info;

	vgpu->mmio.vreg = vzalloc(info->mmio_size * 2);
	if (!vgpu->mmio.vreg)
		return -ENOMEM;

	vgpu->mmio.sreg = vgpu->mmio.vreg + info->mmio_size;

	memcpy(vgpu->mmio.vreg, gvt->firmware.mmio, info->mmio_size);
	memcpy(vgpu->mmio.sreg, gvt->firmware.mmio, info->mmio_size);

	vgpu_vreg(vgpu, GEN6_GT_THREAD_STATUS_REG) = 0;

	/* set the bit 0:2(Core C-State ) to C0 */
	vgpu_vreg(vgpu, GEN6_GT_CORE_STATUS) = 0;
	return 0;
}

static void setup_vgpu_cfg_space(struct intel_vgpu *vgpu,
	struct intel_vgpu_creation_params *param)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_device_info *info = &gvt->device_info;
	u16 *gmch_ctl;
	int i;

	memcpy(vgpu_cfg_space(vgpu), gvt->firmware.cfg_space,
	       info->cfg_space_size);

	if (!param->primary) {
		vgpu_cfg_space(vgpu)[PCI_CLASS_DEVICE] =
			INTEL_GVT_PCI_CLASS_VGA_OTHER;
		vgpu_cfg_space(vgpu)[PCI_CLASS_PROG] =
			INTEL_GVT_PCI_CLASS_VGA_OTHER;
	}

	/* Show guest that there isn't any stolen memory.*/
	gmch_ctl = (u16 *)(vgpu_cfg_space(vgpu) + INTEL_GVT_PCI_GMCH_CONTROL);
	*gmch_ctl &= ~(BDW_GMCH_GMS_MASK << BDW_GMCH_GMS_SHIFT);

	intel_vgpu_write_pci_bar(vgpu, PCI_BASE_ADDRESS_2,
				 gvt_aperture_pa_base(gvt), true);

	vgpu_cfg_space(vgpu)[PCI_COMMAND] &= ~(PCI_COMMAND_IO
					     | PCI_COMMAND_MEMORY
					     | PCI_COMMAND_MASTER);
	/*
	 * Clear the bar upper 32bit and let guest to assign the new value
	 */
	memset(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_1, 0, 4);
	memset(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_3, 0, 4);

	for (i = 0; i < INTEL_GVT_MAX_BAR_NUM; i++) {
		vgpu->cfg_space.bar[i].size = pci_resource_len(
					      gvt->dev_priv->drm.pdev, i * 2);
		vgpu->cfg_space.bar[i].tracked = false;
	}
}

void populate_pvinfo_page(struct intel_vgpu *vgpu)
{
	/* setup the ballooning information */
	vgpu_vreg64(vgpu, vgtif_reg(magic)) = VGT_MAGIC;
	vgpu_vreg(vgpu, vgtif_reg(version_major)) = 1;
	vgpu_vreg(vgpu, vgtif_reg(version_minor)) = 0;
	vgpu_vreg(vgpu, vgtif_reg(display_ready)) = 0;
	vgpu_vreg(vgpu, vgtif_reg(vgt_id)) = vgpu->id;
	vgpu_vreg(vgpu, vgtif_reg(avail_rs.mappable_gmadr.base)) =
		vgpu_aperture_gmadr_base(vgpu);
	vgpu_vreg(vgpu, vgtif_reg(avail_rs.mappable_gmadr.size)) =
		vgpu_aperture_sz(vgpu);
	vgpu_vreg(vgpu, vgtif_reg(avail_rs.nonmappable_gmadr.base)) =
		vgpu_hidden_gmadr_base(vgpu);
	vgpu_vreg(vgpu, vgtif_reg(avail_rs.nonmappable_gmadr.size)) =
		vgpu_hidden_sz(vgpu);

	vgpu_vreg(vgpu, vgtif_reg(avail_rs.fence_num)) = vgpu_fence_sz(vgpu);

	gvt_dbg_core("Populate PVINFO PAGE for vGPU %d\n", vgpu->id);
	gvt_dbg_core("aperture base [GMADR] 0x%llx size 0x%llx\n",
		vgpu_aperture_gmadr_base(vgpu), vgpu_aperture_sz(vgpu));
	gvt_dbg_core("hidden base [GMADR] 0x%llx size=0x%llx\n",
		vgpu_hidden_gmadr_base(vgpu), vgpu_hidden_sz(vgpu));
	gvt_dbg_core("fence size %d\n", vgpu_fence_sz(vgpu));

	WARN_ON(sizeof(struct vgt_if) != VGT_PVINFO_SIZE);
}

/**
 * intel_gvt_init_vgpu_types - initialize vGPU type list
 * @gvt : GVT device
 *
 * Initialize vGPU type list based on available resource.
 *
 */
int intel_gvt_init_vgpu_types(struct intel_gvt *gvt)
{
	unsigned int num_types;
	unsigned int i, low_avail;
	unsigned int min_low;

	/* vGPU type name is defined as GVTg_Vx_y which contains
	 * physical GPU generation type and 'y' means maximum vGPU
	 * instances user can create on one physical GPU for this
	 * type.
	 *
	 * Depend on physical SKU resource, might see vGPU types like
	 * GVTg_V4_8, GVTg_V4_4, GVTg_V4_2, etc. We can create
	 * different types of vGPU on same physical GPU depending on
	 * available resource. Each vGPU type will have "avail_instance"
	 * to indicate how many vGPU instance can be created for this
	 * type.
	 *
	 * Currently use static size here as we init type earlier..
	 */
	low_avail = MB_TO_BYTES(256) - HOST_LOW_GM_SIZE;
	num_types = 4;

	gvt->types = kzalloc(num_types * sizeof(struct intel_vgpu_type),
			     GFP_KERNEL);
	if (!gvt->types)
		return -ENOMEM;

	min_low = MB_TO_BYTES(32);
	for (i = 0; i < num_types; ++i) {
		if (low_avail / min_low == 0)
			break;
		gvt->types[i].low_gm_size = min_low;
		gvt->types[i].high_gm_size = 3 * gvt->types[i].low_gm_size;
		gvt->types[i].fence = 4;
		gvt->types[i].max_instance = low_avail / min_low;
		gvt->types[i].avail_instance = gvt->types[i].max_instance;

		if (IS_GEN8(gvt->dev_priv))
			sprintf(gvt->types[i].name, "GVTg_V4_%u",
						gvt->types[i].max_instance);
		else if (IS_GEN9(gvt->dev_priv))
			sprintf(gvt->types[i].name, "GVTg_V5_%u",
						gvt->types[i].max_instance);

		min_low <<= 1;
		gvt_dbg_core("type[%d]: %s max %u avail %u low %u high %u fence %u\n",
			     i, gvt->types[i].name, gvt->types[i].max_instance,
			     gvt->types[i].avail_instance,
			     gvt->types[i].low_gm_size,
			     gvt->types[i].high_gm_size, gvt->types[i].fence);
	}

	gvt->num_types = i;
	return 0;
}

void intel_gvt_clean_vgpu_types(struct intel_gvt *gvt)
{
	kfree(gvt->types);
}

static void intel_gvt_update_vgpu_types(struct intel_gvt *gvt)
{
	int i;
	unsigned int low_gm_avail, high_gm_avail, fence_avail;
	unsigned int low_gm_min, high_gm_min, fence_min, total_min;

	/* Need to depend on maxium hw resource size but keep on
	 * static config for now.
	 */
	low_gm_avail = MB_TO_BYTES(256) - HOST_LOW_GM_SIZE -
		gvt->gm.vgpu_allocated_low_gm_size;
	high_gm_avail = MB_TO_BYTES(256) * 3 - HOST_HIGH_GM_SIZE -
		gvt->gm.vgpu_allocated_high_gm_size;
	fence_avail = gvt_fence_sz(gvt) - HOST_FENCE -
		gvt->fence.vgpu_allocated_fence_num;

	for (i = 0; i < gvt->num_types; i++) {
		low_gm_min = low_gm_avail / gvt->types[i].low_gm_size;
		high_gm_min = high_gm_avail / gvt->types[i].high_gm_size;
		fence_min = fence_avail / gvt->types[i].fence;
		total_min = min(min(low_gm_min, high_gm_min), fence_min);
		gvt->types[i].avail_instance = min(gvt->types[i].max_instance,
						   total_min);

		gvt_dbg_core("update type[%d]: %s max %u avail %u low %u high %u fence %u\n",
		       i, gvt->types[i].name, gvt->types[i].max_instance,
		       gvt->types[i].avail_instance, gvt->types[i].low_gm_size,
		       gvt->types[i].high_gm_size, gvt->types[i].fence);
	}
}

/**
 * intel_gvt_destroy_vgpu - destroy a virtual GPU
 * @vgpu: virtual GPU
 *
 * This function is called when user wants to destroy a virtual GPU.
 *
 */
void intel_gvt_destroy_vgpu(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;

	mutex_lock(&gvt->lock);

	vgpu->active = false;
	idr_remove(&gvt->vgpu_idr, vgpu->id);

	if (atomic_read(&vgpu->running_workload_num)) {
		mutex_unlock(&gvt->lock);
		intel_gvt_wait_vgpu_idle(vgpu);
		mutex_lock(&gvt->lock);
	}

	intel_vgpu_stop_schedule(vgpu);
	intel_vgpu_clean_sched_policy(vgpu);
	intel_vgpu_clean_gvt_context(vgpu);
	intel_vgpu_clean_execlist(vgpu);
	intel_vgpu_clean_display(vgpu);
	intel_vgpu_clean_opregion(vgpu);
	intel_vgpu_clean_gtt(vgpu);
	intel_gvt_hypervisor_detach_vgpu(vgpu);
	intel_vgpu_free_resource(vgpu);
	clean_vgpu_mmio(vgpu);
	vfree(vgpu);

	intel_gvt_update_vgpu_types(gvt);
	mutex_unlock(&gvt->lock);
}

static struct intel_vgpu *__intel_gvt_create_vgpu(struct intel_gvt *gvt,
		struct intel_vgpu_creation_params *param)
{
	struct intel_vgpu *vgpu;
	int ret;

	gvt_dbg_core("handle %llu low %llu MB high %llu MB fence %llu\n",
			param->handle, param->low_gm_sz, param->high_gm_sz,
			param->fence_sz);

	vgpu = vzalloc(sizeof(*vgpu));
	if (!vgpu)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&gvt->lock);

	ret = idr_alloc(&gvt->vgpu_idr, vgpu, 1, GVT_MAX_VGPU, GFP_KERNEL);
	if (ret < 0)
		goto out_free_vgpu;

	vgpu->id = ret;
	vgpu->handle = param->handle;
	vgpu->gvt = gvt;
	bitmap_zero(vgpu->tlb_handle_pending, I915_NUM_ENGINES);

	setup_vgpu_cfg_space(vgpu, param);

	ret = setup_vgpu_mmio(vgpu);
	if (ret)
		goto out_free_vgpu;

	ret = intel_vgpu_alloc_resource(vgpu, param);
	if (ret)
		goto out_clean_vgpu_mmio;

	populate_pvinfo_page(vgpu);

	ret = intel_gvt_hypervisor_attach_vgpu(vgpu);
	if (ret)
		goto out_clean_vgpu_resource;

	ret = intel_vgpu_init_gtt(vgpu);
	if (ret)
		goto out_detach_hypervisor_vgpu;

	ret = intel_vgpu_init_display(vgpu);
	if (ret)
		goto out_clean_gtt;

	ret = intel_vgpu_init_execlist(vgpu);
	if (ret)
		goto out_clean_display;

	ret = intel_vgpu_init_gvt_context(vgpu);
	if (ret)
		goto out_clean_execlist;

	ret = intel_vgpu_init_sched_policy(vgpu);
	if (ret)
		goto out_clean_shadow_ctx;

	vgpu->active = true;
	mutex_unlock(&gvt->lock);

	return vgpu;

out_clean_shadow_ctx:
	intel_vgpu_clean_gvt_context(vgpu);
out_clean_execlist:
	intel_vgpu_clean_execlist(vgpu);
out_clean_display:
	intel_vgpu_clean_display(vgpu);
out_clean_gtt:
	intel_vgpu_clean_gtt(vgpu);
out_detach_hypervisor_vgpu:
	intel_gvt_hypervisor_detach_vgpu(vgpu);
out_clean_vgpu_resource:
	intel_vgpu_free_resource(vgpu);
out_clean_vgpu_mmio:
	clean_vgpu_mmio(vgpu);
out_free_vgpu:
	vfree(vgpu);
	mutex_unlock(&gvt->lock);
	return ERR_PTR(ret);
}

/**
 * intel_gvt_create_vgpu - create a virtual GPU
 * @gvt: GVT device
 * @type: type of the vGPU to create
 *
 * This function is called when user wants to create a virtual GPU.
 *
 * Returns:
 * pointer to intel_vgpu, error pointer if failed.
 */
struct intel_vgpu *intel_gvt_create_vgpu(struct intel_gvt *gvt,
				struct intel_vgpu_type *type)
{
	struct intel_vgpu_creation_params param;
	struct intel_vgpu *vgpu;

	param.handle = 0;
	param.low_gm_sz = type->low_gm_size;
	param.high_gm_sz = type->high_gm_size;
	param.fence_sz = type->fence;

	/* XXX current param based on MB */
	param.low_gm_sz = BYTES_TO_MB(param.low_gm_sz);
	param.high_gm_sz = BYTES_TO_MB(param.high_gm_sz);

	vgpu = __intel_gvt_create_vgpu(gvt, &param);
	if (IS_ERR(vgpu))
		return vgpu;

	/* calculate left instance change for types */
	intel_gvt_update_vgpu_types(gvt);

	return vgpu;
}
