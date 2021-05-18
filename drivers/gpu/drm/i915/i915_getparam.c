/*
 * SPDX-License-Identifier: MIT
 */

#include "gem/i915_gem_mman.h"
#include "gt/intel_engine_user.h"

#include "i915_drv.h"
#include "i915_perf.h"

int i915_getparam_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	const struct sseu_dev_info *sseu = &i915->gt.info.sseu;
	drm_i915_getparam_t *param = data;
	int value;

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
	case I915_PARAM_ALLOW_BATCHBUFFER:
	case I915_PARAM_LAST_DISPATCH:
	case I915_PARAM_HAS_EXEC_CONSTANTS:
		/* Reject all old ums/dri params. */
		return -ENODEV;
	case I915_PARAM_CHIPSET_ID:
		value = pdev->device;
		break;
	case I915_PARAM_REVISION:
		value = pdev->revision;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = i915->ggtt.num_fences;
		break;
	case I915_PARAM_HAS_OVERLAY:
		value = !!i915->overlay;
		break;
	case I915_PARAM_HAS_BSD:
		value = !!intel_engine_lookup_user(i915,
						   I915_ENGINE_CLASS_VIDEO, 0);
		break;
	case I915_PARAM_HAS_BLT:
		value = !!intel_engine_lookup_user(i915,
						   I915_ENGINE_CLASS_COPY, 0);
		break;
	case I915_PARAM_HAS_VEBOX:
		value = !!intel_engine_lookup_user(i915,
						   I915_ENGINE_CLASS_VIDEO_ENHANCE, 0);
		break;
	case I915_PARAM_HAS_BSD2:
		value = !!intel_engine_lookup_user(i915,
						   I915_ENGINE_CLASS_VIDEO, 1);
		break;
	case I915_PARAM_HAS_LLC:
		value = HAS_LLC(i915);
		break;
	case I915_PARAM_HAS_WT:
		value = HAS_WT(i915);
		break;
	case I915_PARAM_HAS_ALIASING_PPGTT:
		value = INTEL_PPGTT(i915);
		break;
	case I915_PARAM_HAS_SEMAPHORES:
		value = !!(i915->caps.scheduler & I915_SCHEDULER_CAP_SEMAPHORES);
		break;
	case I915_PARAM_HAS_SECURE_BATCHES:
		value = HAS_SECURE_BATCHES(i915) && capable(CAP_SYS_ADMIN);
		break;
	case I915_PARAM_CMD_PARSER_VERSION:
		value = i915_cmd_parser_get_version(i915);
		break;
	case I915_PARAM_SUBSLICE_TOTAL:
		value = intel_sseu_subslice_total(sseu);
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_EU_TOTAL:
		value = sseu->eu_total;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_HAS_GPU_RESET:
		value = i915->params.enable_hangcheck &&
			intel_has_gpu_reset(&i915->gt);
		if (value && intel_has_reset_engine(&i915->gt))
			value = 2;
		break;
	case I915_PARAM_HAS_RESOURCE_STREAMER:
		value = 0;
		break;
	case I915_PARAM_HAS_POOLED_EU:
		value = HAS_POOLED_EU(i915);
		break;
	case I915_PARAM_MIN_EU_IN_POOL:
		value = sseu->min_eu_in_pool;
		break;
	case I915_PARAM_HUC_STATUS:
		value = intel_huc_check_status(&i915->gt.uc.huc);
		if (value < 0)
			return value;
		break;
	case I915_PARAM_MMAP_GTT_VERSION:
		/* Though we've started our numbering from 1, and so class all
		 * earlier versions as 0, in effect their value is undefined as
		 * the ioctl will report EINVAL for the unknown param!
		 */
		value = i915_gem_mmap_gtt_version();
		break;
	case I915_PARAM_HAS_SCHEDULER:
		value = i915->caps.scheduler;
		break;

	case I915_PARAM_MMAP_VERSION:
		/* Remember to bump this if the version changes! */
	case I915_PARAM_HAS_GEM:
	case I915_PARAM_HAS_PAGEFLIPPING:
	case I915_PARAM_HAS_EXECBUF2: /* depends on GEM */
	case I915_PARAM_HAS_RELAXED_FENCING:
	case I915_PARAM_HAS_COHERENT_RINGS:
	case I915_PARAM_HAS_RELAXED_DELTA:
	case I915_PARAM_HAS_GEN7_SOL_RESET:
	case I915_PARAM_HAS_WAIT_TIMEOUT:
	case I915_PARAM_HAS_PRIME_VMAP_FLUSH:
	case I915_PARAM_HAS_PINNED_BATCHES:
	case I915_PARAM_HAS_EXEC_NO_RELOC:
	case I915_PARAM_HAS_EXEC_HANDLE_LUT:
	case I915_PARAM_HAS_COHERENT_PHYS_GTT:
	case I915_PARAM_HAS_EXEC_SOFTPIN:
	case I915_PARAM_HAS_EXEC_ASYNC:
	case I915_PARAM_HAS_EXEC_FENCE:
	case I915_PARAM_HAS_EXEC_CAPTURE:
	case I915_PARAM_HAS_EXEC_BATCH_FIRST:
	case I915_PARAM_HAS_EXEC_FENCE_ARRAY:
	case I915_PARAM_HAS_EXEC_SUBMIT_FENCE:
	case I915_PARAM_HAS_EXEC_TIMELINE_FENCES:
		/* For the time being all of these are always true;
		 * if some supported hardware does not have one of these
		 * features this value needs to be provided from
		 * INTEL_INFO(), a feature macro, or similar.
		 */
		value = 1;
		break;
	case I915_PARAM_HAS_CONTEXT_ISOLATION:
		value = intel_engines_has_context_isolation(i915);
		break;
	case I915_PARAM_SLICE_MASK:
		value = sseu->slice_mask;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_SUBSLICE_MASK:
		value = sseu->subslice_mask[0];
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_CS_TIMESTAMP_FREQUENCY:
		value = i915->gt.clock_frequency;
		break;
	case I915_PARAM_MMAP_GTT_COHERENT:
		value = INTEL_INFO(i915)->has_coherent_ggtt;
		break;
	case I915_PARAM_PERF_REVISION:
		value = i915_perf_ioctl_version();
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	if (put_user(value, param->value))
		return -EFAULT;

	return 0;
}
