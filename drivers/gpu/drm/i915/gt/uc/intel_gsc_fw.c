// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_ring.h"
#include "intel_gsc_fw.h"

#define GSC_FW_STATUS_REG			_MMIO(0x116C40)
#define GSC_FW_CURRENT_STATE			REG_GENMASK(3, 0)
#define   GSC_FW_CURRENT_STATE_RESET		0
#define GSC_FW_INIT_COMPLETE_BIT		REG_BIT(9)

static bool gsc_is_in_reset(struct intel_uncore *uncore)
{
	u32 fw_status = intel_uncore_read(uncore, GSC_FW_STATUS_REG);

	return REG_FIELD_GET(GSC_FW_CURRENT_STATE, fw_status) ==
	       GSC_FW_CURRENT_STATE_RESET;
}

bool intel_gsc_uc_fw_init_done(struct intel_gsc_uc *gsc)
{
	struct intel_uncore *uncore = gsc_uc_to_gt(gsc)->uncore;
	u32 fw_status = intel_uncore_read(uncore, GSC_FW_STATUS_REG);

	return fw_status & GSC_FW_INIT_COMPLETE_BIT;
}

static int emit_gsc_fw_load(struct i915_request *rq, struct intel_gsc_uc *gsc)
{
	u32 offset = i915_ggtt_offset(gsc->local);
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GSC_FW_LOAD;
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);
	*cs++ = (gsc->local->size / SZ_4K) | HECI1_FW_LIMIT_VALID;

	intel_ring_advance(rq, cs);

	return 0;
}

static int gsc_fw_load(struct intel_gsc_uc *gsc)
{
	struct intel_context *ce = gsc->ce;
	struct i915_request *rq;
	int err;

	if (!ce)
		return -ENODEV;

	rq = i915_request_create(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (err)
			goto out_rq;
	}

	err = emit_gsc_fw_load(rq, gsc);
	if (err)
		goto out_rq;

	err = ce->engine->emit_flush(rq, 0);

out_rq:
	i915_request_get(rq);

	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);

	if (!err && i915_request_wait(rq, 0, msecs_to_jiffies(500)) < 0)
		err = -ETIME;

	i915_request_put(rq);

	if (err)
		drm_err(&gsc_uc_to_gt(gsc)->i915->drm,
			"Request submission for GSC load failed (%d)\n",
			err);

	return err;
}

static int gsc_fw_load_prepare(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	void *src, *dst;

	if (!gsc->local)
		return -ENODEV;

	obj = gsc->local->obj;

	if (obj->base.size < gsc->fw.size)
		return -ENOSPC;

	dst = i915_gem_object_pin_map_unlocked(obj,
					       i915_coherent_map_type(i915, obj, true));
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	src = i915_gem_object_pin_map_unlocked(gsc->fw.obj,
					       i915_coherent_map_type(i915, gsc->fw.obj, true));
	if (IS_ERR(src)) {
		i915_gem_object_unpin_map(obj);
		return PTR_ERR(src);
	}

	memset(dst, 0, obj->base.size);
	memcpy(dst, src, gsc->fw.size);

	i915_gem_object_unpin_map(gsc->fw.obj);
	i915_gem_object_unpin_map(obj);

	return 0;
}

static int gsc_fw_wait(struct intel_gt *gt)
{
	return intel_wait_for_register(gt->uncore,
				       GSC_FW_STATUS_REG,
				       GSC_FW_INIT_COMPLETE_BIT,
				       GSC_FW_INIT_COMPLETE_BIT,
				       500);
}

int intel_gsc_uc_fw_upload(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct intel_uc_fw *gsc_fw = &gsc->fw;
	int err;

	/* check current fw status */
	if (intel_gsc_uc_fw_init_done(gsc)) {
		if (GEM_WARN_ON(!intel_uc_fw_is_loaded(gsc_fw)))
			intel_uc_fw_change_status(gsc_fw, INTEL_UC_FIRMWARE_TRANSFERRED);
		return -EEXIST;
	}

	if (!intel_uc_fw_is_loadable(gsc_fw))
		return -ENOEXEC;

	/* FW blob is ok, so clean the status */
	intel_uc_fw_sanitize(&gsc->fw);

	if (!gsc_is_in_reset(gt->uncore))
		return -EIO;

	err = gsc_fw_load_prepare(gsc);
	if (err)
		goto fail;

	err = gsc_fw_load(gsc);
	if (err)
		goto fail;

	err = gsc_fw_wait(gt);
	if (err)
		goto fail;

	/* FW is not fully operational until we enable SW proxy */
	intel_uc_fw_change_status(gsc_fw, INTEL_UC_FIRMWARE_TRANSFERRED);

	drm_info(&gt->i915->drm, "Loaded GSC firmware %s\n",
		 gsc_fw->file_selected.path);

	return 0;

fail:
	return intel_uc_fw_mark_load_failed(gsc_fw, err);
}
