/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TYPES_H__
#define __INTEL_PXP_TYPES_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct intel_context;
struct intel_gt;
struct i915_pxp_component;
struct drm_i915_private;

/**
 * struct intel_pxp - pxp state
 */
struct intel_pxp {
	/**
	 * @ctrl_gt: poiner to the tile that owns the controls for PXP subsystem assets that
	 * the VDBOX, the KCR engine (and GSC CS depending on the platform)
	 */
	struct intel_gt *ctrl_gt;

	/**
	 * @platform_cfg_is_bad: used to track if any prior arb session creation resulted
	 * in a failure that was caused by a platform configuration issue, meaning that
	 * failure will not get resolved without a change to the platform (not kernel)
	 * such as BIOS configuration, firwmware update, etc. This bool gets reflected when
	 * GET_PARAM:I915_PARAM_PXP_STATUS is called.
	 */
	bool platform_cfg_is_bad;

	/**
	 * @kcr_base: base mmio offset for the KCR engine which is different on legacy platforms
	 * vs newer platforms where the KCR is inside the media-tile.
	 */
	u32 kcr_base;

	/**
	 * @gsccs_res: resources for request submission for platforms that have a GSC engine.
	 */
	struct gsccs_session_resources {
		u64 host_session_handle; /* used by firmware to link commands to sessions */
		struct intel_context *ce; /* context for gsc command submission */

		struct i915_vma *pkt_vma; /* GSC FW cmd packet vma */
		void *pkt_vaddr;  /* GSC FW cmd packet virt pointer */

		struct i915_vma *bb_vma; /* HECI_PKT batch buffer vma */
		void *bb_vaddr; /* HECI_PKT batch buffer virt pointer */
	} gsccs_res;

	/**
	 * @pxp_component: i915_pxp_component struct of the bound mei_pxp
	 * module. Only set and cleared inside component bind/unbind functions,
	 * which are protected by &tee_mutex.
	 */
	struct i915_pxp_component *pxp_component;

	/**
	 * @dev_link: Enforce module relationship for power management ordering.
	 */
	struct device_link *dev_link;
	/**
	 * @pxp_component_added: track if the pxp component has been added.
	 * Set and cleared in tee init and fini functions respectively.
	 */
	bool pxp_component_added;

	/** @ce: kernel-owned context used for PXP operations */
	struct intel_context *ce;

	/** @arb_mutex: protects arb session start */
	struct mutex arb_mutex;
	/**
	 * @arb_is_valid: tracks arb session status.
	 * After a teardown, the arb session can still be in play on the HW
	 * even if the keys are gone, so we can't rely on the HW state of the
	 * session to know if it's valid and need to track the status in SW.
	 */
	bool arb_is_valid;

	/**
	 * @key_instance: tracks which key instance we're on, so we can use it
	 * to determine if an object was created using the current key or a
	 * previous one.
	 */
	u32 key_instance;

	/** @tee_mutex: protects the tee channel binding and messaging. */
	struct mutex tee_mutex;

	/** @stream_cmd: LMEM obj used to send stream PXP commands to the GSC */
	struct {
		struct drm_i915_gem_object *obj; /* contains PXP command memory */
		void *vaddr; /* virtual memory for PXP command */
	} stream_cmd;

	/**
	 * @hw_state_invalidated: if the HW perceives an attack on the integrity
	 * of the encryption it will invalidate the keys and expect SW to
	 * re-initialize the session. We keep track of this state to make sure
	 * we only re-start the arb session when required.
	 */
	bool hw_state_invalidated;

	/** @irq_enabled: tracks the status of the kcr irqs */
	bool irq_enabled;
	/**
	 * @termination: tracks the status of a pending termination. Only
	 * re-initialized under gt->irq_lock and completed in &session_work.
	 */
	struct completion termination;

	/** @session_work: worker that manages session events. */
	struct work_struct session_work;
	/** @session_events: pending session events, protected with gt->irq_lock. */
	u32 session_events;
#define PXP_TERMINATION_REQUEST  BIT(0)
#define PXP_TERMINATION_COMPLETE BIT(1)
#define PXP_INVAL_REQUIRED       BIT(2)
};

#endif /* __INTEL_PXP_TYPES_H__ */
