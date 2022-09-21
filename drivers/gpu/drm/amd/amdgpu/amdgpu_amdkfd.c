// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include "amdgpu_amdkfd.h"
#include "amd_pcie.h"
#include "amd_shared.h"

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_dma_buf.h"
#include <linux/module.h>
#include <linux/dma-buf.h>
#include "amdgpu_xgmi.h"
#include <uapi/linux/kfd_ioctl.h>
#include "amdgpu_ras.h"
#include "amdgpu_umc.h"
#include "amdgpu_reset.h"

/* Total memory size in system memory and all GPU VRAM. Used to
 * estimate worst case amount of memory to reserve for page tables
 */
uint64_t amdgpu_amdkfd_total_mem_size;

static bool kfd_initialized;

int amdgpu_amdkfd_init(void)
{
	struct sysinfo si;
	int ret;

	si_meminfo(&si);
	amdgpu_amdkfd_total_mem_size = si.freeram - si.freehigh;
	amdgpu_amdkfd_total_mem_size *= si.mem_unit;

	ret = kgd2kfd_init();
	amdgpu_amdkfd_gpuvm_init_mem_limits();
	kfd_initialized = !ret;

	return ret;
}

void amdgpu_amdkfd_fini(void)
{
	if (kfd_initialized) {
		kgd2kfd_exit();
		kfd_initialized = false;
	}
}

void amdgpu_amdkfd_device_probe(struct amdgpu_device *adev)
{
	bool vf = amdgpu_sriov_vf(adev);

	if (!kfd_initialized)
		return;

	adev->kfd.dev = kgd2kfd_probe(adev, vf);

	if (adev->kfd.dev)
		amdgpu_amdkfd_total_mem_size += adev->gmc.real_vram_size;
}

/**
 * amdgpu_doorbell_get_kfd_info - Report doorbell configuration required to
 *                                setup amdkfd
 *
 * @adev: amdgpu_device pointer
 * @aperture_base: output returning doorbell aperture base physical address
 * @aperture_size: output returning doorbell aperture size in bytes
 * @start_offset: output returning # of doorbell bytes reserved for amdgpu.
 *
 * amdgpu and amdkfd share the doorbell aperture. amdgpu sets it up,
 * takes doorbells required for its own rings and reports the setup to amdkfd.
 * amdgpu reserved doorbells are at the start of the doorbell aperture.
 */
static void amdgpu_doorbell_get_kfd_info(struct amdgpu_device *adev,
					 phys_addr_t *aperture_base,
					 size_t *aperture_size,
					 size_t *start_offset)
{
	/*
	 * The first num_doorbells are used by amdgpu.
	 * amdkfd takes whatever's left in the aperture.
	 */
	if (adev->enable_mes) {
		/*
		 * With MES enabled, we only need to initialize
		 * the base address. The size and offset are
		 * not initialized as AMDGPU manages the whole
		 * doorbell space.
		 */
		*aperture_base = adev->doorbell.base;
		*aperture_size = 0;
		*start_offset = 0;
	} else if (adev->doorbell.size > adev->doorbell.num_doorbells *
						sizeof(u32)) {
		*aperture_base = adev->doorbell.base;
		*aperture_size = adev->doorbell.size;
		*start_offset = adev->doorbell.num_doorbells * sizeof(u32);
	} else {
		*aperture_base = 0;
		*aperture_size = 0;
		*start_offset = 0;
	}
}


static void amdgpu_amdkfd_reset_work(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  kfd.reset_work);

	struct amdgpu_reset_context reset_context;

	memset(&reset_context, 0, sizeof(reset_context));

	reset_context.method = AMD_RESET_METHOD_NONE;
	reset_context.reset_req_dev = adev;
	clear_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);
	clear_bit(AMDGPU_SKIP_MODE2_RESET, &reset_context.flags);

	amdgpu_device_gpu_recover(adev, NULL, &reset_context);
}

