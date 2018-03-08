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

void populate_pvinfo_page(struct intel_vgpu *vgpu)
{
	/* setup the ballooning information */
	vgpu_vreg64_t(vgpu, vgtif_reg(magic)) = VGT_MAGIC;
	vgpu_vreg_t(vgpu, vgtif_reg(version_major)) = 1;
	vgpu_vreg_t(vgpu, vgtif_reg(version_minor)) = 0;
	vgpu_vreg_t(vgpu, vgtif_reg(display_ready)) = 0;
	vgpu_vreg_t(vgpu, vgtif_reg(vgt_id)) = vgpu->id;

	vgpu_vreg_t(vgpu, vgtif_reg(vgt_caps)) = VGT_CAPS_FULL_48BIT_PPGTT;
	vgpu_vreg_t(vgpu, vgtif_reg(vgt_caps)) |= VGT_CAPS_HWSP_EMULATION;

	vgpu_vreg_t(vgpu, vgtif_reg(avail_rs.mappable_gmadr.base)) =
		vgpu_aperture_gmadr_base(vgpu);
	vgpu_vreg_t(vgpu, vgtif_reg(avail_rs.mappable_gmadr.size)) =
		vgpu_aperture_sz(vgpu);
	vgpu_vreg_t(vgpu, vgtif_reg(avail_rs.nonmappable_gmadr.base)) =
		vgpu_hidden_gmadr_base(vgpu);
	vgpu_vreg_t(vgpu, vgtif_reg(avail_rs.nonmappable_gmadr.size)) =
		vgpu_hidden_sz(vgpu);

	vgpu_vreg_t(vgpu, vgtif_reg(avail_rs.fence_num)) = vgpu_fence_sz(vgpu);

	gvt_dbg_core("Populate PVINFO PAGE for vGPU %d\n", vgpu->id);
	gvt_dbg_core("aperture base [GMADR] 0x%llx size 0x%llx\n",
		vgpu_aperture_gmadr_base(vgpu), vgpu_aperture_sz(vgpu));
	gvt_dbg_core("hidden base [GMADR] 0x%llx size=0x%llx\n",
		vgpu_hidden_gmadr_base(vgpu), vgpu_hidden_sz(vgpu));
	gvt_dbg_core("fence size %d\n", vgpu_fence_sz(vgpu));

	WARN_ON(sizeof(struct vgt_if) != VGT_PVINFO_SIZE);
}

#define VGPU_MAX_WEIGHT 16
#define VGPU_WEIGHT(vgpu_num)	\
	(VGPU_MAX_WEIGHT / (vgpu_num))

static struct {
	unsigned int low_mm;
	unsigned int high_mm;
	unsigned int fence;

