// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_reg_sr.h"

#include <kunit/visibility.h>
#include <linux/align.h>
#include <linux/string_helpers.h>
#include <linux/xarray.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_printk.h"
#include "xe_hw_engine_types.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_reg_whitelist.h"
#include "xe_rtp_types.h"

#define XE_REG_SR_GROW_STEP_DEFAULT	16

static void reg_sr_fini(struct drm_device *drm, void *arg)
{
	struct xe_reg_sr *sr = arg;

	xa_destroy(&sr->xa);
	kfree(sr->pool.arr);
	memset(&sr->pool, 0, sizeof(sr->pool));
}

int xe_reg_sr_init(struct xe_reg_sr *sr, const char *name, struct xe_device *xe)
{
	xa_init(&sr->xa);
	memset(&sr->pool, 0, sizeof(sr->pool));
	sr->pool.grow_step = XE_REG_SR_GROW_STEP_DEFAULT;
	sr->name = name;

	return drmm_add_action_or_reset(&xe->drm, reg_sr_fini, sr);
}
EXPORT_SYMBOL_IF_KUNIT(xe_reg_sr_init);

static struct xe_reg_sr_entry *alloc_entry(struct xe_reg_sr *sr)
{
	if (sr->pool.used == sr->pool.allocated) {
		struct xe_reg_sr_entry *arr;

		arr = krealloc_array(sr->pool.arr,
				     ALIGN(sr->pool.allocated + 1, sr->pool.grow_step),
				     sizeof(*arr), GFP_KERNEL);
		if (!arr)
			return NULL;

		sr->pool.arr = arr;
		sr->pool.allocated += sr->pool.grow_step;
	}

	return &sr->pool.arr[sr->pool.used++];
}

static bool compatible_entries(const struct xe_reg_sr_entry *e1,
			       const struct xe_reg_sr_entry *e2)
{
	/*
	 * Don't allow overwriting values: clr_bits/set_bits should be disjoint
	 * when operating in the same register
	 */
	if (e1->clr_bits & e2->clr_bits || e1->set_bits & e2->set_bits ||
	    e1->clr_bits & e2->set_bits || e1->set_bits & e2->clr_bits)
		return false;

	if (e1->reg.raw != e2->reg.raw)
		return false;

	return true;
}

static void reg_sr_inc_error(struct xe_reg_sr *sr)
{
#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
	sr->errors++;
#endif
}

int xe_reg_sr_add(struct xe_reg_sr *sr,
		  const struct xe_reg_sr_entry *e,
		  struct xe_gt *gt)
{
	unsigned long idx = e->reg.addr;
	struct xe_reg_sr_entry *pentry = xa_load(&sr->xa, idx);
	int ret;

	if (pentry) {
		if (!compatible_entries(pentry, e)) {
			ret = -EINVAL;
			goto fail;
		}

		pentry->clr_bits |= e->clr_bits;
		pentry->set_bits |= e->set_bits;
		pentry->read_mask |= e->read_mask;

		return 0;
	}

	pentry = alloc_entry(sr);
	if (!pentry) {
		ret = -ENOMEM;
		goto fail;
	}

	*pentry = *e;
	ret = xa_err(xa_store(&sr->xa, idx, pentry, GFP_KERNEL));
	if (ret)
		goto fail;

	return 0;

fail:
	xe_gt_err(gt,
		  "discarding save-restore reg %04lx (clear: %08x, set: %08x, masked: %s, mcr: %s): ret=%d\n",
		  idx, e->clr_bits, e->set_bits,
		  str_yes_no(e->reg.masked),
		  str_yes_no(e->reg.mcr),
		  ret);
	reg_sr_inc_error(sr);

	return ret;
}

/*
 * Convert back from encoded value to type-safe, only to be used when reg.mcr
 * is true
 */
static struct xe_reg_mcr to_xe_reg_mcr(const struct xe_reg reg)
{
	return (const struct xe_reg_mcr){.__reg.raw = reg.raw };
}

