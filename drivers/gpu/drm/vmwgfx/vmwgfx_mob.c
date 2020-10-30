// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2012-2015 VMware, Inc., Palo Alto, CA., USA
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

#include <linux/highmem.h>

#include "vmwgfx_drv.h"

/*
 * If we set up the screen target otable, screen objects stop working.
 */

#define VMW_OTABLE_SETUP_SUB ((VMWGFX_ENABLE_SCREEN_TARGET_OTABLE ? 0 : 1))

#ifdef CONFIG_64BIT
#define VMW_PPN_SIZE 8
#define VMW_MOBFMT_PTDEPTH_0 SVGA3D_MOBFMT_PTDEPTH64_0
#define VMW_MOBFMT_PTDEPTH_1 SVGA3D_MOBFMT_PTDEPTH64_1
#define VMW_MOBFMT_PTDEPTH_2 SVGA3D_MOBFMT_PTDEPTH64_2
#else
#define VMW_PPN_SIZE 4
#define VMW_MOBFMT_PTDEPTH_0 SVGA3D_MOBFMT_PTDEPTH_0
#define VMW_MOBFMT_PTDEPTH_1 SVGA3D_MOBFMT_PTDEPTH_1
#define VMW_MOBFMT_PTDEPTH_2 SVGA3D_MOBFMT_PTDEPTH_2
#endif

/*
 * struct vmw_mob - Structure containing page table and metadata for a
 * Guest Memory OBject.
 *
 * @num_pages       Number of pages that make up the page table.
 * @pt_level        The indirection level of the page table. 0-2.
 * @pt_root_page    DMA address of the level 0 page of the page table.
 */
struct vmw_mob {
	struct ttm_buffer_object *pt_bo;
	unsigned long num_pages;
	unsigned pt_level;
	dma_addr_t pt_root_page;
	uint32_t id;
};

/*
 * struct vmw_otable - Guest Memory OBject table metadata
 *
 * @size:           Size of the table (page-aligned).
 * @page_table:     Pointer to a struct vmw_mob holding the page table.
 */
static const struct vmw_otable pre_dx_tables[] = {
	{VMWGFX_NUM_MOB * SVGA3D_OTABLE_MOB_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_SURFACE * SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_CONTEXT * SVGA3D_OTABLE_CONTEXT_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_SHADER * SVGA3D_OTABLE_SHADER_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_SCREEN_TARGET * SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE,
	 NULL, VMWGFX_ENABLE_SCREEN_TARGET_OTABLE}
};

static const struct vmw_otable dx_tables[] = {
	{VMWGFX_NUM_MOB * SVGA3D_OTABLE_MOB_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_SURFACE * SVGA3D_OTABLE_SURFACE_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_CONTEXT * SVGA3D_OTABLE_CONTEXT_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_SHADER * SVGA3D_OTABLE_SHADER_ENTRY_SIZE, NULL, true},
	{VMWGFX_NUM_GB_SCREEN_TARGET * SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE,
	 NULL, VMWGFX_ENABLE_SCREEN_TARGET_OTABLE},
	{VMWGFX_NUM_DXCONTEXT * sizeof(SVGAOTableDXContextEntry), NULL, true},
};

static int vmw_mob_pt_populate(struct vmw_private *dev_priv,
			       struct vmw_mob *mob);
