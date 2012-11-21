/**************************************************************************
 *
 * Copyright © 2012 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
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
 *
 **************************************************************************/

#include "vmwgfx_drv.h"

/*
 * Currently the MOB interface does not support 64-bit page frame numbers.
 * This might change in the future to be similar to the GMR2 interface
 * when virtual machines support memory beyond 16TB.
 */

#define VMW_PPN_SIZE 4

/*
 * struct vmw_mob - Structure containing page table and metadata for a
 * Guest Memory OBject.
 *
 * @num_pages       Number of pages that make up the page table.
 * @pt_level        The indirection level of the page table. 0-2.
 * @pt_root_page    Pointer to the level 0 page of the page table.
 */
struct vmw_mob {
	struct ttm_buffer_object *pt_bo;
	unsigned long num_pages;
	unsigned pt_level;
	struct page *pt_root_page;
	uint32_t id;
};

/*
 * struct vmw_otable - Guest Memory OBject table metadata
 *
 * @size:           Size of the table (page-aligned).
 * @page_table:     Pointer to a struct vmw_mob holding the page table.
 */
struct vmw_otable {
	unsigned long size;
	struct vmw_mob *page_table;
};

static int vmw_mob_pt_populate(struct vmw_private *dev_priv,
			       struct vmw_mob *mob);
static void vmw_mob_pt_setup(struct vmw_mob *mob,
			     struct page **data_pages,
			     unsigned long num_data_pages);

/*
 * vmw_setup_otable_base - Issue an object table base setup command to
 * the device
 *
 * @dev_priv:       Pointer to a device private structure
 * @type:           Type of object table base
 * @offset          Start of table offset into dev_priv::otable_bo
 * @otable          Pointer to otable metadata;
 *
 * This function returns -ENOMEM if it fails to reserve fifo space,
 * and may block waiting for fifo space.
 */
static int vmw_setup_otable_base(struct vmw_private *dev_priv,
				 SVGAOTableType type,
				 unsigned long offset,
				 struct vmw_otable *otable)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetOTableBase body;
	} *cmd;
	struct page **pages = dev_priv->otable_bo->ttm->pages +
		(offset >> PAGE_SHIFT);
	struct vmw_mob *mob;
	int ret;

	BUG_ON(otable->page_table != NULL);

	mob = vmw_mob_create(otable->size >> PAGE_SHIFT);
	if (unlikely(mob == NULL)) {
		DRM_ERROR("Failed creating OTable page table.\n");
		return -ENOMEM;
	}

	if (otable->size <= PAGE_SIZE) {
		mob->pt_level = 0;
		mob->pt_root_page = pages[0];
	} else {
		ret = vmw_mob_pt_populate(dev_priv, mob);
		if (unlikely(ret != 0))
			goto out_no_populate;

		vmw_mob_pt_setup(mob, pages,
				 otable->size >> PAGE_SHIFT);
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for OTable setup.\n");
		goto out_no_fifo;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->header.id = SVGA_3D_CMD_SET_OTABLE_BASE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.type = type;
	cmd->body.baseAddress = page_to_pfn(mob->pt_root_page);
	cmd->body.sizeInBytes = otable->size;
	cmd->body.validSizeInBytes = 0;
	cmd->body.ptDepth = mob->pt_level;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	otable->page_table = mob;

	return 0;

out_no_fifo:
out_no_populate:
	vmw_mob_destroy(mob);
	return ret;
}

/*
 * vmw_takedown_otable_base - Issue an object table base takedown command
 * to the device
 *
 * @dev_priv:       Pointer to a device private structure
 * @type:           Type of object table base
 *
 */
static void vmw_takedown_otable_base(struct vmw_private *dev_priv,
				     SVGAOTableType type,
				     struct vmw_otable *otable)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetOTableBase body;
	} *cmd;
	struct ttm_buffer_object *bo = otable->page_table->pt_bo;

	if (otable->page_table == NULL)
		return;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		DRM_ERROR("Failed reserving FIFO space for OTable setup.\n");

	memset(cmd, 0, sizeof(*cmd));
	cmd->header.id = SVGA_3D_CMD_SET_OTABLE_BASE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.type = type;
	cmd->body.baseAddress = 0;
	cmd->body.sizeInBytes = 0;
	cmd->body.validSizeInBytes = 0;
	cmd->body.ptDepth = SVGA3D_MOBFMT_INVALID;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	if (bo) {
		int ret;

		ret = ttm_bo_reserve(bo, false, true, false, false);
		BUG_ON(ret != 0);

		vmw_fence_single_bo(bo, NULL);
		ttm_bo_unreserve(bo);
	}

	vmw_mob_destroy(otable->page_table);
	otable->page_table = NULL;
}