	/* A vGPU with a weight of 8 will get twice as much GPU as a vGPU
	 * with a weight of 4 on a contended host, different vGPU type has
	 * different weight set. Legal weights range from 1 to 16.
	 */
	unsigned int weight;
	enum intel_vgpu_edid edid;
	char *name;
} vgpu_types[] = {
/* Fixed vGPU type table */
	{ MB_TO_BYTES(64), MB_TO_BYTES(384), 4, VGPU_WEIGHT(8), GVT_EDID_1024_768, "8" },
	{ MB_TO_BYTES(128), MB_TO_BYTES(512), 4, VGPU_WEIGHT(4), GVT_EDID_1920_1200, "4" },
	{ MB_TO_BYTES(256), MB_TO_BYTES(1024), 4, VGPU_WEIGHT(2), GVT_EDID_1920_1200, "2" },
	{ MB_TO_BYTES(512), MB_TO_BYTES(2048), 4, VGPU_WEIGHT(1), GVT_EDID_1920_1200, "1" },
};

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
	unsigned int i, low_avail, high_avail;
	unsigned int min_low;

	/* vGPU type name is defined as GVTg_Vx_y which contains
	 * physical GPU generation type (e.g V4 as BDW server, V5 as
	 * SKL server).
	 *
	 * Depend on physical SKU resource, might see vGPU types like
	 * GVTg_V4_8, GVTg_V4_4, GVTg_V4_2, etc. We can create
	 * different types of vGPU on same physical GPU depending on
	 * available resource. Each vGPU type will have "avail_instance"
	 * to indicate how many vGPU instance can be created for this
	 * type.
	 *
	 */
	low_avail = gvt_aperture_sz(gvt) - HOST_LOW_GM_SIZE;
	high_avail = gvt_hidden_sz(gvt) - HOST_HIGH_GM_SIZE;
	num_types = sizeof(vgpu_types) / sizeof(vgpu_types[0]);

	gvt->types = kzalloc(num_types * sizeof(struct intel_vgpu_type),
			     GFP_KERNEL);
	if (!gvt->types)
		return -ENOMEM;

	min_low = MB_TO_BYTES(32);
	for (i = 0; i < num_types; ++i) {
		if (low_avail / vgpu_types[i].low_mm == 0)
			break;

		gvt->types[i].low_gm_size = vgpu_types[i].low_mm;
		gvt->types[i].high_gm_size = vgpu_types[i].high_mm;
		gvt->types[i].fence = vgpu_types[i].fence;

		if (vgpu_types[i].weight < 1 ||
					vgpu_types[i].weight > VGPU_MAX_WEIGHT)
			return -EINVAL;

		gvt->types[i].weight = vgpu_types[i].weight;
		gvt->types[i].resolution = vgpu_types[i].edid;
		gvt->types[i].avail_instance = min(low_avail / vgpu_types[i].low_mm,
						   high_avail / vgpu_types[i].high_mm);

		if (IS_GEN8(gvt->dev_priv))
			sprintf(gvt->types[i].name, "GVTg_V4_%s",
						vgpu_types[i].name);
		else if (IS_GEN9(gvt->dev_priv))
			sprintf(gvt->types[i].name, "GVTg_V5_%s",
						vgpu_types[i].name);

		gvt_dbg_core("type[%d]: %s avail %u low %u high %u fence %u weight %u res %s\n",
			     i, gvt->types[i].name,
			     gvt->types[i].avail_instance,
			     gvt->types[i].low_gm_size,
			     gvt->types[i].high_gm_size, gvt->types[i].fence,
			     gvt->types[i].weight,
			     vgpu_edid_str(gvt->types[i].resolution));
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
	unsigned int low_gm_min, high_gm_min, fence_min;

	/* Need to depend on maxium hw resource size but keep on
	 * static config for now.
	 */
	low_gm_avail = gvt_aperture_sz(gvt) - HOST_LOW_GM_SIZE -
		gvt->gm.vgpu_allocated_low_gm_size;
	high_gm_avail = gvt_hidden_sz(gvt) - HOST_HIGH_GM_SIZE -
		gvt->gm.vgpu_allocated_high_gm_size;
	fence_avail = gvt_fence_sz(gvt) - HOST_FENCE -
		gvt->fence.vgpu_allocated_fence_num;

	for (i = 0; i < gvt->num_types; i++) {
		low_gm_min = low_gm_avail / gvt->types[i].low_gm_size;
		high_gm_min = high_gm_avail / gvt->types[i].high_gm_size;
		fence_min = fence_avail / gvt->types[i].fence;
		gvt->types[i].avail_instance = min(min(low_gm_min, high_gm_min),
						   fence_min);

		gvt_dbg_core("update type[%d]: %s avail %u low %u high %u fence %u\n",
		       i, gvt->types[i].name,
		       gvt->types[i].avail_instance, gvt->types[i].low_gm_size,
		       gvt->types[i].high_gm_size, gvt->types[i].fence);
	}
}

/**
 * intel_gvt_active_vgpu - activate a virtual GPU
 * @vgpu: virtual GPU
 *
 * This function is called when user wants to activate a virtual GPU.
 *
 */
void intel_gvt_activate_vgpu(struct intel_vgpu *vgpu)
{
	mutex_lock(&vgpu->gvt->lock);
	vgpu->active = true;
	mutex_unlock(&vgpu->gvt->lock);
}

/**
 * intel_gvt_deactive_vgpu - deactivate a virtual GPU
 * @vgpu: virtual GPU
 *
 * This function is called when user wants to deactivate a virtual GPU.
 * All virtual GPU runtime information will be destroyed.
 *
 */
