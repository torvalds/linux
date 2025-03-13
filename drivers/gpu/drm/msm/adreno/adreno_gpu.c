// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 */

#include <linux/ascii85.h>
#include <linux/interconnect.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/nvmem-consumer.h>
#include <soc/qcom/ocmem.h>
#include "adreno_gpu.h"
#include "a6xx_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"

static u64 address_space_size = 0;
MODULE_PARM_DESC(address_space_size, "Override for size of processes private GPU address space");
module_param(address_space_size, ullong, 0600);

static bool zap_available = true;

static int zap_shader_load_mdt(struct msm_gpu *gpu, const char *fwname,
		u32 pasid)
{
	struct device *dev = &gpu->pdev->dev;
	const struct firmware *fw;
	const char *signed_fwname = NULL;
	struct device_node *np, *mem_np;
	struct resource r;
	phys_addr_t mem_phys;
	ssize_t mem_size;
	void *mem_region = NULL;
	int ret;

	if (!IS_ENABLED(CONFIG_ARCH_QCOM)) {
		zap_available = false;
		return -EINVAL;
	}

	np = of_get_child_by_name(dev->of_node, "zap-shader");
	if (!of_device_is_available(np)) {
		zap_available = false;
		return -ENODEV;
	}

	mem_np = of_parse_phandle(np, "memory-region", 0);
	of_node_put(np);
	if (!mem_np) {
		zap_available = false;
		return -EINVAL;
	}

	ret = of_address_to_resource(mem_np, 0, &r);
	of_node_put(mem_np);
	if (ret)
		return ret;

	mem_phys = r.start;

	/*
	 * Check for a firmware-name property.  This is the new scheme
	 * to handle firmware that may be signed with device specific
	 * keys, allowing us to have a different zap fw path for different
	 * devices.
	 *
	 * If the firmware-name property is found, we bypass the
	 * adreno_request_fw() mechanism, because we don't need to handle
	 * the /lib/firmware/qcom/... vs /lib/firmware/... case.
	 *
	 * If the firmware-name property is not found, for backwards
	 * compatibility we fall back to the fwname from the gpulist
	 * table.
	 */
	of_property_read_string_index(np, "firmware-name", 0, &signed_fwname);
	if (signed_fwname) {
		fwname = signed_fwname;
		ret = request_firmware_direct(&fw, fwname, gpu->dev->dev);
		if (ret)
			fw = ERR_PTR(ret);
	} else if (fwname) {
		/* Request the MDT file from the default location: */
		fw = adreno_request_fw(to_adreno_gpu(gpu), fwname);
	} else {
		/*
		 * For new targets, we require the firmware-name property,
		 * if a zap-shader is required, rather than falling back
		 * to a firmware name specified in gpulist.
		 *
		 * Because the firmware is signed with a (potentially)
		 * device specific key, having the name come from gpulist
		 * was a bad idea, and is only provided for backwards
		 * compatibility for older targets.
		 */
		return -ENOENT;
	}

	if (IS_ERR(fw)) {
		DRM_DEV_ERROR(dev, "Unable to load %s\n", fwname);
		return PTR_ERR(fw);
	}

	/* Figure out how much memory we need */
	mem_size = qcom_mdt_get_size(fw);
	if (mem_size < 0) {
		ret = mem_size;
		goto out;
	}

	if (mem_size > resource_size(&r)) {
		DRM_DEV_ERROR(dev,
			"memory region is too small to load the MDT\n");
		ret = -E2BIG;
		goto out;
	}

	/* Allocate memory for the firmware image */
	mem_region = memremap(mem_phys, mem_size,  MEMREMAP_WC);
	if (!mem_region) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Load the rest of the MDT
	 *
	 * Note that we could be dealing with two different paths, since
	 * with upstream linux-firmware it would be in a qcom/ subdir..
	 * adreno_request_fw() handles this, but qcom_mdt_load() does
	 * not.  But since we've already gotten through adreno_request_fw()
	 * we know which of the two cases it is:
	 */
	if (signed_fwname || (to_adreno_gpu(gpu)->fwloc == FW_LOCATION_LEGACY)) {
		ret = qcom_mdt_load(dev, fw, fwname, pasid,
				mem_region, mem_phys, mem_size, NULL);
	} else {
		char *newname;

		newname = kasprintf(GFP_KERNEL, "qcom/%s", fwname);

		ret = qcom_mdt_load(dev, fw, newname, pasid,
				mem_region, mem_phys, mem_size, NULL);
		kfree(newname);
	}
	if (ret)
		goto out;

	/* Send the image to the secure world */
	ret = qcom_scm_pas_auth_and_reset(pasid);

	/*
	 * If the scm call returns -EOPNOTSUPP we assume that this target
	 * doesn't need/support the zap shader so quietly fail
	 */
	if (ret == -EOPNOTSUPP)
		zap_available = false;
	else if (ret)
		DRM_DEV_ERROR(dev, "Unable to authorize the image\n");

out:
	if (mem_region)
		memunmap(mem_region);

	release_firmware(fw);

	return ret;
}