void amdgpu_amdkfd_device_init(struct amdgpu_device *adev)
{
	int i;
	int last_valid_bit;

	if (adev->kfd.dev) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap =
				((1 << AMDGPU_NUM_VMID) - 1) -
				((1 << adev->vm_manager.first_kfd_vmid) - 1),
			.num_pipe_per_mec = adev->gfx.mec.num_pipe_per_mec,
			.num_queue_per_pipe = adev->gfx.mec.num_queue_per_pipe,
			.gpuvm_size = min(adev->vm_manager.max_pfn
					  << AMDGPU_GPU_PAGE_SHIFT,
					  AMDGPU_GMC_HOLE_START),
			.drm_render_minor = adev_to_drm(adev)->render->index,
			.sdma_doorbell_idx = adev->doorbell_index.sdma_engine,
			.enable_mes = adev->enable_mes,
		};

		/* this is going to have a few of the MSBs set that we need to
		 * clear
		 */
		bitmap_complement(gpu_resources.cp_queue_bitmap,
				  adev->gfx.mec.queue_bitmap,
				  KGD_MAX_QUEUES);

		/* According to linux/bitmap.h we shouldn't use bitmap_clear if
		 * nbits is not compile time constant
		 */
		last_valid_bit = 1 /* only first MEC can have compute queues */
				* adev->gfx.mec.num_pipe_per_mec
				* adev->gfx.mec.num_queue_per_pipe;
		for (i = last_valid_bit; i < KGD_MAX_QUEUES; ++i)
			clear_bit(i, gpu_resources.cp_queue_bitmap);

		amdgpu_doorbell_get_kfd_info(adev,
				&gpu_resources.doorbell_physical_address,
				&gpu_resources.doorbell_aperture_size,
				&gpu_resources.doorbell_start_offset);

		/* Since SOC15, BIF starts to statically use the
		 * lower 12 bits of doorbell addresses for routing
		 * based on settings in registers like
		 * SDMA0_DOORBELL_RANGE etc..
		 * In order to route a doorbell to CP engine, the lower
		 * 12 bits of its address has to be outside the range
		 * set for SDMA, VCN, and IH blocks.
		 */
		if (adev->asic_type >= CHIP_VEGA10) {
			gpu_resources.non_cp_doorbells_start =
					adev->doorbell_index.first_non_cp;
			gpu_resources.non_cp_doorbells_end =
					adev->doorbell_index.last_non_cp;
		}

		adev->kfd.init_complete = kgd2kfd_device_init(adev->kfd.dev,
						adev_to_drm(adev), &gpu_resources);

		INIT_WORK(&adev->kfd.reset_work, amdgpu_amdkfd_reset_work);
	}
}

void amdgpu_amdkfd_device_fini_sw(struct amdgpu_device *adev)
{
	if (adev->kfd.dev) {
		kgd2kfd_device_exit(adev->kfd.dev);
		adev->kfd.dev = NULL;
	}
}

void amdgpu_amdkfd_interrupt(struct amdgpu_device *adev,
		const void *ih_ring_entry)
{
	if (adev->kfd.dev)
		kgd2kfd_interrupt(adev->kfd.dev, ih_ring_entry);
}

void amdgpu_amdkfd_suspend(struct amdgpu_device *adev, bool run_pm)
{
	if (adev->kfd.dev)
		kgd2kfd_suspend(adev->kfd.dev, run_pm);
}

int amdgpu_amdkfd_resume_iommu(struct amdgpu_device *adev)
{
	int r = 0;

	if (adev->kfd.dev)
		r = kgd2kfd_resume_iommu(adev->kfd.dev);

	return r;
}

int amdgpu_amdkfd_resume(struct amdgpu_device *adev, bool run_pm)
{
	int r = 0;

	if (adev->kfd.dev)
		r = kgd2kfd_resume(adev->kfd.dev, run_pm);

	return r;
}

int amdgpu_amdkfd_pre_reset(struct amdgpu_device *adev)
{
	int r = 0;

	if (adev->kfd.dev)
		r = kgd2kfd_pre_reset(adev->kfd.dev);

	return r;
}