void intel_gvt_deactivate_vgpu(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;

	mutex_lock(&gvt->lock);

	vgpu->active = false;

	if (atomic_read(&vgpu->submission.running_workload_num)) {
		mutex_unlock(&gvt->lock);
		intel_gvt_wait_vgpu_idle(vgpu);
		mutex_lock(&gvt->lock);
	}

	intel_vgpu_stop_schedule(vgpu);
	intel_vgpu_dmabuf_cleanup(vgpu);

	mutex_unlock(&gvt->lock);
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

	WARN(vgpu->active, "vGPU is still active!\n");

	intel_gvt_debugfs_remove_vgpu(vgpu);
	idr_remove(&gvt->vgpu_idr, vgpu->id);
	if (idr_is_empty(&gvt->vgpu_idr))
		intel_gvt_clean_irq(gvt);
	intel_vgpu_clean_sched_policy(vgpu);
	intel_vgpu_clean_submission(vgpu);
	intel_vgpu_clean_display(vgpu);
	intel_vgpu_clean_opregion(vgpu);
	intel_vgpu_clean_gtt(vgpu);
	intel_gvt_hypervisor_detach_vgpu(vgpu);
	intel_vgpu_free_resource(vgpu);
	intel_vgpu_clean_mmio(vgpu);
	intel_vgpu_dmabuf_cleanup(vgpu);
	vfree(vgpu);

	intel_gvt_update_vgpu_types(gvt);
	mutex_unlock(&gvt->lock);
}

#define IDLE_VGPU_IDR 0

/**
 * intel_gvt_create_idle_vgpu - create an idle virtual GPU
 * @gvt: GVT device
 *
 * This function is called when user wants to create an idle virtual GPU.
 *
 * Returns:
 * pointer to intel_vgpu, error pointer if failed.
 */
struct intel_vgpu *intel_gvt_create_idle_vgpu(struct intel_gvt *gvt)
{
	struct intel_vgpu *vgpu;
	enum intel_engine_id i;
	int ret;

	vgpu = vzalloc(sizeof(*vgpu));
	if (!vgpu)
		return ERR_PTR(-ENOMEM);

	vgpu->id = IDLE_VGPU_IDR;
	vgpu->gvt = gvt;

	for (i = 0; i < I915_NUM_ENGINES; i++)
		INIT_LIST_HEAD(&vgpu->submission.workload_q_head[i]);

	ret = intel_vgpu_init_sched_policy(vgpu);
	if (ret)
		goto out_free_vgpu;

	vgpu->active = false;

	return vgpu;

out_free_vgpu:
	vfree(vgpu);
	return ERR_PTR(ret);
}

/**
 * intel_gvt_destroy_vgpu - destroy an idle virtual GPU
 * @vgpu: virtual GPU
 *
 * This function is called when user wants to destroy an idle virtual GPU.
 *
 */
