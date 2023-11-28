// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/align.h>

#include <drm/drm_managed.h>

#include "regs/xe_sriov_regs.h"

#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_lmtt.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_res_cursor.h"
#include "xe_sriov.h"
#include "xe_sriov_printk.h"

/**
 * DOC: Local Memory Translation Table
 *
 * The Local Memory Translation Table (LMTT) provides additional abstraction
 * when Virtual Function (VF) is accessing device Local Memory (VRAM).
 *
 * The Root LMTT Page Directory contains one entry for each VF. Entries are
 * indexed by the function number (1-based, index 0 is unused).
 *
 * See `Two-Level LMTT Structure`_ and `Multi-Level LMTT Structure`_.
 */

#define lmtt_assert(lmtt, condition)	xe_tile_assert(lmtt_to_tile(lmtt), condition)
#define lmtt_debug(lmtt, msg...)	xe_sriov_dbg_verbose(lmtt_to_xe(lmtt), "LMTT: " msg)

static bool xe_has_multi_level_lmtt(struct xe_device *xe)
{
	return xe->info.platform == XE_PVC;
}

static struct xe_tile *lmtt_to_tile(struct xe_lmtt *lmtt)
{
	return container_of(lmtt, struct xe_tile, sriov.pf.lmtt);
}

static struct xe_device *lmtt_to_xe(struct xe_lmtt *lmtt)
{
	return tile_to_xe(lmtt_to_tile(lmtt));
}

static u64 lmtt_page_size(struct xe_lmtt *lmtt)
{
	return BIT_ULL(lmtt->ops->lmtt_pte_shift(0));
}

static struct xe_lmtt_pt *lmtt_pt_alloc(struct xe_lmtt *lmtt, unsigned int level)
{
	unsigned int num_entries = level ? lmtt->ops->lmtt_pte_num(level) : 0;
	struct xe_lmtt_pt *pt;
	struct xe_bo *bo;
	int err;

	pt = kzalloc(struct_size(pt, entries, num_entries), GFP_KERNEL);
	if (!pt) {
		err = -ENOMEM;
		goto out;
	}

	bo = xe_bo_create_pin_map(lmtt_to_xe(lmtt), lmtt_to_tile(lmtt), NULL,
				  PAGE_ALIGN(lmtt->ops->lmtt_pte_size(level) *
					     lmtt->ops->lmtt_pte_num(level)),
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(lmtt_to_tile(lmtt)) |
				  XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto out_free_pt;
	}

	lmtt_assert(lmtt, xe_bo_is_vram(bo));

	pt->level = level;
	pt->bo = bo;
	return pt;

out_free_pt:
	kfree(pt);
out:
	return ERR_PTR(err);
}

static void lmtt_pt_free(struct xe_lmtt_pt *pt)
{
	xe_bo_unpin_map_no_vm(pt->bo);
	kfree(pt);
}

static int lmtt_init_pd(struct xe_lmtt *lmtt)
{
	struct xe_lmtt_pt *pd;

	lmtt_assert(lmtt, !lmtt->pd);
	lmtt_assert(lmtt, lmtt->ops->lmtt_root_pd_level());

	pd = lmtt_pt_alloc(lmtt, lmtt->ops->lmtt_root_pd_level());
	if (IS_ERR(pd))
		return PTR_ERR(pd);

	lmtt->pd = pd;
	return 0;
}

static void lmtt_fini_pd(struct xe_lmtt *lmtt)
{
	struct xe_lmtt_pt *pd = lmtt->pd;
	unsigned int num_entries = lmtt->ops->lmtt_pte_num(pd->level);
	unsigned int n = 0;

	/* make sure we don't leak */
	for (n = 0; n < num_entries; n++)
		lmtt_assert(lmtt, !pd->entries[n]);

	lmtt->pd = NULL;
	lmtt_pt_free(pd);
}

static void fini_lmtt(struct drm_device *drm, void *arg)
{
	struct xe_lmtt *lmtt = arg;

	lmtt_assert(lmtt, !(!!lmtt->ops ^ !!lmtt->pd));

	if (!lmtt->pd)
		return;

	lmtt_fini_pd(lmtt);
	lmtt->ops = NULL;
}

