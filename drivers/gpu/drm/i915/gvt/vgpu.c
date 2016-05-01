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

static void clean_vgpu_mmio(struct intel_vgpu *vgpu)
{
	vfree(vgpu->mmio.vreg);
	vgpu->mmio.vreg = vgpu->mmio.sreg = NULL;
}

static int setup_vgpu_mmio(struct intel_vgpu *vgpu)
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

static void populate_pvinfo_page(struct intel_vgpu *vgpu)
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

	intel_vgpu_clean_gvt_context(vgpu);
	intel_vgpu_clean_execlist(vgpu);
	intel_vgpu_clean_display(vgpu);
	intel_vgpu_clean_opregion(vgpu);
	intel_vgpu_clean_gtt(vgpu);
	intel_gvt_hypervisor_detach_vgpu(vgpu);
	intel_vgpu_free_resource(vgpu);
	clean_vgpu_mmio(vgpu);
	vfree(vgpu);

	mutex_unlock(&gvt->lock);
}

/**
 * intel_gvt_create_vgpu - create a virtual GPU
 * @gvt: GVT device
 * @param: vGPU creation parameters
 *
 * This function is called when user wants to create a virtual GPU.
 *
 * Returns:
 * pointer to intel_vgpu, error pointer if failed.
 */
struct intel_vgpu *intel_gvt_create_vgpu(struct intel_gvt *gvt,
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

	if (intel_gvt_host.hypervisor_type == INTEL_GVT_HYPERVISOR_KVM) {
		ret = intel_vgpu_init_opregion(vgpu, 0);
		if (ret)
			goto out_clean_gtt;
	}

	ret = intel_vgpu_init_display(vgpu);
	if (ret)
		goto out_clean_opregion;

	ret = intel_vgpu_init_execlist(vgpu);
	if (ret)
		goto out_clean_display;

	ret = intel_vgpu_init_gvt_context(vgpu);
	if (ret)
		goto out_clean_execlist;

	vgpu->active = true;
	mutex_unlock(&gvt->lock);

	return vgpu;

out_clean_execlist:
	intel_vgpu_clean_execlist(vgpu);
out_clean_display:
	intel_vgpu_clean_display(vgpu);
out_clean_opregion:
	intel_vgpu_clean_opregion(vgpu);
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