void intel_gvt_destroy_idle_vgpu(struct intel_vgpu *vgpu)
{
	intel_vgpu_clean_sched_policy(vgpu);
	vfree(vgpu);
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

	ret = idr_alloc(&gvt->vgpu_idr, vgpu, IDLE_VGPU_IDR + 1, GVT_MAX_VGPU,
		GFP_KERNEL);
	if (ret < 0)
		goto out_free_vgpu;

	vgpu->id = ret;
	vgpu->handle = param->handle;
	vgpu->gvt = gvt;
	vgpu->sched_ctl.weight = param->weight;
	INIT_LIST_HEAD(&vgpu->dmabuf_obj_list_head);
	idr_init(&vgpu->object_idr);
	intel_vgpu_init_cfg_space(vgpu, param->primary);

	ret = intel_vgpu_init_mmio(vgpu);
	if (ret)
		goto out_clean_idr;

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

	ret = intel_vgpu_init_opregion(vgpu);
	if (ret)
		goto out_clean_gtt;

	ret = intel_vgpu_init_display(vgpu, param->resolution);
	if (ret)
		goto out_clean_opregion;

	ret = intel_vgpu_setup_submission(vgpu);
	if (ret)
		goto out_clean_display;

	ret = intel_vgpu_init_sched_policy(vgpu);
	if (ret)
		goto out_clean_submission;

	ret = intel_gvt_debugfs_add_vgpu(vgpu);
	if (ret)
		goto out_clean_sched_policy;

	ret = intel_gvt_hypervisor_set_opregion(vgpu);
	if (ret)
		goto out_clean_sched_policy;

	mutex_unlock(&gvt->lock);

	return vgpu;

out_clean_sched_policy:
	intel_vgpu_clean_sched_policy(vgpu);
out_clean_submission:
	intel_vgpu_clean_submission(vgpu);
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
	intel_vgpu_clean_mmio(vgpu);
out_clean_idr:
	idr_remove(&gvt->vgpu_idr, vgpu->id);
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
	param.primary = 1;
	param.low_gm_sz = type->low_gm_size;
	param.high_gm_sz = type->high_gm_size;
	param.fence_sz = type->fence;
	param.weight = type->weight;
	param.resolution = type->resolution;

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

/**
 * intel_gvt_reset_vgpu_locked - reset a virtual GPU by DMLR or GT reset
 * @vgpu: virtual GPU
 * @dmlr: vGPU Device Model Level Reset or GT Reset
 * @engine_mask: engines to reset for GT reset
 *
 * This function is called when user wants to reset a virtual GPU through
 * device model reset or GT reset. The caller should hold the gvt lock.
 *
 * vGPU Device Model Level Reset (DMLR) simulates the PCI level reset to reset
 * the whole vGPU to default state as when it is created. This vGPU function
 * is required both for functionary and security concerns.The ultimate goal
 * of vGPU FLR is that reuse a vGPU instance by virtual machines. When we
 * assign a vGPU to a virtual machine we must isse such reset first.
 *
 * Full GT Reset and Per-Engine GT Reset are soft reset flow for GPU engines
 * (Render, Blitter, Video, Video Enhancement). It is defined by GPU Spec.
 * Unlike the FLR, GT reset only reset particular resource of a vGPU per
 * the reset request. Guest driver can issue a GT reset by programming the
 * virtual GDRST register to reset specific virtual GPU engine or all
 * engines.
 *
 * The parameter dev_level is to identify if we will do DMLR or GT reset.
 * The parameter engine_mask is to specific the engines that need to be
 * resetted. If value ALL_ENGINES is given for engine_mask, it means
 * the caller requests a full GT reset that we will reset all virtual
 * GPU engines. For FLR, engine_mask is ignored.
 */
void intel_gvt_reset_vgpu_locked(struct intel_vgpu *vgpu, bool dmlr,
				 unsigned int engine_mask)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	unsigned int resetting_eng = dmlr ? ALL_ENGINES : engine_mask;

	gvt_dbg_core("------------------------------------------\n");
	gvt_dbg_core("resseting vgpu%d, dmlr %d, engine_mask %08x\n",
		     vgpu->id, dmlr, engine_mask);

	vgpu->resetting_eng = resetting_eng;

	intel_vgpu_stop_schedule(vgpu);
	/*
	 * The current_vgpu will set to NULL after stopping the
	 * scheduler when the reset is triggered by current vgpu.
	 */
	if (scheduler->current_vgpu == NULL) {
		mutex_unlock(&gvt->lock);
		intel_gvt_wait_vgpu_idle(vgpu);
		mutex_lock(&gvt->lock);
	}

	intel_vgpu_reset_submission(vgpu, resetting_eng);
	/* full GPU reset or device model level reset */
	if (engine_mask == ALL_ENGINES || dmlr) {
		intel_vgpu_select_submission_ops(vgpu, ALL_ENGINES, 0);
		/*fence will not be reset during virtual reset */
		if (dmlr) {
			intel_vgpu_reset_gtt(vgpu);
			intel_vgpu_reset_resource(vgpu);
		}

		intel_vgpu_reset_mmio(vgpu, dmlr);
		populate_pvinfo_page(vgpu);
		intel_vgpu_reset_display(vgpu);

		if (dmlr) {
			intel_vgpu_reset_cfg_space(vgpu);
			/* only reset the failsafe mode when dmlr reset */
			vgpu->failsafe = false;
			vgpu->pv_notified = false;
		}
	}

	vgpu->resetting_eng = 0;
	gvt_dbg_core("reset vgpu%d done\n", vgpu->id);
	gvt_dbg_core("------------------------------------------\n");
}

/**
 * intel_gvt_reset_vgpu - reset a virtual GPU (Function Level)
 * @vgpu: virtual GPU
 *
 * This function is called when user wants to reset a virtual GPU.
 *
 */
void intel_gvt_reset_vgpu(struct intel_vgpu *vgpu)
{
	mutex_lock(&vgpu->gvt->lock);
	intel_gvt_reset_vgpu_locked(vgpu, true, 0);
	mutex_unlock(&vgpu->gvt->lock);
}