static void vmw_mob_pt_setup(struct vmw_mob *mob,
			     struct vmw_piter data_iter,
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
				 struct ttm_buffer_object *otable_bo,
				 unsigned long offset,
				 struct vmw_otable *otable)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetOTableBase64 body;
	} *cmd;
	struct vmw_mob *mob;
	const struct vmw_sg_table *vsgt;
	struct vmw_piter iter;
	int ret;

	BUG_ON(otable->page_table != NULL);

	vsgt = vmw_bo_sg_table(otable_bo);
	vmw_piter_start(&iter, vsgt, offset >> PAGE_SHIFT);
	WARN_ON(!vmw_piter_next(&iter));

	mob = vmw_mob_create(otable->size >> PAGE_SHIFT);
	if (unlikely(mob == NULL)) {
		DRM_ERROR("Failed creating OTable page table.\n");
		return -ENOMEM;
	}

	if (otable->size <= PAGE_SIZE) {
		mob->pt_level = VMW_MOBFMT_PTDEPTH_0;
		mob->pt_root_page = vmw_piter_dma_addr(&iter);
	} else if (vsgt->num_regions == 1) {
		mob->pt_level = SVGA3D_MOBFMT_RANGE;
		mob->pt_root_page = vmw_piter_dma_addr(&iter);
	} else {
		ret = vmw_mob_pt_populate(dev_priv, mob);
		if (unlikely(ret != 0))
			goto out_no_populate;

		vmw_mob_pt_setup(mob, iter, otable->size >> PAGE_SHIFT);
		mob->pt_level += VMW_MOBFMT_PTDEPTH_1 - SVGA3D_MOBFMT_PTDEPTH_1;
	}

	cmd = VMW_FIFO_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->header.id = SVGA_3D_CMD_SET_OTABLE_BASE64;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.type = type;
	cmd->body.baseAddress = mob->pt_root_page >> PAGE_SHIFT;
	cmd->body.sizeInBytes = otable->size;
	cmd->body.validSizeInBytes = 0;
	cmd->body.ptDepth = mob->pt_level;

	/*
	 * The device doesn't support this, But the otable size is
	 * determined at compile-time, so this BUG shouldn't trigger
	 * randomly.
	 */
	BUG_ON(mob->pt_level == VMW_MOBFMT_PTDEPTH_2);

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
	struct ttm_buffer_object *bo;

	if (otable->page_table == NULL)
		return;

	bo = otable->page_table->pt_bo;
	cmd = VMW_FIFO_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return;

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

		ret = ttm_bo_reserve(bo, false, true, NULL);
		BUG_ON(ret != 0);

		vmw_bo_fence_single(bo, NULL);
		ttm_bo_unreserve(bo);
	}

	vmw_mob_destroy(otable->page_table);
	otable->page_table = NULL;
}


static int vmw_otable_batch_setup(struct vmw_private *dev_priv,
				  struct vmw_otable_batch *batch)
{
	unsigned long offset;
	unsigned long bo_size;
	struct vmw_otable *otables = batch->otables;
	SVGAOTableType i;
	int ret;

	bo_size = 0;
	for (i = 0; i < batch->num_otables; ++i) {
		if (!otables[i].enabled)
			continue;

		otables[i].size =
			(otables[i].size + PAGE_SIZE - 1) & PAGE_MASK;
		bo_size += otables[i].size;
	}

	ret = vmw_bo_create_and_populate(dev_priv, bo_size, &batch->otable_bo);
	if (unlikely(ret != 0))
		return ret;

	offset = 0;
	for (i = 0; i < batch->num_otables; ++i) {
		if (!batch->otables[i].enabled)
			continue;

		ret = vmw_setup_otable_base(dev_priv, i, batch->otable_bo,
					    offset,
					    &otables[i]);
		if (unlikely(ret != 0))
			goto out_no_setup;
		offset += otables[i].size;
	}

	return 0;

out_no_setup:
	for (i = 0; i < batch->num_otables; ++i) {
		if (batch->otables[i].enabled)
			vmw_takedown_otable_base(dev_priv, i,
						 &batch->otables[i]);
	}

	ttm_bo_put(batch->otable_bo);
	batch->otable_bo = NULL;
	return ret;
}

/*
 * vmw_otables_setup - Set up guest backed memory object tables
 *
 * @dev_priv:       Pointer to a device private structure
 *
 * Takes care of the device guest backed surface
 * initialization, by setting up the guest backed memory object tables.
 * Returns 0 on success and various error codes on failure. A successful return
 * means the object tables can be taken down using the vmw_otables_takedown
 * function.
 */
int vmw_otables_setup(struct vmw_private *dev_priv)
{
	struct vmw_otable **otables = &dev_priv->otable_batch.otables;
	int ret;

	if (has_sm4_context(dev_priv)) {
		*otables = kmemdup(dx_tables, sizeof(dx_tables), GFP_KERNEL);
		if (!(*otables))
			return -ENOMEM;

		dev_priv->otable_batch.num_otables = ARRAY_SIZE(dx_tables);
	} else {
		*otables = kmemdup(pre_dx_tables, sizeof(pre_dx_tables),
				   GFP_KERNEL);
		if (!(*otables))
			return -ENOMEM;

		dev_priv->otable_batch.num_otables = ARRAY_SIZE(pre_dx_tables);
	}

	ret = vmw_otable_batch_setup(dev_priv, &dev_priv->otable_batch);
	if (unlikely(ret != 0))
		goto out_setup;

	return 0;

out_setup:
	kfree(*otables);
	return ret;
}