static void apply_one_mmio(struct xe_gt *gt, struct xe_reg_sr_entry *entry)
{
	struct xe_reg reg = entry->reg;
	struct xe_reg_mcr reg_mcr = to_xe_reg_mcr(reg);
	u32 val;

	/*
	 * If this is a masked register, need to set the upper 16 bits.
	 * Set them to clr_bits since that is always a superset of the bits
	 * being modified.
	 *
	 * When it's not masked, we have to read it from hardware, unless we are
	 * supposed to set all bits.
	 */
	if (reg.masked)
		val = entry->clr_bits << 16;
	else if (entry->clr_bits + 1)
		val = (reg.mcr ?
		       xe_gt_mcr_unicast_read_any(gt, reg_mcr) :
		       xe_mmio_read32(gt, reg)) & (~entry->clr_bits);
	else
		val = 0;

	/*
	 * TODO: add selftest to validate all tables, regardless of platform:
	 *   - Masked registers can't have set_bits with upper bits set
	 *   - set_bits must be contained in clr_bits
	 */
	val |= entry->set_bits;

	xe_gt_dbg(gt, "REG[0x%x] = 0x%08x", reg.addr, val);

	if (entry->reg.mcr)
		xe_gt_mcr_multicast_write(gt, reg_mcr, val);
	else
		xe_mmio_write32(gt, reg, val);
}

void xe_reg_sr_apply_mmio(struct xe_reg_sr *sr, struct xe_gt *gt)
{
	struct xe_reg_sr_entry *entry;
	unsigned long reg;
	int err;

	if (xa_empty(&sr->xa))
		return;

	xe_gt_dbg(gt, "Applying %s save-restore MMIOs\n", sr->name);

	err = xe_force_wake_get(&gt->mmio.fw, XE_FORCEWAKE_ALL);
	if (err)
		goto err_force_wake;

	xa_for_each(&sr->xa, reg, entry)
		apply_one_mmio(gt, entry);

	err = xe_force_wake_put(&gt->mmio.fw, XE_FORCEWAKE_ALL);
	XE_WARN_ON(err);

	return;

err_force_wake:
	xe_gt_err(gt, "Failed to apply, err=%d\n", err);
}

void xe_reg_sr_apply_whitelist(struct xe_hw_engine *hwe)
{
	struct xe_reg_sr *sr = &hwe->reg_whitelist;
	struct xe_gt *gt = hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_reg_sr_entry *entry;
	struct drm_printer p;
	u32 mmio_base = hwe->mmio_base;
	unsigned long reg;
	unsigned int slot = 0;
	int err;

	if (xa_empty(&sr->xa))
		return;

	drm_dbg(&xe->drm, "Whitelisting %s registers\n", sr->name);

	err = xe_force_wake_get(&gt->mmio.fw, XE_FORCEWAKE_ALL);
	if (err)
		goto err_force_wake;

	p = drm_dbg_printer(&xe->drm, DRM_UT_DRIVER, NULL);
	xa_for_each(&sr->xa, reg, entry) {
		if (slot == RING_MAX_NONPRIV_SLOTS) {
			xe_gt_err(gt,
				  "hwe %s: maximum register whitelist slots (%d) reached, refusing to add more\n",
				  hwe->name, RING_MAX_NONPRIV_SLOTS);
			break;
		}

		xe_reg_whitelist_print_entry(&p, 0, reg, entry);
		xe_mmio_write32(gt, RING_FORCE_TO_NONPRIV(mmio_base, slot),
				reg | entry->set_bits);
		slot++;
	}

	/* And clear the rest just in case of garbage */
	for (; slot < RING_MAX_NONPRIV_SLOTS; slot++) {
		u32 addr = RING_NOPID(mmio_base).addr;

		xe_mmio_write32(gt, RING_FORCE_TO_NONPRIV(mmio_base, slot), addr);
	}

	err = xe_force_wake_put(&gt->mmio.fw, XE_FORCEWAKE_ALL);
	XE_WARN_ON(err);

	return;

err_force_wake:
	drm_err(&xe->drm, "Failed to apply, err=%d\n", err);
}

/**
 * xe_reg_sr_dump - print all save/restore entries
 * @sr: Save/restore entries
 * @p: DRM printer
 */
void xe_reg_sr_dump(struct xe_reg_sr *sr, struct drm_printer *p)
{
	struct xe_reg_sr_entry *entry;
	unsigned long reg;

	if (!sr->name || xa_empty(&sr->xa))
		return;

	drm_printf(p, "%s\n", sr->name);
	xa_for_each(&sr->xa, reg, entry)
		drm_printf(p, "\tREG[0x%lx] clr=0x%08x set=0x%08x masked=%s mcr=%s\n",
			   reg, entry->clr_bits, entry->set_bits,
			   str_yes_no(entry->reg.masked),
			   str_yes_no(entry->reg.mcr));
}
