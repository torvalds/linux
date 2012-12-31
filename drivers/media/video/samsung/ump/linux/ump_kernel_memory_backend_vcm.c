/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* create by boojin.kim@samsung.com */
/* needed to detect kernel version specific code */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else /* pre 2.6.26 the file was in the arch specific location */
#include <asm/semaphore.h>
#endif

#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include "ump_kernel_common.h"
#include "ump_kernel_memory_backend.h"
#include "ump_kernel_interface_ref_drv.h"
#include "ump_kernel_memory_backend_vcm.h"
#include "../common/ump_uk_types.h"
#include <linux/vcm-drv.h>
#include <plat/s5p-vcm.h>
#include <linux/dma-mapping.h>

#define UMP_REF_DRV_UK_VCM_DEV_G2D 12

typedef struct ump_vcm {
	struct vcm *vcm;
	struct vcm_res  *vcm_res;
	unsigned int dev_id;
} ump_vcm;

typedef struct vcm_allocator {
	struct semaphore mutex;
	u32 num_vcm_blocks;
} vcm_allocator;

static void ump_vcm_free(void* ctx, ump_dd_mem * descriptor);
static int ump_vcm_allocate(void* ctx, ump_dd_mem * descriptor);
static void *vcm_res_get(ump_dd_mem *mem, void* args);
static void vcm_attr_set(ump_dd_mem *mem, void* args);
static int vcm_mem_allocator(vcm_allocator *info, ump_dd_mem *descriptor);
static void vcm_memory_backend_destroy(ump_memory_backend * backend);


/*
 * Create VCM memory backend
 */
ump_memory_backend * ump_vcm_memory_backend_create(const int max_allocation)
{
	ump_memory_backend * backend;
	vcm_allocator * info;

	info = kmalloc(sizeof(vcm_allocator), GFP_KERNEL);
	if (NULL == info)
	{
		return NULL;
	}
	
	info->num_vcm_blocks = 0;
	

	sema_init(&info->mutex, 1);

	backend = kmalloc(sizeof(ump_memory_backend), GFP_KERNEL);
	if (NULL == backend)
	{
		kfree(info);
		return NULL;
	}

	backend->ctx = info;
	backend->allocate = ump_vcm_allocate;
	backend->release = ump_vcm_free;
	backend->shutdown = vcm_memory_backend_destroy;
	backend->pre_allocate_physical_check = NULL;
	backend->adjust_to_mali_phys = NULL;
	
	backend->get = vcm_res_get;
	backend->set = vcm_attr_set;


	return backend;
}

/*
 * Destroy specified VCM memory backend
 */
static void vcm_memory_backend_destroy(ump_memory_backend * backend)
{
	vcm_allocator * info = (vcm_allocator*)backend->ctx;
#if 0
	DBG_MSG_IF(1, 0 != info->num_pages_allocated, ("%d pages still in use during shutdown\n", info->num_pages_allocated));
#endif
	kfree(info);
	kfree(backend);
}

/*
 * Allocate UMP memory
 */
static int ump_vcm_allocate(void *ctx, ump_dd_mem * descriptor)
{
	int ret;		/* success */
	vcm_allocator *info;
	struct ump_vcm *ump_vcm;

	BUG_ON(!descriptor);
	BUG_ON(!ctx);

	info = (vcm_allocator*)ctx;

	ump_vcm = kmalloc(sizeof(struct ump_vcm), GFP_KERNEL);
	if (NULL == ump_vcm)
	{
		return 0;
	}

	ump_vcm->dev_id = (int)descriptor->backend_info & ~UMP_REF_DRV_UK_CONSTRAINT_USE_CACHE;

	if(ump_vcm->dev_id == UMP_REF_DRV_UK_CONSTRAINT_NONE) { 	/* None */
		ump_vcm->dev_id = UMP_REF_DRV_UK_VCM_DEV_G2D; 	/* this ID is G2D */
	}
	else if(ump_vcm->dev_id == UMP_REF_DRV_UK_CONSTRAINT_PHYSICALLY_LINEAR) { /* Physical Linear */
		return 0;
	}
	else {				/* Other VCM */
		ump_vcm->dev_id -= 2;
	}
	
	DBG_MSG(5, ("Device ID for VCM : %d\n", ump_vcm->dev_id));
	ump_vcm->vcm = vcm_find_vcm(ump_vcm->dev_id);

	if (!ump_vcm->vcm)
	{
		return 0;
	}
	descriptor->backend_info = (void*)ump_vcm;
	
	if (down_interruptible(&info->mutex)) {
		DBG_MSG(1, ("Failed to get mutex in ump_vcm_allocate\n"));
		return 0;	/* failure */
	}

	ret = vcm_mem_allocator(info, descriptor);
	up(&info->mutex);

	return ret;		/* success */
}