/*
 * vmw_otables_setup - Set up guest backed memory object tables
 *
 * @dev_priv:       Pointer to a device private structure
 *
 * Takes care of the device guest backed surface
 * initialization, by setting up the guest backed memory object tables.
 * Returns 0 on success and various error codes on failure. A succesful return
 * means the object tables can be taken down using the vmw_otables_takedown
 * function.
 */
int vmw_otables_setup(struct vmw_private *dev_priv)
{
	unsigned long offset;
	unsigned long bo_size;
	struct vmw_otable *otables;
	SVGAOTableType i;
	int ret;

	otables = kzalloc(SVGA_OTABLE_COUNT * sizeof(*otables),
			  GFP_KERNEL);
	if (unlikely(otables == NULL)) {
		DRM_ERROR("Failed to allocate space for otable "
			  "metadata.\n");
		return -ENOMEM;
	}

	otables[SVGA_OTABLE_MOB].size =
		VMWGFX_NUM_MOB * SVGA3D_OTABLE_MOB_ENTRY_SIZE;
	otables[SVGA_OTABLE_SURFACE].size =
		VMWGFX_NUM_GB_SURFACE * SVGA3D_OTABLE_SURFACE_ENTRY_SIZE;
	otables[SVGA_OTABLE_CONTEXT].size =
		VMWGFX_NUM_GB_CONTEXT * SVGA3D_OTABLE_CONTEXT_ENTRY_SIZE;
	otables[SVGA_OTABLE_SHADER].size =
		VMWGFX_NUM_GB_SHADER * SVGA3D_OTABLE_SHADER_ENTRY_SIZE;

	bo_size = 0;
	for (i = 0; i < SVGA_OTABLE_COUNT; ++i) {
		otables[i].size =
			(otables[i].size + PAGE_SIZE - 1) & PAGE_MASK;
		bo_size += otables[i].size;
	}

	ret = ttm_bo_create(&dev_priv->bdev, bo_size,
			    ttm_bo_type_device,
			    &vmw_sys_ne_placement,
			    0, false, NULL,
			    &dev_priv->otable_bo);

	if (unlikely(ret != 0))
		goto out_no_bo;

	ret = ttm_bo_reserve(dev_priv->otable_bo, false, true, false, false);
	BUG_ON(ret != 0);
	ret = vmw_bo_driver.ttm_tt_populate(dev_priv->otable_bo->ttm);
	ttm_bo_unreserve(dev_priv->otable_bo);
	if (unlikely(ret != 0))
		goto out_no_setup;

	offset = 0;
	for (i = 0; i < SVGA_OTABLE_COUNT; ++i) {
		ret = vmw_setup_otable_base(dev_priv, i, offset,
					    &otables[i]);
		if (unlikely(ret != 0))
			goto out_no_setup;
		offset += otables[i].size;
	}

	dev_priv->otables = otables;
	return 0;

out_no_setup:
	for (i = 0; i < SVGA_OTABLE_COUNT; ++i)
		vmw_takedown_otable_base(dev_priv, i, &otables[i]);

	ttm_bo_unref(&dev_priv->otable_bo);
out_no_bo:
	kfree(otables);
	return ret;
}


/*
 * vmw_otables_takedown - Take down guest backed memory object tables
 *
 * @dev_priv:       Pointer to a device private structure
 *
 * Take down the Guest Memory Object tables.
 */
void vmw_otables_takedown(struct vmw_private *dev_priv)
{
	SVGAOTableType i;
	struct ttm_buffer_object *bo = dev_priv->otable_bo;
	int ret;

	for (i = 0; i < SVGA_OTABLE_COUNT; ++i)
		vmw_takedown_otable_base(dev_priv, i,
					 &dev_priv->otables[i]);

	ret = ttm_bo_reserve(bo, false, true, false, false);
	BUG_ON(ret != 0);

	vmw_fence_single_bo(bo, NULL);
	ttm_bo_unreserve(bo);

	ttm_bo_unref(&dev_priv->otable_bo);
	kfree(dev_priv->otables);
	dev_priv->otables = NULL;
}