static void vmw_otable_batch_takedown(struct vmw_private *dev_priv,
			       struct vmw_otable_batch *batch)
{
	SVGAOTableType i;
	struct ttm_buffer_object *bo = batch->otable_bo;
	int ret;

	for (i = 0; i < batch->num_otables; ++i)
		if (batch->otables[i].enabled)
			vmw_takedown_otable_base(dev_priv, i,
						 &batch->otables[i]);

	ret = ttm_bo_reserve(bo, false, true, NULL);
	BUG_ON(ret != 0);

	vmw_bo_fence_single(bo, NULL);
	ttm_bo_unreserve(bo);

	ttm_bo_put(batch->otable_bo);
	batch->otable_bo = NULL;
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
	vmw_otable_batch_takedown(dev_priv, &dev_priv->otable_batch);
	kfree(dev_priv->otable_batch.otables);
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

	if (unlikely(!mob))
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
	BUG_ON(mob->pt_bo != NULL);

	return vmw_bo_create_and_populate(dev_priv, mob->num_pages * PAGE_SIZE, &mob->pt_bo);
}

/**
 * vmw_mob_assign_ppn - Assign a value to a page table entry
 *
 * @addr: Pointer to pointer to page table entry.
 * @val: The page table entry
 *
 * Assigns a value to a page table entry pointed to by *@addr and increments
 * *@addr according to the page table entry size.
 */
#if (VMW_PPN_SIZE == 8)
static void vmw_mob_assign_ppn(u32 **addr, dma_addr_t val)
{
	*((u64 *) *addr) = val >> PAGE_SHIFT;
	*addr += 2;
}
#else
static void vmw_mob_assign_ppn(u32 **addr, dma_addr_t val)
{
	*(*addr)++ = val >> PAGE_SHIFT;
}
#endif

/*
 * vmw_mob_build_pt - Build a pagetable
 *
 * @data_addr:      Array of DMA addresses to the underlying buffer
 *                  object's data pages.
 * @num_data_pages: Number of buffer object data pages.
 * @pt_pages:       Array of page pointers to the page table pages.
 *
 * Returns the number of page table pages actually used.
 * Uses atomic kmaps of highmem pages to avoid TLB thrashing.
 */
static unsigned long vmw_mob_build_pt(struct vmw_piter *data_iter,
				      unsigned long num_data_pages,
				      struct vmw_piter *pt_iter)
{
	unsigned long pt_size = num_data_pages * VMW_PPN_SIZE;
	unsigned long num_pt_pages = DIV_ROUND_UP(pt_size, PAGE_SIZE);
	unsigned long pt_page;
	u32 *addr, *save_addr;
	unsigned long i;
	struct page *page;

	for (pt_page = 0; pt_page < num_pt_pages; ++pt_page) {
		page = vmw_piter_page(pt_iter);

		save_addr = addr = kmap_atomic(page);

		for (i = 0; i < PAGE_SIZE / VMW_PPN_SIZE; ++i) {
			vmw_mob_assign_ppn(&addr,
					   vmw_piter_dma_addr(data_iter));
			if (unlikely(--num_data_pages == 0))
				break;
			WARN_ON(!vmw_piter_next(data_iter));
		}
		kunmap_atomic(save_addr);
		vmw_piter_next(pt_iter);
	}

	return num_pt_pages;
}

/*
 * vmw_mob_build_pt - Set up a multilevel mob pagetable
 *
 * @mob:            Pointer to a mob whose page table needs setting up.
 * @data_addr       Array of DMA addresses to the buffer object's data
 *                  pages.
 * @num_data_pages: Number of buffer object data pages.
 *
 * Uses tail recursion to set up a multilevel mob page table.
 */