int adreno_zap_shader_load(struct msm_gpu *gpu, u32 pasid)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct platform_device *pdev = gpu->pdev;

	/* Short cut if we determine the zap shader isn't available/needed */
	if (!zap_available)
		return -ENODEV;

	/* We need SCM to be able to load the firmware */
	if (!qcom_scm_is_available()) {
		DRM_DEV_ERROR(&pdev->dev, "SCM is not available\n");
		return -EPROBE_DEFER;
	}

	return zap_shader_load_mdt(gpu, adreno_gpu->info->zapfw, pasid);
}

struct msm_gem_address_space *
adreno_create_address_space(struct msm_gpu *gpu,
			    struct platform_device *pdev)
{
	return adreno_iommu_create_address_space(gpu, pdev, 0);
}

struct msm_gem_address_space *
adreno_iommu_create_address_space(struct msm_gpu *gpu,
				  struct platform_device *pdev,
				  unsigned long quirks)
{
	struct iommu_domain_geometry *geometry;
	struct msm_mmu *mmu;
	struct msm_gem_address_space *aspace;
	u64 start, size;

	mmu = msm_iommu_gpu_new(&pdev->dev, gpu, quirks);
	if (IS_ERR_OR_NULL(mmu))
		return ERR_CAST(mmu);

	geometry = msm_iommu_get_geometry(mmu);
	if (IS_ERR(geometry))
		return ERR_CAST(geometry);

	/*
	 * Use the aperture start or SZ_16M, whichever is greater. This will
	 * ensure that we align with the allocated pagetable range while still
	 * allowing room in the lower 32 bits for GMEM and whatnot
	 */
	start = max_t(u64, SZ_16M, geometry->aperture_start);
	size = geometry->aperture_end - start + 1;

	aspace = msm_gem_address_space_create(mmu, "gpu",
		start & GENMASK_ULL(48, 0), size);

	if (IS_ERR(aspace) && !IS_ERR(mmu))
		mmu->funcs->destroy(mmu);

	return aspace;
}

u64 adreno_private_address_space_size(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);

	if (address_space_size)
		return address_space_size;

	if (adreno_gpu->info->address_space_size)
		return adreno_gpu->info->address_space_size;

	return SZ_4G;
}

#define ARM_SMMU_FSR_TF                 BIT(1)
#define ARM_SMMU_FSR_PF			BIT(3)
#define ARM_SMMU_FSR_EF			BIT(4)

int adreno_fault_handler(struct msm_gpu *gpu, unsigned long iova, int flags,
			 struct adreno_smmu_fault_info *info, const char *block,
			 u32 scratch[4])
{
	const char *type = "UNKNOWN";
	bool do_devcoredump = info && !READ_ONCE(gpu->crashstate);

	/*
	 * If we aren't going to be resuming later from fault_worker, then do
	 * it now.
	 */
	if (!do_devcoredump) {
		gpu->aspace->mmu->funcs->resume_translation(gpu->aspace->mmu);
	}

	/*
	 * Print a default message if we couldn't get the data from the
	 * adreno-smmu-priv
	 */
	if (!info) {
		pr_warn_ratelimited("*** gpu fault: iova=%.16lx flags=%d (%u,%u,%u,%u)\n",
			iova, flags,
			scratch[0], scratch[1], scratch[2], scratch[3]);

		return 0;
	}

	if (info->fsr & ARM_SMMU_FSR_TF)
		type = "TRANSLATION";
	else if (info->fsr & ARM_SMMU_FSR_PF)
		type = "PERMISSION";
	else if (info->fsr & ARM_SMMU_FSR_EF)
		type = "EXTERNAL";

	pr_warn_ratelimited("*** gpu fault: ttbr0=%.16llx iova=%.16lx dir=%s type=%s source=%s (%u,%u,%u,%u)\n",
			info->ttbr0, iova,
			flags & IOMMU_FAULT_WRITE ? "WRITE" : "READ",
			type, block,
			scratch[0], scratch[1], scratch[2], scratch[3]);

	if (do_devcoredump) {
		/* Turn off the hangcheck timer to keep it from bothering us */
		del_timer(&gpu->hangcheck_timer);

		gpu->fault_info.ttbr0 = info->ttbr0;
		gpu->fault_info.iova  = iova;
		gpu->fault_info.flags = flags;
		gpu->fault_info.type  = type;
		gpu->fault_info.block = block;

		kthread_queue_work(gpu->worker, &gpu->fault_work);
	}

	return 0;
}

