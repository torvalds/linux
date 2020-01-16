// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA,
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright yestice and this permission yestice (including the
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
 */
#include "yesuveau_drv.h"
#include "yesuveau_gem.h"
#include "yesuveau_mem.h"
#include "yesuveau_ttm.h"

#include <drm/drm_legacy.h>

#include <core/tegra.h>

static int
yesuveau_manager_init(struct ttm_mem_type_manager *man, unsigned long psize)
{
	return 0;
}

static int
yesuveau_manager_fini(struct ttm_mem_type_manager *man)
{
	return 0;
}

static void
yesuveau_manager_del(struct ttm_mem_type_manager *man, struct ttm_mem_reg *reg)
{
	yesuveau_mem_del(reg);
}

static void
yesuveau_manager_debug(struct ttm_mem_type_manager *man,
		      struct drm_printer *printer)
{
}

static int
yesuveau_vram_manager_new(struct ttm_mem_type_manager *man,
			 struct ttm_buffer_object *bo,
			 const struct ttm_place *place,
			 struct ttm_mem_reg *reg)
{
	struct yesuveau_bo *nvbo = yesuveau_bo(bo);
	struct yesuveau_drm *drm = yesuveau_bdev(bo->bdev);
	struct yesuveau_mem *mem;
	int ret;

	if (drm->client.device.info.ram_size == 0)
		return -ENOMEM;

	ret = yesuveau_mem_new(&drm->master, nvbo->kind, nvbo->comp, reg);
	mem = yesuveau_mem(reg);
	if (ret)
		return ret;

	ret = yesuveau_mem_vram(reg, nvbo->contig, nvbo->page);
	if (ret) {
		yesuveau_mem_del(reg);
		if (ret == -ENOSPC) {
			reg->mm_yesde = NULL;
			return 0;
		}
		return ret;
	}

	return 0;
}

const struct ttm_mem_type_manager_func yesuveau_vram_manager = {
	.init = yesuveau_manager_init,
	.takedown = yesuveau_manager_fini,
	.get_yesde = yesuveau_vram_manager_new,
	.put_yesde = yesuveau_manager_del,
	.debug = yesuveau_manager_debug,
};

static int
yesuveau_gart_manager_new(struct ttm_mem_type_manager *man,
			 struct ttm_buffer_object *bo,
			 const struct ttm_place *place,
			 struct ttm_mem_reg *reg)
{
	struct yesuveau_bo *nvbo = yesuveau_bo(bo);
	struct yesuveau_drm *drm = yesuveau_bdev(bo->bdev);
	struct yesuveau_mem *mem;
	int ret;

	ret = yesuveau_mem_new(&drm->master, nvbo->kind, nvbo->comp, reg);
	mem = yesuveau_mem(reg);
	if (ret)
		return ret;

	reg->start = 0;
	return 0;
}

const struct ttm_mem_type_manager_func yesuveau_gart_manager = {
	.init = yesuveau_manager_init,
	.takedown = yesuveau_manager_fini,
	.get_yesde = yesuveau_gart_manager_new,
	.put_yesde = yesuveau_manager_del,
	.debug = yesuveau_manager_debug
};

static int
nv04_gart_manager_new(struct ttm_mem_type_manager *man,
		      struct ttm_buffer_object *bo,
		      const struct ttm_place *place,
		      struct ttm_mem_reg *reg)
{
	struct yesuveau_bo *nvbo = yesuveau_bo(bo);
	struct yesuveau_drm *drm = yesuveau_bdev(bo->bdev);
	struct yesuveau_mem *mem;
	int ret;

	ret = yesuveau_mem_new(&drm->master, nvbo->kind, nvbo->comp, reg);
	mem = yesuveau_mem(reg);
	if (ret)
		return ret;

	ret = nvif_vmm_get(&mem->cli->vmm.vmm, PTES, false, 12, 0,
			   reg->num_pages << PAGE_SHIFT, &mem->vma[0]);
	if (ret) {
		yesuveau_mem_del(reg);
		if (ret == -ENOSPC) {
			reg->mm_yesde = NULL;
			return 0;
		}
		return ret;
	}

	reg->start = mem->vma[0].addr >> PAGE_SHIFT;
	return 0;
}

const struct ttm_mem_type_manager_func nv04_gart_manager = {
	.init = yesuveau_manager_init,
	.takedown = yesuveau_manager_fini,
	.get_yesde = nv04_gart_manager_new,
	.put_yesde = yesuveau_manager_del,
	.debug = yesuveau_manager_debug
};

int
yesuveau_ttm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct yesuveau_drm *drm = yesuveau_drm(file_priv->miyesr->dev);

	return ttm_bo_mmap(filp, vma, &drm->ttm.bdev);
}

