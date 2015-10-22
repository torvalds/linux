/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 *
 *
 */
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <drm/drmP.h>
#include <linux/firmware.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "cgs_linux.h"
#include "atom.h"
#include "amdgpu_ucode.h"


struct amdgpu_cgs_device {
	struct cgs_device base;
	struct amdgpu_device *adev;
};

#define CGS_FUNC_ADEV							\
	struct amdgpu_device *adev =					\
		((struct amdgpu_cgs_device *)cgs_device)->adev

static int amdgpu_cgs_gpu_mem_info(void *cgs_device, enum cgs_gpu_mem_type type,
				   uint64_t *mc_start, uint64_t *mc_size,
				   uint64_t *mem_size)
{
	CGS_FUNC_ADEV;
	switch(type) {
	case CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__VISIBLE_FB:
		*mc_start = 0;
		*mc_size = adev->mc.visible_vram_size;
		*mem_size = adev->mc.visible_vram_size - adev->vram_pin_size;
		break;
	case CGS_GPU_MEM_TYPE__INVISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__INVISIBLE_FB:
		*mc_start = adev->mc.visible_vram_size;
		*mc_size = adev->mc.real_vram_size - adev->mc.visible_vram_size;
		*mem_size = *mc_size;
		break;
	case CGS_GPU_MEM_TYPE__GART_CACHEABLE:
	case CGS_GPU_MEM_TYPE__GART_WRITECOMBINE:
		*mc_start = adev->mc.gtt_start;
		*mc_size = adev->mc.gtt_size;
		*mem_size = adev->mc.gtt_size - adev->gart_pin_size;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int amdgpu_cgs_gmap_kmem(void *cgs_device, void *kmem,
				uint64_t size,
				uint64_t min_offset, uint64_t max_offset,
				cgs_handle_t *kmem_handle, uint64_t *mcaddr)
{
	CGS_FUNC_ADEV;
	int ret;
	struct amdgpu_bo *bo;
	struct page *kmem_page = vmalloc_to_page(kmem);
	int npages = ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT;

	struct sg_table *sg = drm_prime_pages_to_sg(&kmem_page, npages);
	ret = amdgpu_bo_create(adev, size, PAGE_SIZE, false,
			       AMDGPU_GEM_DOMAIN_GTT, 0, sg, NULL, &bo);
	if (ret)
		return ret;
	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	/* pin buffer into GTT */
	ret = amdgpu_bo_pin_restricted(bo, AMDGPU_GEM_DOMAIN_GTT,
				       min_offset, max_offset, mcaddr);
	amdgpu_bo_unreserve(bo);

	*kmem_handle = (cgs_handle_t)bo;
	return ret;
}

static int amdgpu_cgs_gunmap_kmem(void *cgs_device, cgs_handle_t kmem_handle)
{
	struct amdgpu_bo *obj = (struct amdgpu_bo *)kmem_handle;

	if (obj) {
		int r = amdgpu_bo_reserve(obj, false);
		if (likely(r == 0)) {
			amdgpu_bo_unpin(obj);
			amdgpu_bo_unreserve(obj);
		}
		amdgpu_bo_unref(&obj);

	}
	return 0;
}

static int amdgpu_cgs_alloc_gpu_mem(void *cgs_device,
				    enum cgs_gpu_mem_type type,
				    uint64_t size, uint64_t align,
				    uint64_t min_offset, uint64_t max_offset,
				    cgs_handle_t *handle)
{
	CGS_FUNC_ADEV;
	uint16_t flags = 0;
	int ret = 0;
	uint32_t domain = 0;
	struct amdgpu_bo *obj;
	struct ttm_placement placement;
	struct ttm_place place;

	if (min_offset > max_offset) {
		BUG_ON(1);
		return -EINVAL;
	}

	/* fail if the alignment is not a power of 2 */
	if (((align != 1) && (align & (align - 1)))
	    || size == 0 || align == 0)
		return -EINVAL;


	switch(type) {
	case CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__VISIBLE_FB:
		flags = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		if (max_offset > adev->mc.real_vram_size)
			return -EINVAL;
		place.fpfn = min_offset >> PAGE_SHIFT;
		place.lpfn = max_offset >> PAGE_SHIFT;
		place.flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
			TTM_PL_FLAG_VRAM;
		break;
	case CGS_GPU_MEM_TYPE__INVISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__INVISIBLE_FB:
		flags = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		if (adev->mc.visible_vram_size < adev->mc.real_vram_size) {
			place.fpfn =
				max(min_offset, adev->mc.visible_vram_size) >> PAGE_SHIFT;
			place.lpfn =
				min(max_offset, adev->mc.real_vram_size) >> PAGE_SHIFT;
			place.flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
				TTM_PL_FLAG_VRAM;
		}

		break;
	case CGS_GPU_MEM_TYPE__GART_CACHEABLE:
		domain = AMDGPU_GEM_DOMAIN_GTT;
		place.fpfn = min_offset >> PAGE_SHIFT;
		place.lpfn = max_offset >> PAGE_SHIFT;
		place.flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_TT;
		break;
	case CGS_GPU_MEM_TYPE__GART_WRITECOMBINE:
		flags = AMDGPU_GEM_CREATE_CPU_GTT_USWC;
		domain = AMDGPU_GEM_DOMAIN_GTT;
		place.fpfn = min_offset >> PAGE_SHIFT;
		place.lpfn = max_offset >> PAGE_SHIFT;
		place.flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_TT |
			TTM_PL_FLAG_UNCACHED;
		break;
	default:
		return -EINVAL;
	}


	*handle = 0;

	placement.placement = &place;
	placement.num_placement = 1;
	placement.busy_placement = &place;
	placement.num_busy_placement = 1;

	ret = amdgpu_bo_create_restricted(adev, size, PAGE_SIZE,
					  true, domain, flags,
					  NULL, &placement, NULL,
					  &obj);
	if (ret) {
		DRM_ERROR("(%d) bo create failed\n", ret);
		return ret;
	}
	*handle = (cgs_handle_t)obj;

	return ret;
}

static int amdgpu_cgs_import_gpu_mem(void *cgs_device, int dmabuf_fd,
				     cgs_handle_t *handle)
{
	CGS_FUNC_ADEV;
	int r;
	uint32_t dma_handle;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	struct drm_device *dev = adev->ddev;
	struct drm_file *file_priv = NULL, *priv;

	mutex_lock(&dev->struct_mutex);
	list_for_each_entry(priv, &dev->filelist, lhead) {
		rcu_read_lock();
		if (priv->pid == get_pid(task_pid(current)))
			file_priv = priv;
		rcu_read_unlock();
		if (file_priv)
			break;
	}
	mutex_unlock(&dev->struct_mutex);
	r = dev->driver->prime_fd_to_handle(dev,
					    file_priv, dmabuf_fd,
					    &dma_handle);
	spin_lock(&file_priv->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_find(&file_priv->object_idr, dma_handle);
	if (obj == NULL) {
		spin_unlock(&file_priv->table_lock);
		return -EINVAL;
	}
	spin_unlock(&file_priv->table_lock);
	bo = gem_to_amdgpu_bo(obj);
	*handle = (cgs_handle_t)bo;
	return 0;
}

static int amdgpu_cgs_free_gpu_mem(void *cgs_device, cgs_handle_t handle)
{
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;

	if (obj) {
		int r = amdgpu_bo_reserve(obj, false);
		if (likely(r == 0)) {
			amdgpu_bo_kunmap(obj);
			amdgpu_bo_unpin(obj);
			amdgpu_bo_unreserve(obj);
		}
		amdgpu_bo_unref(&obj);

	}
	return 0;
}

static int amdgpu_cgs_gmap_gpu_mem(void *cgs_device, cgs_handle_t handle,
				   uint64_t *mcaddr)
{
	int r;
	u64 min_offset, max_offset;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;

	WARN_ON_ONCE(obj->placement.num_placement > 1);

	min_offset = obj->placements[0].fpfn << PAGE_SHIFT;
	max_offset = obj->placements[0].lpfn << PAGE_SHIFT;

	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_pin_restricted(obj, AMDGPU_GEM_DOMAIN_GTT,
				     min_offset, max_offset, mcaddr);
	amdgpu_bo_unreserve(obj);
	return r;
}

static int amdgpu_cgs_gunmap_gpu_mem(void *cgs_device, cgs_handle_t handle)
{
	int r;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;
	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_unpin(obj);
	amdgpu_bo_unreserve(obj);
	return r;
}

static int amdgpu_cgs_kmap_gpu_mem(void *cgs_device, cgs_handle_t handle,
				   void **map)
{
	int r;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;
	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_kmap(obj, map);
	amdgpu_bo_unreserve(obj);
	return r;
}

static int amdgpu_cgs_kunmap_gpu_mem(void *cgs_device, cgs_handle_t handle)
{
	int r;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;
	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	amdgpu_bo_kunmap(obj);
	amdgpu_bo_unreserve(obj);
	return r;
}

static uint32_t amdgpu_cgs_read_register(void *cgs_device, unsigned offset)
{
	CGS_FUNC_ADEV;
	return RREG32(offset);
}

static void amdgpu_cgs_write_register(void *cgs_device, unsigned offset,
				      uint32_t value)
{
	CGS_FUNC_ADEV;
	WREG32(offset, value);
}

static uint32_t amdgpu_cgs_read_ind_register(void *cgs_device,
					     enum cgs_ind_reg space,
					     unsigned index)
{
	CGS_FUNC_ADEV;
	switch (space) {
	case CGS_IND_REG__MMIO:
		return RREG32_IDX(index);
	case CGS_IND_REG__PCIE:
		return RREG32_PCIE(index);
	case CGS_IND_REG__SMC:
		return RREG32_SMC(index);
	case CGS_IND_REG__UVD_CTX:
		return RREG32_UVD_CTX(index);
	case CGS_IND_REG__DIDT:
		return RREG32_DIDT(index);
	case CGS_IND_REG__AUDIO_ENDPT:
		DRM_ERROR("audio endpt register access not implemented.\n");
		return 0;
	}
	WARN(1, "Invalid indirect register space");
	return 0;
}

static void amdgpu_cgs_write_ind_register(void *cgs_device,
					  enum cgs_ind_reg space,
					  unsigned index, uint32_t value)
{
	CGS_FUNC_ADEV;
	switch (space) {
	case CGS_IND_REG__MMIO:
		return WREG32_IDX(index, value);
	case CGS_IND_REG__PCIE:
		return WREG32_PCIE(index, value);
	case CGS_IND_REG__SMC:
		return WREG32_SMC(index, value);
	case CGS_IND_REG__UVD_CTX:
		return WREG32_UVD_CTX(index, value);
	case CGS_IND_REG__DIDT:
		return WREG32_DIDT(index, value);
	case CGS_IND_REG__AUDIO_ENDPT:
		DRM_ERROR("audio endpt register access not implemented.\n");
		return;
	}
	WARN(1, "Invalid indirect register space");
}

static uint8_t amdgpu_cgs_read_pci_config_byte(void *cgs_device, unsigned addr)
{
	CGS_FUNC_ADEV;
	uint8_t val;
	int ret = pci_read_config_byte(adev->pdev, addr, &val);
	if (WARN(ret, "pci_read_config_byte error"))
		return 0;
	return val;
}

static uint16_t amdgpu_cgs_read_pci_config_word(void *cgs_device, unsigned addr)
{
	CGS_FUNC_ADEV;
	uint16_t val;
	int ret = pci_read_config_word(adev->pdev, addr, &val);
	if (WARN(ret, "pci_read_config_word error"))
		return 0;
	return val;
}

static uint32_t amdgpu_cgs_read_pci_config_dword(void *cgs_device,
						 unsigned addr)
{
	CGS_FUNC_ADEV;
	uint32_t val;
	int ret = pci_read_config_dword(adev->pdev, addr, &val);
	if (WARN(ret, "pci_read_config_dword error"))
		return 0;
	return val;
}

static void amdgpu_cgs_write_pci_config_byte(void *cgs_device, unsigned addr,
					     uint8_t value)
{
	CGS_FUNC_ADEV;
	int ret = pci_write_config_byte(adev->pdev, addr, value);
	WARN(ret, "pci_write_config_byte error");
}

static void amdgpu_cgs_write_pci_config_word(void *cgs_device, unsigned addr,
					     uint16_t value)
{
	CGS_FUNC_ADEV;
	int ret = pci_write_config_word(adev->pdev, addr, value);
	WARN(ret, "pci_write_config_word error");
}

static void amdgpu_cgs_write_pci_config_dword(void *cgs_device, unsigned addr,
					      uint32_t value)
{
	CGS_FUNC_ADEV;
	int ret = pci_write_config_dword(adev->pdev, addr, value);
	WARN(ret, "pci_write_config_dword error");
}

static const void *amdgpu_cgs_atom_get_data_table(void *cgs_device,
						  unsigned table, uint16_t *size,
						  uint8_t *frev, uint8_t *crev)
{
	CGS_FUNC_ADEV;
	uint16_t data_start;

	if (amdgpu_atom_parse_data_header(
		    adev->mode_info.atom_context, table, size,
		    frev, crev, &data_start))
		return (uint8_t*)adev->mode_info.atom_context->bios +
			data_start;

	return NULL;
}

static int amdgpu_cgs_atom_get_cmd_table_revs(void *cgs_device, unsigned table,
					      uint8_t *frev, uint8_t *crev)
{
	CGS_FUNC_ADEV;

	if (amdgpu_atom_parse_cmd_header(
		    adev->mode_info.atom_context, table,
		    frev, crev))
		return 0;

	return -EINVAL;
}

static int amdgpu_cgs_atom_exec_cmd_table(void *cgs_device, unsigned table,
					  void *args)
{
	CGS_FUNC_ADEV;

	return amdgpu_atom_execute_table(
		adev->mode_info.atom_context, table, args);
}

static int amdgpu_cgs_create_pm_request(void *cgs_device, cgs_handle_t *request)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_destroy_pm_request(void *cgs_device, cgs_handle_t request)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_set_pm_request(void *cgs_device, cgs_handle_t request,
				     int active)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_pm_request_clock(void *cgs_device, cgs_handle_t request,
				       enum cgs_clock clock, unsigned freq)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_pm_request_engine(void *cgs_device, cgs_handle_t request,
					enum cgs_engine engine, int powered)
{
	/* TODO */
	return 0;
}



static int amdgpu_cgs_pm_query_clock_limits(void *cgs_device,
					    enum cgs_clock clock,
					    struct cgs_clock_limits *limits)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_set_camera_voltages(void *cgs_device, uint32_t mask,
					  const uint32_t *voltages)
{
	DRM_ERROR("not implemented");
	return -EPERM;
}

struct cgs_irq_params {
	unsigned src_id;
	cgs_irq_source_set_func_t set;
	cgs_irq_handler_func_t handler;
	void *private_data;
};

static int cgs_set_irq_state(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *src,
			     unsigned type,
			     enum amdgpu_interrupt_state state)
{
	struct cgs_irq_params *irq_params =
		(struct cgs_irq_params *)src->data;
	if (!irq_params)
		return -EINVAL;
	if (!irq_params->set)
		return -EINVAL;
	return irq_params->set(irq_params->private_data,
			       irq_params->src_id,
			       type,
			       (int)state);
}

static int cgs_process_irq(struct amdgpu_device *adev,
			   struct amdgpu_irq_src *source,
			   struct amdgpu_iv_entry *entry)
{
	struct cgs_irq_params *irq_params =
		(struct cgs_irq_params *)source->data;
	if (!irq_params)
		return -EINVAL;
	if (!irq_params->handler)
		return -EINVAL;
	return irq_params->handler(irq_params->private_data,
				   irq_params->src_id,
				   entry->iv_entry);
}

static const struct amdgpu_irq_src_funcs cgs_irq_funcs = {
	.set = cgs_set_irq_state,
	.process = cgs_process_irq,
};

static int amdgpu_cgs_add_irq_source(void *cgs_device, unsigned src_id,
				     unsigned num_types,
				     cgs_irq_source_set_func_t set,
				     cgs_irq_handler_func_t handler,
				     void *private_data)
{
	CGS_FUNC_ADEV;
	int ret = 0;
	struct cgs_irq_params *irq_params;
	struct amdgpu_irq_src *source =
		kzalloc(sizeof(struct amdgpu_irq_src), GFP_KERNEL);
	if (!source)
		return -ENOMEM;
	irq_params =
		kzalloc(sizeof(struct cgs_irq_params), GFP_KERNEL);
	if (!irq_params) {
		kfree(source);
		return -ENOMEM;
	}
	source->num_types = num_types;
	source->funcs = &cgs_irq_funcs;
	irq_params->src_id = src_id;
	irq_params->set = set;
	irq_params->handler = handler;
	irq_params->private_data = private_data;
	source->data = (void *)irq_params;
	ret = amdgpu_irq_add_id(adev, src_id, source);
	if (ret) {
		kfree(irq_params);
		kfree(source);
	}

	return ret;
}

static int amdgpu_cgs_irq_get(void *cgs_device, unsigned src_id, unsigned type)
{
	CGS_FUNC_ADEV;
	return amdgpu_irq_get(adev, adev->irq.sources[src_id], type);
}

static int amdgpu_cgs_irq_put(void *cgs_device, unsigned src_id, unsigned type)
{
	CGS_FUNC_ADEV;
	return amdgpu_irq_put(adev, adev->irq.sources[src_id], type);
}

int amdgpu_cgs_set_clockgating_state(void *cgs_device,
				  enum amd_ip_block_type block_type,
				  enum amd_clockgating_state state)
{
	CGS_FUNC_ADEV;
	int i, r = -1;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_block_status[i].valid)
			continue;

		if (adev->ip_blocks[i].type == block_type) {
			r = adev->ip_blocks[i].funcs->set_clockgating_state(
								(void *)adev,
									state);
			break;
		}
	}
	return r;
}

int amdgpu_cgs_set_powergating_state(void *cgs_device,
				  enum amd_ip_block_type block_type,
				  enum amd_powergating_state state)
{
	CGS_FUNC_ADEV;
	int i, r = -1;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_block_status[i].valid)
			continue;

		if (adev->ip_blocks[i].type == block_type) {
			r = adev->ip_blocks[i].funcs->set_powergating_state(
								(void *)adev,
									state);
			break;
		}
	}
	return r;
}