int adreno_get_param(struct msm_gpu *gpu, struct msm_file_private *ctx,
		     uint32_t param, uint64_t *value, uint32_t *len)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct drm_device *drm = gpu->dev;

	/* No pointer params yet */
	if (*len != 0)
		return UERR(EINVAL, drm, "invalid len");

	switch (param) {
	case MSM_PARAM_GPU_ID:
		*value = adreno_gpu->info->revn;
		return 0;
	case MSM_PARAM_GMEM_SIZE:
		*value = adreno_gpu->info->gmem;
		return 0;
	case MSM_PARAM_GMEM_BASE:
		if (adreno_is_a650_family(adreno_gpu) ||
		    adreno_is_a740_family(adreno_gpu))
			*value = 0;
		else
			*value = 0x100000;
		return 0;
	case MSM_PARAM_CHIP_ID:
		*value = adreno_gpu->chip_id;
		if (!adreno_gpu->info->revn)
			*value |= ((uint64_t) adreno_gpu->speedbin) << 32;
		return 0;
	case MSM_PARAM_MAX_FREQ:
		*value = adreno_gpu->base.fast_rate;
		return 0;
	case MSM_PARAM_TIMESTAMP:
		if (adreno_gpu->funcs->get_timestamp) {
			int ret;

			pm_runtime_get_sync(&gpu->pdev->dev);
			ret = adreno_gpu->funcs->get_timestamp(gpu, value);
			pm_runtime_put_autosuspend(&gpu->pdev->dev);

			return ret;
		}
		return -EINVAL;
	case MSM_PARAM_PRIORITIES:
		*value = gpu->nr_rings * NR_SCHED_PRIORITIES;
		return 0;
	case MSM_PARAM_PP_PGTABLE:
		*value = 0;
		return 0;
	case MSM_PARAM_FAULTS:
		if (ctx->aspace)
			*value = gpu->global_faults + ctx->aspace->faults;
		else
			*value = gpu->global_faults;
		return 0;
	case MSM_PARAM_SUSPENDS:
		*value = gpu->suspend_count;
		return 0;
	case MSM_PARAM_VA_START:
		if (ctx->aspace == gpu->aspace)
			return UERR(EINVAL, drm, "requires per-process pgtables");
		*value = ctx->aspace->va_start;
		return 0;
	case MSM_PARAM_VA_SIZE:
		if (ctx->aspace == gpu->aspace)
			return UERR(EINVAL, drm, "requires per-process pgtables");
		*value = ctx->aspace->va_size;
		return 0;
	case MSM_PARAM_HIGHEST_BANK_BIT:
		*value = adreno_gpu->ubwc_config.highest_bank_bit;
		return 0;
	case MSM_PARAM_RAYTRACING:
		*value = adreno_gpu->has_ray_tracing;
		return 0;
	case MSM_PARAM_UBWC_SWIZZLE:
		*value = adreno_gpu->ubwc_config.ubwc_swizzle;
		return 0;
	case MSM_PARAM_MACROTILE_MODE:
		*value = adreno_gpu->ubwc_config.macrotile_mode;
		return 0;
	case MSM_PARAM_UCHE_TRAP_BASE:
		*value = adreno_gpu->uche_trap_base;
		return 0;
	default:
		return UERR(EINVAL, drm, "%s: invalid param: %u", gpu->name, param);
	}
}

int adreno_set_param(struct msm_gpu *gpu, struct msm_file_private *ctx,
		     uint32_t param, uint64_t value, uint32_t len)
{
	struct drm_device *drm = gpu->dev;

	switch (param) {
	case MSM_PARAM_COMM:
	case MSM_PARAM_CMDLINE:
		/* kstrdup_quotable_cmdline() limits to PAGE_SIZE, so
		 * that should be a reasonable upper bound
		 */
		if (len > PAGE_SIZE)
			return UERR(EINVAL, drm, "invalid len");
		break;
	default:
		if (len != 0)
			return UERR(EINVAL, drm, "invalid len");
	}

	switch (param) {
	case MSM_PARAM_COMM:
	case MSM_PARAM_CMDLINE: {
		char *str, **paramp;

		str = memdup_user_nul(u64_to_user_ptr(value), len);
		if (IS_ERR(str))
			return PTR_ERR(str);

		mutex_lock(&gpu->lock);

		if (param == MSM_PARAM_COMM) {
			paramp = &ctx->comm;
		} else {
			paramp = &ctx->cmdline;
		}

		kfree(*paramp);
		*paramp = str;

		mutex_unlock(&gpu->lock);

		return 0;
	}
	case MSM_PARAM_SYSPROF:
		if (!capable(CAP_SYS_ADMIN))
			return UERR(EPERM, drm, "invalid permissions");
		return msm_file_private_set_sysprof(ctx, gpu, value);
	default:
		return UERR(EINVAL, drm, "%s: invalid param: %u", gpu->name, param);
	}
}