/*
 * vmw_mob_calculate_pt_pages - Calculate the number of page table pages
 * needed for a guest backed memory object.
 *
 * @data_pages:  Number of data pages in the memory object buffer.
 */
static unsigned long vmw_mob_calculate_pt_pages(unsigned long data_pages)
{
	unsigned long data_size = data_pages * PAGE_SIZE;
	unsigned long tot_size = 0;

	while (likely(data_size > PAGE_SIZE)) {
		data_size = DIV_ROUND_UP(data_size, PAGE_SIZE);
		data_size *= VMW_PPN_SIZE;
		tot_size += (data_size + PAGE_SIZE - 1) & PAGE_MASK;
	}

	return tot_size >> PAGE_SHIFT;
}

/*
 * vmw_mob_create - Create a mob, but don't populate it.
 *
 * @data_pages:  Number of data pages of the underlying buffer object.
 */
struct vmw_mob *vmw_mob_create(unsigned long data_pages)
{
	struct vmw_mob *mob = kzalloc(sizeof(*mob), GFP_KERNEL);

	if (unlikely(mob == NULL))
		return NULL;

	mob->num_pages = vmw_mob_calculate_pt_pages(data_pages);

	return mob;
}

/*
 * vmw_mob_pt_populate - Populate the mob pagetable
 *
 * @mob:         Pointer to the mob the pagetable of which we want to
 *               populate.
 *
 * This function allocates memory to be used for the pagetable, and
 * adjusts TTM memory accounting accordingly. Returns ENOMEM if
 * memory resources aren't sufficient and may cause TTM buffer objects
 * to be swapped out by using the TTM memory accounting function.
 */
static int vmw_mob_pt_populate(struct vmw_private *dev_priv,
			       struct vmw_mob *mob)
{
	int ret;
	BUG_ON(mob->pt_bo != NULL);

	ret = ttm_bo_create(&dev_priv->bdev, mob->num_pages * PAGE_SIZE,
			    ttm_bo_type_device,
			    &vmw_sys_ne_placement,
			    0, false, NULL, &mob->pt_bo);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_reserve(mob->pt_bo, false, true, false, false);

	BUG_ON(ret != 0);
	ret = vmw_bo_driver.ttm_tt_populate(mob->pt_bo->ttm);
	ttm_bo_unreserve(mob->pt_bo);
	if (unlikely(ret != 0))
		ttm_bo_unref(&mob->pt_bo);

	return ret;
}


/*
 * vmw_mob_build_pt - Build a pagetable
 *
 * @data_pages:     Array of page pointers to the underlying buffer
 *                  object's data pages.
 * @num_data_pages: Number of buffer object data pages.
 * @pt_pages:       Array of page pointers to the page table pages.
 *
 * Returns the number of page table pages actually used.
 * Uses atomic kmaps of highmem pages to avoid TLB thrashing.
 */
static unsigned long vmw_mob_build_pt(struct page **data_pages,
				      unsigned long num_data_pages,
				      struct page **pt_pages)
{
	unsigned long pt_size = num_data_pages * VMW_PPN_SIZE;
	unsigned long num_pt_pages = DIV_ROUND_UP(pt_size, PAGE_SIZE);
	unsigned long pt_page, data_page;
	uint32_t *addr, *save_addr;
	unsigned long i;

	data_page = 0;
	for (pt_page = 0; pt_page < num_pt_pages; ++pt_page) {
		save_addr = addr = kmap_atomic(pt_pages[pt_page]);

		for (i = 0; i < PAGE_SIZE / VMW_PPN_SIZE; ++i) {
			*addr++ = page_to_pfn(data_pages[data_page++]);
			if (unlikely(data_page >= num_data_pages))
				break;
		}
		kunmap_atomic(save_addr);
	}

	return num_pt_pages;
}

/*
 * vmw_mob_build_pt - Set up a multilevel mob pagetable
 *
 * @mob:            Pointer to a mob whose page table needs setting up.
 * @data_pages      Array of page pointers to the buffer object's data
 *                  pages.
 * @num_data_pages: Number of buffer object data pages.
 *
 * Uses tail recursion to set up a multilevel mob page table.
 */