static uint32_t fw_type_convert(void *cgs_device, uint32_t fw_type)
{
	CGS_FUNC_ADEV;
	enum AMDGPU_UCODE_ID result = AMDGPU_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case CGS_UCODE_ID_SDMA0:
		result = AMDGPU_UCODE_ID_SDMA0;
		break;
	case CGS_UCODE_ID_SDMA1:
		result = AMDGPU_UCODE_ID_SDMA1;
		break;
	case CGS_UCODE_ID_CP_CE:
		result = AMDGPU_UCODE_ID_CP_CE;
		break;
	case CGS_UCODE_ID_CP_PFP:
		result = AMDGPU_UCODE_ID_CP_PFP;
		break;
	case CGS_UCODE_ID_CP_ME:
		result = AMDGPU_UCODE_ID_CP_ME;
		break;
	case CGS_UCODE_ID_CP_MEC:
	case CGS_UCODE_ID_CP_MEC_JT1:
		result = AMDGPU_UCODE_ID_CP_MEC1;
		break;
	case CGS_UCODE_ID_CP_MEC_JT2:
		if (adev->asic_type == CHIP_TONGA)
			result = AMDGPU_UCODE_ID_CP_MEC2;
		else if (adev->asic_type == CHIP_CARRIZO)
			result = AMDGPU_UCODE_ID_CP_MEC1;
		break;
	case CGS_UCODE_ID_RLC_G:
		result = AMDGPU_UCODE_ID_RLC_G;
		break;
	default:
		DRM_ERROR("Firmware type not supported\n");
	}
	return result;
}