const struct firmware *
adreno_request_fw(struct adreno_gpu *adreno_gpu, const char *fwname)
{
	struct drm_device *drm = adreno_gpu->base.dev;
	const struct firmware *fw = NULL;
	char *newname;
	int ret;

	newname = kasprintf(GFP_KERNEL, "qcom/%s", fwname);
	if (!newname)
		return ERR_PTR(-ENOMEM);

	/*
	 * Try first to load from qcom/$fwfile using a direct load (to avoid
	 * a potential timeout waiting for usermode helper)
	 */
	if ((adreno_gpu->fwloc == FW_LOCATION_UNKNOWN) ||
	    (adreno_gpu->fwloc == FW_LOCATION_NEW)) {

		ret = request_firmware_direct(&fw, newname, drm->dev);
		if (!ret) {
			DRM_DEV_INFO(drm->dev, "loaded %s from new location\n",
				newname);
			adreno_gpu->fwloc = FW_LOCATION_NEW;
			goto out;
		} else if (adreno_gpu->fwloc != FW_LOCATION_UNKNOWN) {
			DRM_DEV_ERROR(drm->dev, "failed to load %s: %d\n",
				newname, ret);
			fw = ERR_PTR(ret);
			goto out;
		}
	}

	/*
	 * Then try the legacy location without qcom/ prefix
	 */
	if ((adreno_gpu->fwloc == FW_LOCATION_UNKNOWN) ||
	    (adreno_gpu->fwloc == FW_LOCATION_LEGACY)) {

		ret = request_firmware_direct(&fw, fwname, drm->dev);
		if (!ret) {
			DRM_DEV_INFO(drm->dev, "loaded %s from legacy location\n",
				fwname);
			adreno_gpu->fwloc = FW_LOCATION_LEGACY;
			goto out;
		} else if (adreno_gpu->fwloc != FW_LOCATION_UNKNOWN) {
			DRM_DEV_ERROR(drm->dev, "failed to load %s: %d\n",
				fwname, ret);
			fw = ERR_PTR(ret);
			goto out;
		}
	}

	/*
	 * Finally fall back to request_firmware() for cases where the
	 * usermode helper is needed (I think mainly android)
	 */
	if ((adreno_gpu->fwloc == FW_LOCATION_UNKNOWN) ||
	    (adreno_gpu->fwloc == FW_LOCATION_HELPER)) {

		ret = request_firmware(&fw, newname, drm->dev);
		if (!ret) {
			DRM_DEV_INFO(drm->dev, "loaded %s with helper\n",
				newname);
			adreno_gpu->fwloc = FW_LOCATION_HELPER;
			goto out;
		} else if (adreno_gpu->fwloc != FW_LOCATION_UNKNOWN) {
			DRM_DEV_ERROR(drm->dev, "failed to load %s: %d\n",
				newname, ret);
			fw = ERR_PTR(ret);
			goto out;
		}
	}

	DRM_DEV_ERROR(drm->dev, "failed to load %s\n", fwname);
	fw = ERR_PTR(-ENOENT);
out:
	kfree(newname);
	return fw;
}

int adreno_load_fw(struct adreno_gpu *adreno_gpu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adreno_gpu->info->fw); i++) {
		const struct firmware *fw;

		if (!adreno_gpu->info->fw[i])
			continue;

		/* Skip loading GMU firmware with GMU Wrapper */
		if (adreno_has_gmu_wrapper(adreno_gpu) && i == ADRENO_FW_GMU)
			continue;

		/* Skip if the firmware has already been loaded */
		if (adreno_gpu->fw[i])
			continue;

		fw = adreno_request_fw(adreno_gpu, adreno_gpu->info->fw[i]);
		if (IS_ERR(fw))
			return PTR_ERR(fw);

		adreno_gpu->fw[i] = fw;
	}

	return 0;
}

struct drm_gem_object *adreno_fw_create_bo(struct msm_gpu *gpu,
		const struct firmware *fw, u64 *iova)
{
	struct drm_gem_object *bo;
	void *ptr;

	ptr = msm_gem_kernel_new(gpu->dev, fw->size - 4,
		MSM_BO_WC | MSM_BO_GPU_READONLY, gpu->aspace, &bo, iova);

	if (IS_ERR(ptr))
		return ERR_CAST(ptr);

	memcpy(ptr, &fw->data[4], fw->size - 4);

	msm_gem_put_vaddr(bo);

	return bo;
}