static void vmw_mob_pt_setup(struct vmw_mob *mob,
			     struct page **data_pages,
			     unsigned long num_data_pages)
{
	struct page **pt_pages;
	unsigned long num_pt_pages = 0;
	struct ttm_buffer_object *bo = mob->pt_bo;
	int ret;

	ret = ttm_bo_reserve(bo, false, true, false, 0);
	BUG_ON(ret != 0);

	pt_pages = bo->ttm->pages;
	mob->pt_level = 0;
	while (likely(num_data_pages > 1)) {
		++mob->pt_level;
		BUG_ON(mob->pt_level > 2);

		pt_pages += num_pt_pages;
		num_pt_pages = vmw_mob_build_pt(data_pages, num_data_pages,
						pt_pages);
		data_pages = pt_pages;
		num_data_pages = num_pt_pages;
	}

	mob->pt_root_page = *pt_pages;
	ttm_bo_unreserve(bo);
}

/*
 * vmw_mob_destroy - Destroy a mob, unpopulating first if necessary.
 *
 * @mob:            Pointer to a mob to destroy.
 */
void vmw_mob_destroy(struct vmw_mob *mob)
{
	if (mob->pt_bo)
		ttm_bo_unref(&mob->pt_bo);
	kfree(mob);
}

/*
 * vmw_mob_unbind - Hide a mob from the device.
 *
 * @dev_priv:       Pointer to a device private.
 * @mob_id:         Device id of the mob to unbind.
 */
void vmw_mob_unbind(struct vmw_private *dev_priv,
		    struct vmw_mob *mob)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyGBMob body;
	} *cmd;
	int ret;
	struct ttm_buffer_object *bo = mob->pt_bo;

	if (bo) {
		ret = ttm_bo_reserve(bo, false, true, false, 0);
		/*
		 * Noone else should be using this buffer.
		 */
		BUG_ON(ret != 0);
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for Memory "
			  "Object unbinding.\n");
	}
	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_MOB;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.mobid = mob->id;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	if (bo) {
		vmw_fence_single_bo(bo, NULL);
		ttm_bo_unreserve(bo);
	}
	vmw_3d_resource_dec(dev_priv, false);
}

/*
 * vmw_mob_bind - Make a mob visible to the device after first
 *                populating it if necessary.
 *
 * @dev_priv:       Pointer to a device private.
 * @mob:            Pointer to the mob we're making visible.
 * @data_pages:     Array of pointers to the data pages of the underlying
 *                  buffer object.
 * @num_data_pages: Number of data pages of the underlying buffer
 *                  object.
 * @mob_id:         Device id of the mob to bind
 *
 * This function is intended to be interfaced with the ttm_tt backend
 * code.
 */
int vmw_mob_bind(struct vmw_private *dev_priv,
		 struct vmw_mob *mob,
		 struct page **data_pages,
		 unsigned long num_data_pages,
		 int32_t mob_id)
{
	int ret;
	bool pt_set_up = false;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBMob body;
	} *cmd;

	mob->id = mob_id;
	if (likely(num_data_pages == 1)) {
		mob->pt_level = 0;
		mob->pt_root_page = *data_pages;
	} else if (unlikely(mob->pt_bo == NULL)) {
		ret = vmw_mob_pt_populate(dev_priv, mob);
		if (unlikely(ret != 0))
			return ret;

		vmw_mob_pt_setup(mob, data_pages, num_data_pages);
		pt_set_up = true;
	}

	(void) vmw_3d_resource_inc(dev_priv, false);

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for Memory "
			  "Object binding.\n");
		goto out_no_cmd_space;
	}

	cmd->header.id = SVGA_3D_CMD_DEFINE_GB_MOB;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.mobid = mob_id;
	cmd->body.ptDepth = mob->pt_level;
	cmd->body.base = page_to_pfn(mob->pt_root_page);
	cmd->body.sizeInBytes = num_data_pages * PAGE_SIZE;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;

out_no_cmd_space:
	vmw_3d_resource_dec(dev_priv, false);
	if (pt_set_up)
		ttm_bo_unref(&mob->pt_bo);

	return -ENOMEM;
}
