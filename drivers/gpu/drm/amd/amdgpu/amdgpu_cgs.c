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
#include <linux/pci.h>
#include "amdgpu.h"
#include "cgs_linux.h"
#include "atom.h"

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
	return 0;
}

static int amdgpu_cgs_gmap_kmem(void *cgs_device, void *kmem,
				uint64_t size,
				uint64_t min_offset, uint64_t max_offset,
				cgs_handle_t *kmem_handle, uint64_t *mcaddr)
{
	return 0;
}

static int amdgpu_cgs_gunmap_kmem(void *cgs_device, cgs_handle_t kmem_handle)
{
	return 0;
}

static int amdgpu_cgs_alloc_gpu_mem(void *cgs_device,
				    enum cgs_gpu_mem_type type,
				    uint64_t size, uint64_t align,
				    uint64_t min_offset, uint64_t max_offset,
				    cgs_handle_t *handle)
{
	return 0;
}

static int amdgpu_cgs_import_gpu_mem(void *cgs_device, int dmabuf_fd,
				     cgs_handle_t *handle)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_free_gpu_mem(void *cgs_device, cgs_handle_t handle)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_gmap_gpu_mem(void *cgs_device, cgs_handle_t handle,
				   uint64_t *mcaddr)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_gunmap_gpu_mem(void *cgs_device, cgs_handle_t handle)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_kmap_gpu_mem(void *cgs_device, cgs_handle_t handle,
				   void **map)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_kunmap_gpu_mem(void *cgs_device, cgs_handle_t handle)
{
	/* TODO */
	return 0;
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
	amdgpu_cgs_set_camera_voltages
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