int adreno_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int ret;

	VERB("%s", gpu->name);

	if (adreno_gpu->info->family >= ADRENO_6XX_GEN1 &&
	    qcom_scm_set_gpu_smmu_aperture_is_available()) {
		/* We currently always use context bank 0, so hard code this */
		ret = qcom_scm_set_gpu_smmu_aperture(0);
		if (ret)
			DRM_DEV_ERROR(gpu->dev->dev, "unable to set SMMU aperture: %d\n", ret);
	}

	for (int i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		if (!ring)
			continue;

		ring->cur = ring->start;
		ring->next = ring->start;
		ring->memptrs->rptr = 0;
		ring->memptrs->bv_fence = ring->fctx->completed_fence;

		/* Detect and clean up an impossible fence, ie. if GPU managed
		 * to scribble something invalid, we don't want that to confuse
		 * us into mistakingly believing that submits have completed.
		 */
		if (fence_before(ring->fctx->last_fence, ring->memptrs->fence)) {
			ring->memptrs->fence = ring->fctx->last_fence;
		}
	}

	return 0;
}

/* Use this helper to read rptr, since a430 doesn't update rptr in memory */
static uint32_t get_rptr(struct adreno_gpu *adreno_gpu,
		struct msm_ringbuffer *ring)
{
	struct msm_gpu *gpu = &adreno_gpu->base;

	return gpu->funcs->get_rptr(gpu, ring);
}

struct msm_ringbuffer *adreno_active_ring(struct msm_gpu *gpu)
{
	return gpu->rb[0];
}

void adreno_recover(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	int ret;

	// XXX pm-runtime??  we *need* the device to be off after this
	// so maybe continuing to call ->pm_suspend/resume() is better?

	gpu->funcs->pm_suspend(gpu);
	gpu->funcs->pm_resume(gpu);

	ret = msm_gpu_hw_init(gpu);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "gpu hw init failed: %d\n", ret);
		/* hmm, oh well? */
	}
}

void adreno_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring, u32 reg)
{
	uint32_t wptr;

	/* Copy the shadow to the actual register */
	ring->cur = ring->next;

	/*
	 * Mask wptr value that we calculate to fit in the HW range. This is
	 * to account for the possibility that the last command fit exactly into
	 * the ringbuffer and rb->next hasn't wrapped to zero yet
	 */
	wptr = get_wptr(ring);

	/* ensure writes to ringbuffer have hit system memory: */
	mb();

	gpu_write(gpu, reg, wptr);
}

bool adreno_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	uint32_t wptr = get_wptr(ring);

	/* wait for CP to drain ringbuffer: */
	if (!spin_until(get_rptr(adreno_gpu, ring) == wptr))
		return true;

	/* TODO maybe we need to reset GPU here to recover from hang? */
	DRM_ERROR("%s: timeout waiting to drain ringbuffer %d rptr/wptr = %X/%X\n",
		gpu->name, ring->id, get_rptr(adreno_gpu, ring), wptr);

	return false;
}

int adreno_gpu_state_get(struct msm_gpu *gpu, struct msm_gpu_state *state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i, count = 0;

	WARN_ON(!mutex_is_locked(&gpu->lock));

	kref_init(&state->ref);

	ktime_get_real_ts64(&state->time);

	for (i = 0; i < gpu->nr_rings; i++) {
		int size = 0, j;

		state->ring[i].fence = gpu->rb[i]->memptrs->fence;
		state->ring[i].iova = gpu->rb[i]->iova;
		state->ring[i].seqno = gpu->rb[i]->fctx->last_fence;
		state->ring[i].rptr = get_rptr(adreno_gpu, gpu->rb[i]);
		state->ring[i].wptr = get_wptr(gpu->rb[i]);

		/* Copy at least 'wptr' dwords of the data */
		size = state->ring[i].wptr;

		/* After wptr find the last non zero dword to save space */
		for (j = state->ring[i].wptr; j < MSM_GPU_RINGBUFFER_SZ >> 2; j++)
			if (gpu->rb[i]->start[j])
				size = j + 1;

		if (size) {
			state->ring[i].data = kvmemdup(gpu->rb[i]->start, size << 2, GFP_KERNEL);
			if (state->ring[i].data)
				state->ring[i].data_size = size << 2;
		}
	}

	/* Some targets prefer to collect their own registers */
	if (!adreno_gpu->registers)
		return 0;

	/* Count the number of registers */
	for (i = 0; adreno_gpu->registers[i] != ~0; i += 2)
		count += adreno_gpu->registers[i + 1] -
			adreno_gpu->registers[i] + 1;

	state->registers = kcalloc(count * 2, sizeof(u32), GFP_KERNEL);
	if (state->registers) {
		int pos = 0;

		for (i = 0; adreno_gpu->registers[i] != ~0; i += 2) {
			u32 start = adreno_gpu->registers[i];
			u32 end   = adreno_gpu->registers[i + 1];
			u32 addr;

			for (addr = start; addr <= end; addr++) {
				state->registers[pos++] = addr;
				state->registers[pos++] = gpu_read(gpu, addr);
			}
		}

		state->nr_registers = count;
	}

	return 0;
}