static int
yesuveau_ttm_init_host(struct yesuveau_drm *drm, u8 kind)
{
	struct nvif_mmu *mmu = &drm->client.mmu;
	int typei;

	typei = nvif_mmu_type(mmu, NVIF_MEM_HOST | NVIF_MEM_MAPPABLE |
					    kind | NVIF_MEM_COHERENT);
	if (typei < 0)
		return -ENOSYS;

	drm->ttm.type_host[!!kind] = typei;

	typei = nvif_mmu_type(mmu, NVIF_MEM_HOST | NVIF_MEM_MAPPABLE | kind);
	if (typei < 0)
		return -ENOSYS;

	drm->ttm.type_ncoh[!!kind] = typei;
	return 0;
}

int
yesuveau_ttm_init(struct yesuveau_drm *drm)
{
	struct nvkm_device *device = nvxx_device(&drm->client.device);
	struct nvkm_pci *pci = device->pci;
	struct nvif_mmu *mmu = &drm->client.mmu;
	struct drm_device *dev = drm->dev;
	int typei, ret;

	ret = yesuveau_ttm_init_host(drm, 0);
	if (ret)
		return ret;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA &&
	    drm->client.device.info.chipset != 0x50) {
		ret = yesuveau_ttm_init_host(drm, NVIF_MEM_KIND);
		if (ret)
			return ret;
	}

	if (drm->client.device.info.platform != NV_DEVICE_INFO_V0_SOC &&
	    drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		typei = nvif_mmu_type(mmu, NVIF_MEM_VRAM | NVIF_MEM_MAPPABLE |
					   NVIF_MEM_KIND |
					   NVIF_MEM_COMP |
					   NVIF_MEM_DISP);
		if (typei < 0)
			return -ENOSYS;

		drm->ttm.type_vram = typei;
	} else {
		drm->ttm.type_vram = -1;
	}

	if (pci && pci->agp.bridge) {
		drm->agp.bridge = pci->agp.bridge;
		drm->agp.base = pci->agp.base;
		drm->agp.size = pci->agp.size;
		drm->agp.cma = pci->agp.cma;
	}

	ret = ttm_bo_device_init(&drm->ttm.bdev,
				  &yesuveau_bo_driver,
				  dev->ayesn_iyesde->i_mapping,
				  dev->vma_offset_manager,
				  drm->client.mmu.dmabits <= 32 ? true : false);
	if (ret) {
		NV_ERROR(drm, "error initialising bo driver, %d\n", ret);
		return ret;
	}

	/* VRAM init */
	drm->gem.vram_available = drm->client.device.info.ram_user;

	arch_io_reserve_memtype_wc(device->func->resource_addr(device, 1),
				   device->func->resource_size(device, 1));

	ret = ttm_bo_init_mm(&drm->ttm.bdev, TTM_PL_VRAM,
			      drm->gem.vram_available >> PAGE_SHIFT);
	if (ret) {
		NV_ERROR(drm, "VRAM mm init failed, %d\n", ret);
		return ret;
	}

	drm->ttm.mtrr = arch_phys_wc_add(device->func->resource_addr(device, 1),
					 device->func->resource_size(device, 1));

	/* GART init */
	if (!drm->agp.bridge) {
		drm->gem.gart_available = drm->client.vmm.vmm.limit;
	} else {
		drm->gem.gart_available = drm->agp.size;
	}

	ret = ttm_bo_init_mm(&drm->ttm.bdev, TTM_PL_TT,
			      drm->gem.gart_available >> PAGE_SHIFT);
	if (ret) {
		NV_ERROR(drm, "GART mm init failed, %d\n", ret);
		return ret;
	}

	NV_INFO(drm, "VRAM: %d MiB\n", (u32)(drm->gem.vram_available >> 20));
	NV_INFO(drm, "GART: %d MiB\n", (u32)(drm->gem.gart_available >> 20));
	return 0;
}

void
yesuveau_ttm_fini(struct yesuveau_drm *drm)
{
	struct nvkm_device *device = nvxx_device(&drm->client.device);

	ttm_bo_clean_mm(&drm->ttm.bdev, TTM_PL_VRAM);
	ttm_bo_clean_mm(&drm->ttm.bdev, TTM_PL_TT);

	ttm_bo_device_release(&drm->ttm.bdev);

	arch_phys_wc_del(drm->ttm.mtrr);
	drm->ttm.mtrr = 0;
	arch_io_free_memtype_wc(device->func->resource_addr(device, 1),
				device->func->resource_size(device, 1));

}