/**
 * xe_lmtt_init - LMTT software initialization.
 * @lmtt: the &xe_lmtt to initialize
 *
 * The LMTT initialization requires two steps.
 *
 * The xe_lmtt_init() checks if LMTT is required on current device and selects
 * and initialize proper variant of the LMTT Root Directory. Currently supported
 * variants are `Two-Level LMTT Structure`_ and `Multi-Level LMTT Structure`_.
 *
 * In next step xe_lmtt_init_hw() will register this directory on the hardware.
 *
 * Notes:
 * The LMTT allocations are managed and will be implicitly released on driver unload.
 * This function shall be called only once and only when running as a PF driver.
 * Any LMTT initialization failure should block VFs enabling.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_lmtt_init(struct xe_lmtt *lmtt)
{
	struct xe_device *xe = lmtt_to_xe(lmtt);
	int err;

	lmtt_assert(lmtt, IS_SRIOV_PF(xe));
	lmtt_assert(lmtt, !lmtt->ops);

	if (!IS_DGFX(xe))
		return 0;

	if (xe_has_multi_level_lmtt(xe))
		lmtt->ops = &lmtt_ml_ops;
	else
		lmtt->ops = &lmtt_2l_ops;

	err = lmtt_init_pd(lmtt);
	if (unlikely(err))
		goto fail;

	return drmm_add_action_or_reset(&xe->drm, fini_lmtt, lmtt);

fail:
	lmtt->ops = NULL;
	return err;
}

static void lmtt_setup_dir_ptr(struct xe_lmtt *lmtt)
{
	struct xe_tile *tile = lmtt_to_tile(lmtt);
	struct xe_device *xe = tile_to_xe(tile);
	dma_addr_t offset = xe_bo_main_addr(lmtt->pd->bo, XE_PAGE_SIZE);

	lmtt_debug(lmtt, "DIR offset %pad\n", &offset);
	lmtt_assert(lmtt, xe_bo_is_vram(lmtt->pd->bo));
	lmtt_assert(lmtt, IS_ALIGNED(offset, SZ_64K));

	xe_mmio_write32(tile->primary_gt,
			GRAPHICS_VER(xe) >= 20 ? XE2_LMEM_CFG : LMEM_CFG,
			LMEM_EN | REG_FIELD_PREP(LMTT_DIR_PTR, offset / SZ_64K));
}

/**
 * xe_lmtt_init_hw - Perform LMTT hardware initialization.
 * @lmtt: the &xe_lmtt to initialize
 *
 * This function is a second step of the LMTT initialization.
 * This function registers LMTT Root Directory prepared in xe_lmtt_init().
 *
 * This function shall be called after every hardware reset.
 * This function shall be called only when running as a PF driver.
 */
void xe_lmtt_init_hw(struct xe_lmtt *lmtt)
{
	if (!lmtt->pd)
		return;

	lmtt_setup_dir_ptr(lmtt);
}

static void lmtt_write_pte(struct xe_lmtt *lmtt, struct xe_lmtt_pt *pt,
			   u64 pte, unsigned int idx)
{
	unsigned int level = pt->level;

	lmtt_assert(lmtt, idx <= lmtt->ops->lmtt_pte_num(level));
	lmtt_debug(lmtt, "WRITE level=%u index=%u pte=%#llx\n", level, idx, pte);

	switch (lmtt->ops->lmtt_pte_size(level)) {
	case sizeof(u32):
		xe_map_wr(lmtt_to_xe(lmtt), &pt->bo->vmap, idx * sizeof(u32), u32, pte);
		break;
	case sizeof(u64):
		xe_map_wr(lmtt_to_xe(lmtt), &pt->bo->vmap, idx * sizeof(u64), u64, pte);
		break;
	default:
		lmtt_assert(lmtt, !!!"invalid pte size");
	}
}

static void lmtt_destroy_pt(struct xe_lmtt *lmtt, struct xe_lmtt_pt *pd)
{
	unsigned int num_entries = pd->level ? lmtt->ops->lmtt_pte_num(pd->level) : 0;
	struct xe_lmtt_pt *pt;
	unsigned int i;

	for (i = 0; i < num_entries; i++) {
		pt = pd->entries[i];
		pd->entries[i] = NULL;
		if (!pt)
			continue;

		lmtt_destroy_pt(lmtt, pt);
	}

	lmtt_pt_free(pd);
}

static void lmtt_drop_pages(struct xe_lmtt *lmtt, unsigned int vfid)
{
	struct xe_lmtt_pt *pd = lmtt->pd;
	struct xe_lmtt_pt *pt;

	pt = pd->entries[vfid];
	pd->entries[vfid] = NULL;
	if (!pt)
		return;

	lmtt_write_pte(lmtt, pd, LMTT_PTE_INVALID, vfid);

	lmtt_assert(lmtt, pd->level > 0);
	lmtt_assert(lmtt, pt->level == pd->level - 1);
	lmtt_destroy_pt(lmtt, pt);
}