void adreno_gpu_state_destroy(struct msm_gpu_state *state)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(state->ring); i++)
		kvfree(state->ring[i].data);

	for (i = 0; state->bos && i < state->nr_bos; i++)
		kvfree(state->bos[i].data);

	kfree(state->bos);
	kfree(state->comm);
	kfree(state->cmd);
	kfree(state->registers);
}

static void adreno_gpu_state_kref_destroy(struct kref *kref)
{
	struct msm_gpu_state *state = container_of(kref,
		struct msm_gpu_state, ref);

	adreno_gpu_state_destroy(state);
	kfree(state);
}

int adreno_gpu_state_put(struct msm_gpu_state *state)
{
	if (IS_ERR_OR_NULL(state))
		return 1;

	return kref_put(&state->ref, adreno_gpu_state_kref_destroy);
}

#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_DEV_COREDUMP)

static char *adreno_gpu_ascii85_encode(u32 *src, size_t len)
{
	void *buf;
	size_t buf_itr = 0, buffer_size;
	char out[ASCII85_BUFSZ];
	long l;
	int i;

	if (!src || !len)
		return NULL;

	l = ascii85_encode_len(len);

	/*
	 * Ascii85 outputs either a 5 byte string or a 1 byte string. So we
	 * account for the worst case of 5 bytes per dword plus the 1 for '\0'
	 */
	buffer_size = (l * 5) + 1;

	buf = kvmalloc(buffer_size, GFP_KERNEL);
	if (!buf)
		return NULL;

	for (i = 0; i < l; i++)
		buf_itr += scnprintf(buf + buf_itr, buffer_size - buf_itr, "%s",
				ascii85_encode(src[i], out));

	return buf;
}

/* len is expected to be in bytes
 *
 * WARNING: *ptr should be allocated with kvmalloc or friends.  It can be free'd
 * with kvfree() and replaced with a newly kvmalloc'd buffer on the first call
 * when the unencoded raw data is encoded
 */
void adreno_show_object(struct drm_printer *p, void **ptr, int len,
		bool *encoded)
{
	if (!*ptr || !len)
		return;

	if (!*encoded) {
		long datalen, i;
		u32 *buf = *ptr;

		/*
		 * Only dump the non-zero part of the buffer - rarely will
		 * any data completely fill the entire allocated size of
		 * the buffer.
		 */
		for (datalen = 0, i = 0; i < len >> 2; i++)
			if (buf[i])
				datalen = ((i + 1) << 2);

		/*
		 * If we reach here, then the originally captured binary buffer
		 * will be replaced with the ascii85 encoded string
		 */
		*ptr = adreno_gpu_ascii85_encode(buf, datalen);

		kvfree(buf);

		*encoded = true;
	}

	if (!*ptr)
		return;

	drm_puts(p, "    data: !!ascii85 |\n");
	drm_puts(p, "     ");

	drm_puts(p, *ptr);

	drm_puts(p, "\n");
}

void adreno_show(struct msm_gpu *gpu, struct msm_gpu_state *state,
		struct drm_printer *p)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i;

	if (IS_ERR_OR_NULL(state))
		return;

	drm_printf(p, "revision: %u (%"ADRENO_CHIPID_FMT")\n",
			adreno_gpu->info->revn,
			ADRENO_CHIPID_ARGS(adreno_gpu->chip_id));
	/*
	 * If this is state collected due to iova fault, so fault related info
	 *
	 * TTBR0 would not be zero, so this is a good way to distinguish
	 */
	if (state->fault_info.ttbr0) {
		const struct msm_gpu_fault_info *info = &state->fault_info;

		drm_puts(p, "fault-info:\n");
		drm_printf(p, "  - ttbr0=%.16llx\n", info->ttbr0);
		drm_printf(p, "  - iova=%.16lx\n", info->iova);
		drm_printf(p, "  - dir=%s\n", info->flags & IOMMU_FAULT_WRITE ? "WRITE" : "READ");
		drm_printf(p, "  - type=%s\n", info->type);
		drm_printf(p, "  - source=%s\n", info->block);
	}

	drm_printf(p, "rbbm-status: 0x%08x\n", state->rbbm_status);

	drm_puts(p, "ringbuffer:\n");

	for (i = 0; i < gpu->nr_rings; i++) {
		drm_printf(p, "  - id: %d\n", i);
		drm_printf(p, "    iova: 0x%016llx\n", state->ring[i].iova);
		drm_printf(p, "    last-fence: %u\n", state->ring[i].seqno);
		drm_printf(p, "    retired-fence: %u\n", state->ring[i].fence);
		drm_printf(p, "    rptr: %u\n", state->ring[i].rptr);
		drm_printf(p, "    wptr: %u\n", state->ring[i].wptr);
		drm_printf(p, "    size: %u\n", MSM_GPU_RINGBUFFER_SZ);

		adreno_show_object(p, &state->ring[i].data,
			state->ring[i].data_size, &state->ring[i].encoded);
	}

	if (state->bos) {
		drm_puts(p, "bos:\n");

		for (i = 0; i < state->nr_bos; i++) {
			drm_printf(p, "  - iova: 0x%016llx\n",
				state->bos[i].iova);
			drm_printf(p, "    size: %zd\n", state->bos[i].size);
			drm_printf(p, "    flags: 0x%x\n", state->bos[i].flags);
			drm_printf(p, "    name: %-32s\n", state->bos[i].name);

			adreno_show_object(p, &state->bos[i].data,
				state->bos[i].size, &state->bos[i].encoded);
		}
	}

	if (state->nr_registers) {
		drm_puts(p, "registers:\n");

		for (i = 0; i < state->nr_registers; i++) {
			drm_printf(p, "  - { offset: 0x%04x, value: 0x%08x }\n",
				state->registers[i * 2] << 2,
				state->registers[(i * 2) + 1]);
		}
	}
}
#endif