static int amdgpu_cgs_get_firmware_info(void *cgs_device,
					enum cgs_ucode_id type,
					struct cgs_firmware_info *info)
{
	CGS_FUNC_ADEV;

	if (CGS_UCODE_ID_SMU != type) {
		uint64_t gpu_addr;
		uint32_t data_size;
		const struct gfx_firmware_header_v1_0 *header;
		enum AMDGPU_UCODE_ID id;
		struct amdgpu_firmware_info *ucode;

		id = fw_type_convert(cgs_device, type);
		ucode = &adev->firmware.ucode[id];
		if (ucode->fw == NULL)
			return -EINVAL;

		gpu_addr  = ucode->mc_addr;
		header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
		data_size = le32_to_cpu(header->header.ucode_size_bytes);

		if ((type == CGS_UCODE_ID_CP_MEC_JT1) ||
		    (type == CGS_UCODE_ID_CP_MEC_JT2)) {
			gpu_addr += le32_to_cpu(header->jt_offset) << 2;
			data_size = le32_to_cpu(header->jt_size) << 2;
		}
		info->mc_addr = gpu_addr;
		info->image_size = data_size;
		info->version = (uint16_t)le32_to_cpu(header->header.ucode_version);
		info->feature_version = (uint16_t)le32_to_cpu(header->ucode_feature_version);
	} else {
		char fw_name[30] = {0};
		int err = 0;
		uint32_t ucode_size;
		uint32_t ucode_start_address;
		const uint8_t *src;
		const struct smc_firmware_header_v1_0 *hdr;

		switch (adev->asic_type) {
		case CHIP_TONGA:
			strcpy(fw_name, "amdgpu/tonga_smc.bin");
			break;
		default:
			DRM_ERROR("SMC firmware not supported\n");
			return -EINVAL;
		}

		err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
		if (err) {
			DRM_ERROR("Failed to request firmware\n");
			return err;
		}

		err = amdgpu_ucode_validate(adev->pm.fw);
		if (err) {
			DRM_ERROR("Failed to load firmware \"%s\"", fw_name);
			release_firmware(adev->pm.fw);
			adev->pm.fw = NULL;
			return err;
		}

		hdr = (const struct smc_firmware_header_v1_0 *)	adev->pm.fw->data;
		adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);
		ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes);
		ucode_start_address = le32_to_cpu(hdr->ucode_start_addr);
		src = (const uint8_t *)(adev->pm.fw->data +
		       le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		info->version = adev->pm.fw_version;
		info->image_size = ucode_size;
		info->kptr = (void *)src;
	}
	return 0;
}