static int __lmtt_alloc_range(struct xe_lmtt *lmtt, struct xe_lmtt_pt *pd,
			      u64 start, u64 end)
{
	u64 pte_addr_shift = BIT_ULL(lmtt->ops->lmtt_pte_shift(pd->level));
	u64 offset;
	int err;

	lmtt_assert(lmtt, pd->level > 0);

	offset = start;
	while (offset < end) {
		struct xe_lmtt_pt *pt;
		u64 next, pde, pt_addr;
		unsigned int idx;

		pt = lmtt_pt_alloc(lmtt, pd->level - 1);
		if (IS_ERR(pt))
			return PTR_ERR(pt);

		pt_addr = xe_bo_main_addr(pt->bo, XE_PAGE_SIZE);

		idx = lmtt->ops->lmtt_pte_index(offset, pd->level);
		pde = lmtt->ops->lmtt_pte_encode(pt_addr, pd->level);

		lmtt_write_pte(lmtt, pd, pde, idx);

		pd->entries[idx] = pt;

		next = min(end, round_up(offset + 1, pte_addr_shift));

		if (pt->level != 0) {
			err = __lmtt_alloc_range(lmtt, pt, offset, next);
			if (err)
				return err;
		}

		offset = next;
	}

	return 0;
}

static int lmtt_alloc_range(struct xe_lmtt *lmtt, unsigned int vfid, u64 start, u64 end)
{
	struct xe_lmtt_pt *pd = lmtt->pd;
	struct xe_lmtt_pt *pt;
	u64 pt_addr;
	u64 pde;
	int err;

	lmtt_assert(lmtt, pd->level > 0);
	lmtt_assert(lmtt, vfid <= lmtt->ops->lmtt_pte_num(pd->level));
	lmtt_assert(lmtt, IS_ALIGNED(start, lmtt_page_size(lmtt)));
	lmtt_assert(lmtt, IS_ALIGNED(end, lmtt_page_size(lmtt)));

	if (pd->entries[vfid])
		return -ENOTEMPTY;

	pt = lmtt_pt_alloc(lmtt, pd->level - 1);
	if (IS_ERR(pt))
		return PTR_ERR(pt);

	pt_addr = xe_bo_main_addr(pt->bo, XE_PAGE_SIZE);

	pde = lmtt->ops->lmtt_pte_encode(pt_addr, pd->level);

	lmtt_write_pte(lmtt, pd, pde, vfid);

	pd->entries[vfid] = pt;

	if (pt->level != 0) {
		err = __lmtt_alloc_range(lmtt, pt, start, end);
		if (err)
			goto out_free_pt;
	}

	return 0;

out_free_pt:
	lmtt_pt_free(pt);
	return err;
}

static struct xe_lmtt_pt *lmtt_leaf_pt(struct xe_lmtt *lmtt, unsigned int vfid, u64 addr)
{
	struct xe_lmtt_pt *pd = lmtt->pd;
	struct xe_lmtt_pt *pt;

	lmtt_assert(lmtt, vfid <= lmtt->ops->lmtt_pte_num(pd->level));
	pt = pd->entries[vfid];

	while (pt->level) {
		lmtt_assert(lmtt, lmtt->ops->lmtt_pte_index(addr, pt->level) <=
			    lmtt->ops->lmtt_pte_num(pt->level));

		pt = pt->entries[lmtt->ops->lmtt_pte_index(addr, pt->level)];

		addr >>= lmtt->ops->lmtt_pte_shift(pt->level);
	}

	lmtt_assert(lmtt, lmtt->ops->lmtt_pte_index(addr, pt->level) <=
		    lmtt->ops->lmtt_pte_num(pt->level));
	lmtt_assert(lmtt, pt->level != pd->level);
	lmtt_assert(lmtt, pt->level == 0);
	return pt;
}