/* Dump common gpu status and scratch registers on any hang, to make
 * the hangcheck logs more useful.  The scratch registers seem always
 * safe to read when GPU has hung (unlike some other regs, depending
 * on how the GPU hung), and they are useful to match up to cmdstream
 * dumps when debugging hangs:
 */
void adreno_dump_info(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i;

	printk("revision: %u (%"ADRENO_CHIPID_FMT")\n",
			adreno_gpu->info->revn,
			ADRENO_CHIPID_ARGS(adreno_gpu->chip_id));

	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		printk("rb %d: fence:    %d/%d\n", i,
			ring->memptrs->fence,
			ring->fctx->last_fence);

		printk("rptr:     %d\n", get_rptr(adreno_gpu, ring));
		printk("rb wptr:  %d\n", get_wptr(ring));
	}
}

/* would be nice to not have to duplicate the _show() stuff with printk(): */
void adreno_dump(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	int i;

	if (!adreno_gpu->registers)
		return;

	/* dump these out in a form that can be parsed by demsm: */
	printk("IO:region %s 00000000 00020000\n", gpu->name);
	for (i = 0; adreno_gpu->registers[i] != ~0; i += 2) {
		uint32_t start = adreno_gpu->registers[i];
		uint32_t end   = adreno_gpu->registers[i+1];
		uint32_t addr;

		for (addr = start; addr <= end; addr++) {
			uint32_t val = gpu_read(gpu, addr);
			printk("IO:R %08x %08x\n", addr<<2, val);
		}
	}
}

static uint32_t ring_freewords(struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(ring->gpu);
	uint32_t size = MSM_GPU_RINGBUFFER_SZ >> 2;
	/* Use ring->next to calculate free size */
	uint32_t wptr = ring->next - ring->start;
	uint32_t rptr = get_rptr(adreno_gpu, ring);
	return (rptr + (size - 1) - wptr) % size;
}

void adreno_wait_ring(struct msm_ringbuffer *ring, uint32_t ndwords)
{
	if (spin_until(ring_freewords(ring) >= ndwords))
		DRM_DEV_ERROR(ring->gpu->dev->dev,
			"timeout waiting for space in ringbuffer %d\n",
			ring->id);
}

static int adreno_get_pwrlevels(struct device *dev,
		struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	unsigned long freq = ULONG_MAX;
	struct dev_pm_opp *opp;
	int ret;

	gpu->fast_rate = 0;

	/* devm_pm_opp_of_add_table may error out but will still create an OPP table */
	ret = devm_pm_opp_of_add_table(dev);
	if (ret == -ENODEV) {
		/* Special cases for ancient hw with ancient DT bindings */
		if (adreno_is_a2xx(adreno_gpu)) {
			dev_warn(dev, "Unable to find the OPP table. Falling back to 200 MHz.\n");
			dev_pm_opp_add(dev, 200000000, 0);
		} else if (adreno_is_a320(adreno_gpu)) {
			dev_warn(dev, "Unable to find the OPP table. Falling back to 450 MHz.\n");
			dev_pm_opp_add(dev, 450000000, 0);
		} else {
			DRM_DEV_ERROR(dev, "Unable to find the OPP table\n");
			return -ENODEV;
		}
	} else if (ret) {
		DRM_DEV_ERROR(dev, "Unable to set the OPP table\n");
		return ret;
	}

	/* Find the fastest defined rate */
	opp = dev_pm_opp_find_freq_floor(dev, &freq);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	gpu->fast_rate = freq;
	dev_pm_opp_put(opp);

	DBG("fast_rate=%u, slow_rate=27000000", gpu->fast_rate);

	return 0;
}