static const struct cgs_ops amdgpu_cgs_ops = {
	amdgpu_cgs_gpu_mem_info,
	amdgpu_cgs_gmap_kmem,
	amdgpu_cgs_gunmap_kmem,
	amdgpu_cgs_alloc_gpu_mem,
	amdgpu_cgs_free_gpu_mem,
	amdgpu_cgs_gmap_gpu_mem,
	amdgpu_cgs_gunmap_gpu_mem,
	amdgpu_cgs_kmap_gpu_mem,
	amdgpu_cgs_kunmap_gpu_mem,
	amdgpu_cgs_read_register,
	amdgpu_cgs_write_register,
	amdgpu_cgs_read_ind_register,
	amdgpu_cgs_write_ind_register,
	amdgpu_cgs_read_pci_config_byte,
	amdgpu_cgs_read_pci_config_word,
	amdgpu_cgs_read_pci_config_dword,
	amdgpu_cgs_write_pci_config_byte,
	amdgpu_cgs_write_pci_config_word,
	amdgpu_cgs_write_pci_config_dword,
	amdgpu_cgs_atom_get_data_table,
	amdgpu_cgs_atom_get_cmd_table_revs,
	amdgpu_cgs_atom_exec_cmd_table,
	amdgpu_cgs_create_pm_request,
	amdgpu_cgs_destroy_pm_request,
	amdgpu_cgs_set_pm_request,
	amdgpu_cgs_pm_request_clock,
	amdgpu_cgs_pm_request_engine,
	amdgpu_cgs_pm_query_clock_limits,
	amdgpu_cgs_set_camera_voltages,
	amdgpu_cgs_get_firmware_info,
	amdgpu_cgs_set_powergating_state,
	amdgpu_cgs_set_clockgating_state
};

static const struct cgs_os_ops amdgpu_cgs_os_ops = {
	amdgpu_cgs_import_gpu_mem,
	amdgpu_cgs_add_irq_source,
	amdgpu_cgs_irq_get,
	amdgpu_cgs_irq_put
};

void *amdgpu_cgs_create_device(struct amdgpu_device *adev)
{
	struct amdgpu_cgs_device *cgs_device =
		kmalloc(sizeof(*cgs_device), GFP_KERNEL);

	if (!cgs_device) {
		DRM_ERROR("Couldn't allocate CGS device structure\n");
		return NULL;
	}

	cgs_device->base.ops = &amdgpu_cgs_ops;
	cgs_device->base.os_ops = &amdgpu_cgs_os_ops;
	cgs_device->adev = adev;

	return cgs_device;
}

void amdgpu_cgs_destroy_device(void *cgs_device)
{
	kfree(cgs_device);
}