static int vcm_mem_allocator(vcm_allocator *info, ump_dd_mem *descriptor)
{
	unsigned long num_blocks;
	int i;
	struct vcm_phys *phys;
	struct vcm_phys_part *part;
	int size_total = 0;
	struct ump_vcm *ump_vcm;

	ump_vcm = (struct ump_vcm*)descriptor->backend_info;
	
	ump_vcm->vcm_res =
	    vcm_make_binding(ump_vcm->vcm, descriptor->size_bytes,
	    ump_vcm->dev_id, 0);

	phys = ump_vcm->vcm_res->phys;
	part = phys->parts;
	num_blocks = phys->count;

	DBG_MSG(5,
		("Allocating page array. Size: %lu, VCM Reservation : 0x%x\n",
		 phys->count * sizeof(ump_dd_physical_block),
		 ump_vcm->vcm_res->start));

	/* Now, make a copy of the block information supplied by the user */
	descriptor->block_array =
	    (ump_dd_physical_block *) vmalloc(sizeof(ump_dd_physical_block) *
					      num_blocks);

	if (NULL == descriptor->block_array) {
		vfree(descriptor->block_array);
		DBG_MSG(1, ("Could not allocate a mem handle for function.\n"));
		return 0; /* failure */
	}

	for (i = 0; i < num_blocks; i++) {
		descriptor->block_array[i].addr = part->start;
		descriptor->block_array[i].size = part->size;

		dmac_unmap_area(phys_to_virt(part->start), part->size, DMA_FROM_DEVICE);
		outer_inv_range(part->start, part->start + part->size);

		++part;
		size_total += descriptor->block_array[i].size;
		DBG_MSG(6,
			("UMP memory created with VCM. addr 0x%x, size: 0x%x\n",
			 descriptor->block_array[i].addr,
			 descriptor->block_array[i].size));
	}

	descriptor->size_bytes = size_total;
	descriptor->nr_blocks = num_blocks;
	descriptor->ctx = NULL;

	info->num_vcm_blocks += num_blocks;
	return 1;
}

/*
 * Free specified UMP memory
 */
static void ump_vcm_free(void *ctx, ump_dd_mem * descriptor)
{
	struct ump_vcm *ump_vcm;
	vcm_allocator *info;

	BUG_ON(!descriptor);
	BUG_ON(!ctx);

	ump_vcm = (struct ump_vcm*)descriptor->backend_info;
	info = (vcm_allocator*)ctx;

	BUG_ON(descriptor->nr_blocks > info->num_vcm_blocks);

	if (down_interruptible(&info->mutex)) {
		DBG_MSG(1, ("Failed to get mutex in ump_vcm_free\n"));
		return;
	}

	DBG_MSG(5, ("Releasing %lu VCM pages\n", descriptor->nr_blocks));

	info->num_vcm_blocks -= descriptor->nr_blocks;

	up(&info->mutex);

	DBG_MSG(6, ("Freeing physical page by VCM\n"));
	vcm_destroy_binding(ump_vcm->vcm_res);
	ump_vcm->vcm = NULL;
	ump_vcm->vcm_res = NULL;

	kfree(ump_vcm);
	vfree(descriptor->block_array);
}

static void *vcm_res_get(ump_dd_mem *mem, void *args)
{
	struct ump_vcm *ump_vcm;
	enum vcm_dev_id vcm_id;

	ump_vcm = (struct ump_vcm*)mem->backend_info;
	vcm_id = (enum vcm_dev_id)args;

	if (vcm_reservation_in_vcm
		(vcm_find_vcm(vcm_id), ump_vcm->vcm_res)
		== S5PVCM_RES_NOT_IN_VCM)
		return NULL;
	else
		return ump_vcm->vcm_res;
}

static void vcm_attr_set(ump_dd_mem *mem, void *args)
{
	struct ump_vcm *ump_vcm, *ump_vcmh;

	ump_vcm = (struct ump_vcm*)args;

	ump_vcmh = kmalloc(sizeof(struct ump_vcm), GFP_KERNEL);
        if (NULL == ump_vcmh)
	{
		return;
	}

	ump_vcmh->dev_id = ump_vcm->dev_id;
	ump_vcmh->vcm = ump_vcm->vcm;
	ump_vcmh->vcm_res = ump_vcm->vcm_res;

	mem->backend_info= (void*)ump_vcmh;

	return;
}