int adreno_gpu_ocmem_init(struct device *dev, struct adreno_gpu *adreno_gpu,
			  struct adreno_ocmem *adreno_ocmem)
{
	struct ocmem_buf *ocmem_hdl;
	struct ocmem *ocmem;

	ocmem = of_get_ocmem(dev);
	if (IS_ERR(ocmem)) {
		if (PTR_ERR(ocmem) == -ENODEV) {
			/*
			 * Return success since either the ocmem property was
			 * not specified in device tree, or ocmem support is
			 * not compiled into the kernel.
			 */
			return 0;
		}

		return PTR_ERR(ocmem);
	}

	ocmem_hdl = ocmem_allocate(ocmem, OCMEM_GRAPHICS, adreno_gpu->info->gmem);
	if (IS_ERR(ocmem_hdl))
		return PTR_ERR(ocmem_hdl);

	adreno_ocmem->ocmem = ocmem;
	adreno_ocmem->base = ocmem_hdl->addr;
	adreno_ocmem->hdl = ocmem_hdl;

	if (WARN_ON(ocmem_hdl->len != adreno_gpu->info->gmem))
		return -ENOMEM;

	return 0;
}

void adreno_gpu_ocmem_cleanup(struct adreno_ocmem *adreno_ocmem)
{
	if (adreno_ocmem && adreno_ocmem->base)
		ocmem_free(adreno_ocmem->ocmem, OCMEM_GRAPHICS,
			   adreno_ocmem->hdl);
}

int adreno_read_speedbin(struct device *dev, u32 *speedbin)
{
	return nvmem_cell_read_variable_le_u32(dev, "speed_bin", speedbin);
}

int adreno_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct adreno_gpu *adreno_gpu,
		const struct adreno_gpu_funcs *funcs, int nr_rings)
{
	struct device *dev = &pdev->dev;
	struct adreno_platform_config *config = dev->platform_data;
	struct msm_gpu_config adreno_gpu_config  = { 0 };
	struct msm_gpu *gpu = &adreno_gpu->base;
	const char *gpu_name;
	u32 speedbin;
	int ret;

	adreno_gpu->funcs = funcs;
	adreno_gpu->info = config->info;
	adreno_gpu->chip_id = config->chip_id;

	gpu->allow_relocs = config->info->family < ADRENO_6XX_GEN1;
	gpu->pdev = pdev;

	/* Only handle the core clock when GMU is not in use (or is absent). */
	if (adreno_has_gmu_wrapper(adreno_gpu) ||
	    adreno_gpu->info->family < ADRENO_6XX_GEN1) {
		/*
		 * This can only be done before devm_pm_opp_of_add_table(), or
		 * dev_pm_opp_set_config() will WARN_ON()
		 */
		if (IS_ERR(devm_clk_get(dev, "core"))) {
			/*
			 * If "core" is absent, go for the legacy clock name.
			 * If we got this far in probing, it's a given one of
			 * them exists.
			 */
			devm_pm_opp_set_clkname(dev, "core_clk");
		} else
			devm_pm_opp_set_clkname(dev, "core");
	}

	if (adreno_read_speedbin(dev, &speedbin) || !speedbin)
		speedbin = 0xffff;
	adreno_gpu->speedbin = (uint16_t) (0xffff & speedbin);

	gpu_name = devm_kasprintf(dev, GFP_KERNEL, "%"ADRENO_CHIPID_FMT,
			ADRENO_CHIPID_ARGS(config->chip_id));
	if (!gpu_name)
		return -ENOMEM;

	adreno_gpu_config.ioname = "kgsl_3d0_reg_memory";

	adreno_gpu_config.nr_rings = nr_rings;

	ret = adreno_get_pwrlevels(dev, gpu);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev,
		adreno_gpu->info->inactive_period);
	pm_runtime_use_autosuspend(dev);

	return msm_gpu_init(drm, pdev, &adreno_gpu->base, &funcs->base,
			gpu_name, &adreno_gpu_config);
}

void adreno_gpu_cleanup(struct adreno_gpu *adreno_gpu)
{
	struct msm_gpu *gpu = &adreno_gpu->base;
	struct msm_drm_private *priv = gpu->dev ? gpu->dev->dev_private : NULL;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(adreno_gpu->info->fw); i++)
		release_firmware(adreno_gpu->fw[i]);

	if (priv && pm_runtime_enabled(&priv->gpu_pdev->dev))
		pm_runtime_disable(&priv->gpu_pdev->dev);

	msm_gpu_cleanup(&adreno_gpu->base);
}