static void vmw_mob_pt_setup(struct vmw_mob *mob,
			     struct vmw_piter data_iter,
			     unsigned long num_data_pages)
{
	unsigned long num_pt_pages = 0;
	struct ttm_buffer_object *bo = mob->pt_bo;
	struct vmw_piter save_pt_iter;
	struct vmw_piter pt_iter;
	const struct vmw_sg_table *vsgt;
	int ret;

	ret = ttm_bo_reserve(bo, false, true, NULL);
	BUG_ON(ret != 0);

	vsgt = vmw_bo_sg_table(bo);
	vmw_piter_start(&pt_iter, vsgt, 0);
	BUG_ON(!vmw_piter_next(&pt_iter));
	mob->pt_level = 0;
	while (likely(num_data_pages > 1)) {
		++mob->pt_level;
		BUG_ON(mob->pt_level > 2);
		save_pt_iter = pt_iter;
		num_pt_pages = vmw_mob_build_pt(&data_iter, num_data_pages,
						&pt_iter);
		data_iter = save_pt_iter;
		num_data_pages = num_pt_pages;
	}

	mob->pt_root_page = vmw_piter_dma_addr(&save_pt_iter);
	ttm_bo_unreserve(bo);
}

/*
 * vmw_mob_destroy - Destroy a mob, unpopulating first if necessary.
 *
 * @mob:            Pointer to a mob to destroy.
 */
void vmw_mob_destroy(struct vmw_mob *mob)
{
	if (mob->pt_bo) {
		ttm_bo_put(mob->pt_bo);
		mob->pt_bo = NULL;
	}
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
		ret = ttm_bo_reserve(bo, false, true, NULL);
		/*
		 * Noone else should be using this buffer.
		 */
		BUG_ON(ret != 0);
	}

	cmd = VMW_FIFO_RESERVE(dev_priv, sizeof(*cmd));
	if (cmd) {
		cmd->header.id = SVGA_3D_CMD_DESTROY_GB_MOB;
		cmd->header.size = sizeof(cmd->body);
		cmd->body.mobid = mob->id;
		vmw_fifo_commit(dev_priv, sizeof(*cmd));
	}

	if (bo) {
		vmw_bo_fence_single(bo, NULL);
		ttm_bo_unreserve(bo);
	}
	vmw_fifo_resource_dec(dev_priv);
}

/*
 * vmw_mob_bind - Make a mob visible to the device after first
 *                populating it if necessary.
 *
 * @dev_priv:       Pointer to a device private.
 * @mob:            Pointer to the mob we're making visible.
 * @data_addr:      Array of DMA addresses to the data pages of the underlying
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
		 const struct vmw_sg_table *vsgt,
		 unsigned long num_data_pages,
		 int32_t mob_id)
{
	int ret;
	bool pt_set_up = false;
	struct vmw_piter data_iter;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBMob64 body;
	} *cmd;

	mob->id = mob_id;
	vmw_piter_start(&data_iter, vsgt, 0);
	if (unlikely(!vmw_piter_next(&data_iter)))
		return 0;

	if (likely(num_data_pages == 1)) {
		mob->pt_level = VMW_MOBFMT_PTDEPTH_0;
		mob->pt_root_page = vmw_piter_dma_addr(&data_iter);
	} else if (vsgt->num_regions == 1) {
		mob->pt_level = SVGA3D_MOBFMT_RANGE;
		mob->pt_root_page = vmw_piter_dma_addr(&data_iter);
	} else if (unlikely(mob->pt_bo == NULL)) {
		ret = vmw_mob_pt_populate(dev_priv, mob);
		if (unlikely(ret != 0))
			return ret;

		vmw_mob_pt_setup(mob, data_iter, num_data_pages);
		pt_set_up = true;
		mob->pt_level += VMW_MOBFMT_PTDEPTH_1 - SVGA3D_MOBFMT_PTDEPTH_1;
	}

	vmw_fifo_resource_inc(dev_priv);

	cmd = VMW_FIFO_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		goto out_no_cmd_space;

	cmd->header.id = SVGA_3D_CMD_DEFINE_GB_MOB64;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.mobid = mob_id;
	cmd->body.ptDepth = mob->pt_level;
	cmd->body.base = mob->pt_root_page >> PAGE_SHIFT;
	cmd->body.sizeInBytes = num_data_pages * PAGE_SIZE;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;

out_no_cmd_space:
	vmw_fifo_resource_dec(dev_priv);
	if (pt_set_up) {
		ttm_bo_put(mob->pt_bo);
		mob->pt_bo = NULL;
	}

	return -ENOMEM;
}