static void lmtt_insert_bo(struct xe_lmtt *lmtt, unsigned int vfid, struct xe_bo *bo, u64 start)
{
	u64 page_size = lmtt_page_size(lmtt);
	struct xe_res_cursor cur;
	struct xe_lmtt_pt *pt;
	u64 addr, vram_offset;

	lmtt_assert(lmtt, IS_ALIGNED(start, page_size));
	lmtt_assert(lmtt, IS_ALIGNED(bo->size, page_size));
	lmtt_assert(lmtt, xe_bo_is_vram(bo));

	vram_offset = vram_region_gpu_offset(bo->ttm.resource);
	xe_res_first(bo->ttm.resource, 0, bo->size, &cur);
	while (cur.remaining) {
		addr = xe_res_dma(&cur);
		addr += vram_offset; /* XXX */

		pt = lmtt_leaf_pt(lmtt, vfid, start);

		lmtt_write_pte(lmtt, pt, lmtt->ops->lmtt_pte_encode(addr, 0),
					 lmtt->ops->lmtt_pte_index(start, 0));

		xe_res_next(&cur, page_size);
		start += page_size;
	}
}

/**
 * xe_lmtt_prepare_pages - Create VF's LMTT Page Tables.
 * @lmtt: the &xe_lmtt to update
 * @vfid: the VF identifier (1-based)
 * @range: top range of LMEM offset to be supported
 *
 * This function creates empty LMTT page tables for given VF to support
 * up to maximum #range LMEM offset. The LMTT page tables created by this
 * function must be released using xe_lmtt_drop_pages() function.
 *
 * Notes:
 * This function shall be called only after successful LMTT initialization.
 * See xe_lmtt_init().
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_lmtt_prepare_pages(struct xe_lmtt *lmtt, unsigned int vfid, u64 range)
{
	lmtt_assert(lmtt, lmtt->pd);
	lmtt_assert(lmtt, vfid);

	return lmtt_alloc_range(lmtt, vfid, 0, range);
}

/**
 * xe_lmtt_populate_pages - Update VF's LMTT Page Table Entries.
 * @lmtt: the &xe_lmtt to update
 * @vfid: the VF identifier (1-based)
 * @bo: the buffer object with LMEM allocation to be mapped
 * @offset: the offset at which #bo should be mapped
 *
 * This function updates VF's LMTT entries to use given buffer object as a backstore.
 *
 * Notes:
 * This function shall be called only after successful preparation of the
 * VF's LMTT Page Tables. See xe_lmtt_prepare().
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_lmtt_populate_pages(struct xe_lmtt *lmtt, unsigned int vfid, struct xe_bo *bo, u64 offset)
{
	lmtt_assert(lmtt, lmtt->pd);
	lmtt_assert(lmtt, vfid);

	lmtt_insert_bo(lmtt, vfid, bo, offset);
	return 0;
}

/**
 * xe_lmtt_drop_pages - Remove VF's LMTT Pages.
 * @lmtt: the &xe_lmtt to update
 * @vfid: the VF identifier (1-based)
 *
 * This function removes all LMTT Page Tables prepared by xe_lmtt_prepare_pages().
 *
 * This function shall be called only after successful LMTT initialization.
 * See xe_lmtt_init().
 */
void xe_lmtt_drop_pages(struct xe_lmtt *lmtt, unsigned int vfid)
{
	lmtt_assert(lmtt, lmtt->pd);
	lmtt_assert(lmtt, vfid);

	lmtt_drop_pages(lmtt, vfid);
}

/**
 * xe_lmtt_estimate_pt_size - Estimate size of LMTT PT allocations.
 * @lmtt: the &xe_lmtt
 * @size: the size of the LMEM to be mapped over LMTT (including any offset)
 *
 * This function shall be called only by PF.
 *
 * Return: size of the PT allocation(s) needed to support given LMEM size.
 */
u64 xe_lmtt_estimate_pt_size(struct xe_lmtt *lmtt, u64 size)
{
	unsigned int level = 0;
	u64 pt_size;

	lmtt_assert(lmtt, IS_SRIOV_PF(lmtt_to_xe(lmtt)));
	lmtt_assert(lmtt, IS_DGFX(lmtt_to_xe(lmtt)));
	lmtt_assert(lmtt, lmtt->ops);

	pt_size = PAGE_ALIGN(lmtt->ops->lmtt_pte_size(level) *
			     lmtt->ops->lmtt_pte_num(level));

	while (++level < lmtt->ops->lmtt_root_pd_level()) {
		pt_size *= lmtt->ops->lmtt_pte_index(size, level) + 1;
		pt_size += PAGE_ALIGN(lmtt->ops->lmtt_pte_size(level) *
				      lmtt->ops->lmtt_pte_num(level));
	}

	return pt_size;
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_lmtt_test.c"
#endif