int amdgpu_amdkfd_post_reset(struct amdgpu_device *adev)
{
	int r = 0;

	if (adev->kfd.dev)
		r = kgd2kfd_post_reset(adev->kfd.dev);

	return r;
}

void amdgpu_amdkfd_gpu_reset(struct amdgpu_device *adev)
{
	if (amdgpu_device_should_recover_gpu(adev))
		amdgpu_reset_domain_schedule(adev->reset_domain,
					     &adev->kfd.reset_work);
}

int amdgpu_amdkfd_alloc_gtt_mem(struct amdgpu_device *adev, size_t size,
				void **mem_obj, uint64_t *gpu_addr,
				void **cpu_ptr, bool cp_mqd_gfx9)
{
	struct amdgpu_bo *bo = NULL;
	struct amdgpu_bo_param bp;
	int r;
	void *cpu_ptr_tmp = NULL;

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_GTT;
	bp.flags = AMDGPU_GEM_CREATE_CPU_GTT_USWC;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;
	bp.bo_ptr_size = sizeof(struct amdgpu_bo);

	if (cp_mqd_gfx9)
		bp.flags |= AMDGPU_GEM_CREATE_CP_MQD_GFX9;

	r = amdgpu_bo_create(adev, &bp, &bo);
	if (r) {
		dev_err(adev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = amdgpu_bo_reserve(bo, true);
	if (r) {
		dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT);
	if (r) {
		dev_err(adev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}

	r = amdgpu_ttm_alloc_gart(&bo->tbo);
	if (r) {
		dev_err(adev->dev, "%p bind failed\n", bo);
		goto allocate_mem_kmap_bo_failed;
	}

	r = amdgpu_bo_kmap(bo, &cpu_ptr_tmp);
	if (r) {
		dev_err(adev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}

	*mem_obj = bo;
	*gpu_addr = amdgpu_bo_gpu_offset(bo);
	*cpu_ptr = cpu_ptr_tmp;

	amdgpu_bo_unreserve(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin(bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
allocate_mem_reserve_bo_failed:
	amdgpu_bo_unref(&bo);

	return r;
}

void amdgpu_amdkfd_free_gtt_mem(struct amdgpu_device *adev, void *mem_obj)
{
	struct amdgpu_bo *bo = (struct amdgpu_bo *) mem_obj;

	amdgpu_bo_reserve(bo, true);
	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&(bo));
}

int amdgpu_amdkfd_alloc_gws(struct amdgpu_device *adev, size_t size,
				void **mem_obj)
{
	struct amdgpu_bo *bo = NULL;
	struct amdgpu_bo_user *ubo;
	struct amdgpu_bo_param bp;
	int r;

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = 1;
	bp.domain = AMDGPU_GEM_DOMAIN_GWS;
	bp.flags = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	bp.type = ttm_bo_type_device;
	bp.resv = NULL;
	bp.bo_ptr_size = sizeof(struct amdgpu_bo);

	r = amdgpu_bo_create_user(adev, &bp, &ubo);
	if (r) {
		dev_err(adev->dev,
			"failed to allocate gws BO for amdkfd (%d)\n", r);
		return r;
	}

	bo = &ubo->bo;
	*mem_obj = bo;
	return 0;
}

void amdgpu_amdkfd_free_gws(struct amdgpu_device *adev, void *mem_obj)
{
	struct amdgpu_bo *bo = (struct amdgpu_bo *)mem_obj;

	amdgpu_bo_unref(&bo);
}

uint32_t amdgpu_amdkfd_get_fw_version(struct amdgpu_device *adev,
				      enum kgd_engine_type type)
{
	switch (type) {
	case KGD_ENGINE_PFP:
		return adev->gfx.pfp_fw_version;

	case KGD_ENGINE_ME:
		return adev->gfx.me_fw_version;

	case KGD_ENGINE_CE:
		return adev->gfx.ce_fw_version;

	case KGD_ENGINE_MEC1:
		return adev->gfx.mec_fw_version;

	case KGD_ENGINE_MEC2:
		return adev->gfx.mec2_fw_version;

	case KGD_ENGINE_RLC:
		return adev->gfx.rlc_fw_version;

	case KGD_ENGINE_SDMA1:
		return adev->sdma.instance[0].fw_version;

	case KGD_ENGINE_SDMA2:
		return adev->sdma.instance[1].fw_version;

	default:
		return 0;
	}

	return 0;
}

void amdgpu_amdkfd_get_local_mem_info(struct amdgpu_device *adev,
				      struct kfd_local_mem_info *mem_info)
{
	memset(mem_info, 0, sizeof(*mem_info));

	mem_info->local_mem_size_public = adev->gmc.visible_vram_size;
	mem_info->local_mem_size_private = adev->gmc.real_vram_size -
						adev->gmc.visible_vram_size;

	mem_info->vram_width = adev->gmc.vram_width;

	pr_debug("Address base: %pap public 0x%llx private 0x%llx\n",
			&adev->gmc.aper_base,
			mem_info->local_mem_size_public,
			mem_info->local_mem_size_private);

	if (amdgpu_sriov_vf(adev))
		mem_info->mem_clk_max = adev->clock.default_mclk / 100;
	else if (adev->pm.dpm_enabled) {
		if (amdgpu_emu_mode == 1)
			mem_info->mem_clk_max = 0;
		else
			mem_info->mem_clk_max = amdgpu_dpm_get_mclk(adev, false) / 100;
	} else
		mem_info->mem_clk_max = 100;
}

uint64_t amdgpu_amdkfd_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	if (adev->gfx.funcs->get_gpu_clock_counter)
		return adev->gfx.funcs->get_gpu_clock_counter(adev);
	return 0;
}

uint32_t amdgpu_amdkfd_get_max_engine_clock_in_mhz(struct amdgpu_device *adev)
{
	/* the sclk is in quantas of 10kHz */
	if (amdgpu_sriov_vf(adev))
		return adev->clock.default_sclk / 100;
	else if (adev->pm.dpm_enabled)
		return amdgpu_dpm_get_sclk(adev, false) / 100;
	else
		return 100;
}

void amdgpu_amdkfd_get_cu_info(struct amdgpu_device *adev, struct kfd_cu_info *cu_info)
{
	struct amdgpu_cu_info acu_info = adev->gfx.cu_info;

	memset(cu_info, 0, sizeof(*cu_info));
	if (sizeof(cu_info->cu_bitmap) != sizeof(acu_info.bitmap))
		return;

	cu_info->cu_active_number = acu_info.number;
	cu_info->cu_ao_mask = acu_info.ao_cu_mask;
	memcpy(&cu_info->cu_bitmap[0], &acu_info.bitmap[0],
	       sizeof(acu_info.bitmap));
	cu_info->num_shader_engines = adev->gfx.config.max_shader_engines;
	cu_info->num_shader_arrays_per_engine = adev->gfx.config.max_sh_per_se;
	cu_info->num_cu_per_sh = adev->gfx.config.max_cu_per_sh;
	cu_info->simd_per_cu = acu_info.simd_per_cu;
	cu_info->max_waves_per_simd = acu_info.max_waves_per_simd;
	cu_info->wave_front_size = acu_info.wave_front_size;
	cu_info->max_scratch_slots_per_cu = acu_info.max_scratch_slots_per_cu;
	cu_info->lds_size = acu_info.lds_size;
}

int amdgpu_amdkfd_get_dmabuf_info(struct amdgpu_device *adev, int dma_buf_fd,
				  struct amdgpu_device **dmabuf_adev,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags)
{
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	uint64_t metadata_flags;
	int r = -EINVAL;

	dma_buf = dma_buf_get(dma_buf_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	if (dma_buf->ops != &amdgpu_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		goto out_put;

	obj = dma_buf->priv;
	if (obj->dev->driver != adev_to_drm(adev)->driver)
		/* Can't handle buffers from different drivers */
		goto out_put;

	adev = drm_to_adev(obj->dev);
	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->preferred_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		goto out_put;

	r = 0;
	if (dmabuf_adev)
		*dmabuf_adev = adev;
	if (bo_size)
		*bo_size = amdgpu_bo_size(bo);
	if (metadata_buffer)
		r = amdgpu_bo_get_metadata(bo, metadata_buffer, buffer_size,
					   metadata_size, &metadata_flags);
	if (flags) {
		*flags = (bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
				KFD_IOC_ALLOC_MEM_FLAGS_VRAM
				: KFD_IOC_ALLOC_MEM_FLAGS_GTT;

		if (bo->flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
			*flags |= KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC;
	}

out_put:
	dma_buf_put(dma_buf);
	return r;
}

uint8_t amdgpu_amdkfd_get_xgmi_hops_count(struct amdgpu_device *dst,
					  struct amdgpu_device *src)
{
	struct amdgpu_device *peer_adev = src;
	struct amdgpu_device *adev = dst;
	int ret = amdgpu_xgmi_get_hops_count(adev, peer_adev);

	if (ret < 0) {
		DRM_ERROR("amdgpu: failed to get  xgmi hops count between node %d and %d. ret = %d\n",
			adev->gmc.xgmi.physical_node_id,
			peer_adev->gmc.xgmi.physical_node_id, ret);
		ret = 0;
	}
	return  (uint8_t)ret;
}

int amdgpu_amdkfd_get_xgmi_bandwidth_mbytes(struct amdgpu_device *dst,
					    struct amdgpu_device *src,
					    bool is_min)
{
	struct amdgpu_device *adev = dst, *peer_adev;
	int num_links;

	if (adev->asic_type != CHIP_ALDEBARAN)
		return 0;

	if (src)
		peer_adev = src;

	/* num links returns 0 for indirect peers since indirect route is unknown. */
	num_links = is_min ? 1 : amdgpu_xgmi_get_num_links(adev, peer_adev);
	if (num_links < 0) {
		DRM_ERROR("amdgpu: failed to get xgmi num links between node %d and %d. ret = %d\n",
			adev->gmc.xgmi.physical_node_id,
			peer_adev->gmc.xgmi.physical_node_id, num_links);
		num_links = 0;
	}

	/* Aldebaran xGMI DPM is defeatured so assume x16 x 25Gbps for bandwidth. */
	return (num_links * 16 * 25000)/BITS_PER_BYTE;
}

int amdgpu_amdkfd_get_pcie_bandwidth_mbytes(struct amdgpu_device *adev, bool is_min)
{
	int num_lanes_shift = (is_min ? ffs(adev->pm.pcie_mlw_mask) :
							fls(adev->pm.pcie_mlw_mask)) - 1;
	int gen_speed_shift = (is_min ? ffs(adev->pm.pcie_gen_mask &
						CAIL_PCIE_LINK_SPEED_SUPPORT_MASK) :
					fls(adev->pm.pcie_gen_mask &
						CAIL_PCIE_LINK_SPEED_SUPPORT_MASK)) - 1;
	uint32_t num_lanes_mask = 1 << num_lanes_shift;
	uint32_t gen_speed_mask = 1 << gen_speed_shift;
	int num_lanes_factor = 0, gen_speed_mbits_factor = 0;

	switch (num_lanes_mask) {
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X1:
		num_lanes_factor = 1;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X2:
		num_lanes_factor = 2;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X4:
		num_lanes_factor = 4;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X8:
		num_lanes_factor = 8;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X12:
		num_lanes_factor = 12;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X16:
		num_lanes_factor = 16;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X32:
		num_lanes_factor = 32;
		break;
	}

	switch (gen_speed_mask) {
	case CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1:
		gen_speed_mbits_factor = 2500;
		break;
	case CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2:
		gen_speed_mbits_factor = 5000;
		break;
	case CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3:
		gen_speed_mbits_factor = 8000;
		break;
	case CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4:
		gen_speed_mbits_factor = 16000;
		break;
	case CAIL_PCIE_LINK_SPEED_SUPPORT_GEN5:
		gen_speed_mbits_factor = 32000;
		break;
	}

	return (num_lanes_factor * gen_speed_mbits_factor)/BITS_PER_BYTE;
}

int amdgpu_amdkfd_submit_ib(struct amdgpu_device *adev,
				enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len)
{
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct amdgpu_ring *ring;
	struct dma_fence *f = NULL;
	int ret;

	switch (engine) {
	case KGD_ENGINE_MEC1:
		ring = &adev->gfx.compute_ring[0];
		break;
	case KGD_ENGINE_SDMA1:
		ring = &adev->sdma.instance[0].ring;
		break;
	case KGD_ENGINE_SDMA2:
		ring = &adev->sdma.instance[1].ring;
		break;
	default:
		pr_err("Invalid engine in IB submission: %d\n", engine);
		ret = -EINVAL;
		goto err;
	}

	ret = amdgpu_job_alloc(adev, 1, &job, NULL);
	if (ret)
		goto err;

	ib = &job->ibs[0];
	memset(ib, 0, sizeof(struct amdgpu_ib));

	ib->gpu_addr = gpu_addr;
	ib->ptr = ib_cmd;
	ib->length_dw = ib_len;
	/* This works for NO_HWS. TODO: need to handle without knowing VMID */
	job->vmid = vmid;
	job->num_ibs = 1;

	ret = amdgpu_ib_schedule(ring, 1, ib, job, &f);

	if (ret) {
		DRM_ERROR("amdgpu: failed to schedule IB.\n");
		goto err_ib_sched;
	}

	/* Drop the initial kref_init count (see drm_sched_main as example) */
	dma_fence_put(f);
	ret = dma_fence_wait(f, false);

err_ib_sched:
	amdgpu_job_free(job);
err:
	return ret;
}

void amdgpu_amdkfd_set_compute_idle(struct amdgpu_device *adev, bool idle)
{
	amdgpu_dpm_switch_power_profile(adev,
					PP_SMC_POWER_PROFILE_COMPUTE,
					!idle);
}

bool amdgpu_amdkfd_is_kfd_vmid(struct amdgpu_device *adev, u32 vmid)
{
	if (adev->kfd.dev)
		return vmid >= adev->vm_manager.first_kfd_vmid;

	return false;
}

int amdgpu_amdkfd_flush_gpu_tlb_vmid(struct amdgpu_device *adev,
				     uint16_t vmid)
{
	if (adev->family == AMDGPU_FAMILY_AI) {
		int i;

		for (i = 0; i < adev->num_vmhubs; i++)
			amdgpu_gmc_flush_gpu_tlb(adev, vmid, i, 0);
	} else {
		amdgpu_gmc_flush_gpu_tlb(adev, vmid, AMDGPU_GFXHUB_0, 0);
	}

	return 0;
}

int amdgpu_amdkfd_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
				      uint16_t pasid, enum TLB_FLUSH_TYPE flush_type)
{
	bool all_hub = false;

	if (adev->family == AMDGPU_FAMILY_AI ||
	    adev->family == AMDGPU_FAMILY_RV)
		all_hub = true;

	return amdgpu_gmc_flush_gpu_tlb_pasid(adev, pasid, flush_type, all_hub);
}

bool amdgpu_amdkfd_have_atomics_support(struct amdgpu_device *adev)
{
	return adev->have_atomics_support;
}

void amdgpu_amdkfd_ras_poison_consumption_handler(struct amdgpu_device *adev, bool reset)
{
	struct ras_err_data err_data = {0, 0, 0, NULL};

	amdgpu_umc_poison_handler(adev, &err_data, reset);
}

bool amdgpu_amdkfd_ras_query_utcl2_poison_status(struct amdgpu_device *adev)
{
	if (adev->gfx.ras && adev->gfx.ras->query_utcl2_poison_status)
		return adev->gfx.ras->query_utcl2_poison_status(adev);
	else
		return false;
}
