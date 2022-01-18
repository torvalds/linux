// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include <linux/component.h>

#include <drm/i915_pxp_tee_interface.h>
#include <drm/i915_component.h>

#include "i915_drv.h"
#include "intel_pxp.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "intel_pxp_tee_interface.h"

static inline struct intel_pxp *i915_dev_to_pxp(struct device *i915_kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);

	return &to_gt(i915)->pxp;
}

static int intel_pxp_tee_io_message(struct intel_pxp *pxp,
				    void *msg_in, u32 msg_in_size,
				    void *msg_out, u32 msg_out_max_size,
				    u32 *msg_out_rcv_size)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct i915_pxp_component *pxp_component = pxp->pxp_component;
	int ret = 0;

	mutex_lock(&pxp->tee_mutex);

	/*
	 * The binding of the component is asynchronous from i915 probe, so we
	 * can't be sure it has happened.
	 */
	if (!pxp_component) {
		ret = -ENODEV;
		goto unlock;
	}

	ret = pxp_component->ops->send(pxp_component->tee_dev, msg_in, msg_in_size);
	if (ret) {
		drm_err(&i915->drm, "Failed to send PXP TEE message\n");
		goto unlock;
	}

	ret = pxp_component->ops->recv(pxp_component->tee_dev, msg_out, msg_out_max_size);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to receive PXP TEE message\n");
		goto unlock;
	}

	if (ret > msg_out_max_size) {
		drm_err(&i915->drm,
			"Failed to receive PXP TEE message due to unexpected output size\n");
		ret = -ENOSPC;
		goto unlock;
	}

	if (msg_out_rcv_size)
		*msg_out_rcv_size = ret;

	ret = 0;
unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

/**
 * i915_pxp_tee_component_bind - bind function to pass the function pointers to pxp_tee
 * @i915_kdev: pointer to i915 kernel device
 * @tee_kdev: pointer to tee kernel device
 * @data: pointer to pxp_tee_master containing the function pointers
 *
 * This bind function is called during the system boot or resume from system sleep.
 *
 * Return: return 0 if successful.
 */
static int i915_pxp_tee_component_bind(struct device *i915_kdev,
				       struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);
	intel_wakeref_t wakeref;

	mutex_lock(&pxp->tee_mutex);
	pxp->pxp_component = data;
	pxp->pxp_component->tee_dev = tee_kdev;
	mutex_unlock(&pxp->tee_mutex);

	/* if we are suspended, the HW will be re-initialized on resume */
	wakeref = intel_runtime_pm_get_if_in_use(&i915->runtime_pm);
	if (!wakeref)
		return 0;

	/* the component is required to fully start the PXP HW */
	intel_pxp_init_hw(pxp);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return 0;
}

static void i915_pxp_tee_component_unbind(struct device *i915_kdev,
					  struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);
	intel_wakeref_t wakeref;

	with_intel_runtime_pm_if_in_use(&i915->runtime_pm, wakeref)
		intel_pxp_fini_hw(pxp);

	mutex_lock(&pxp->tee_mutex);
	pxp->pxp_component = NULL;
	mutex_unlock(&pxp->tee_mutex);
}

static const struct component_ops i915_pxp_tee_component_ops = {
	.bind   = i915_pxp_tee_component_bind,
	.unbind = i915_pxp_tee_component_unbind,
};

int intel_pxp_tee_component_init(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct drm_i915_private *i915 = gt->i915;

	ret = component_add_typed(i915->drm.dev, &i915_pxp_tee_component_ops,
				  I915_COMPONENT_PXP);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to add PXP component (%d)\n", ret);
		return ret;
	}

	pxp->pxp_component_added = true;

	return 0;
}

void intel_pxp_tee_component_fini(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;

	if (!pxp->pxp_component_added)
		return;

	component_del(i915->drm.dev, &i915_pxp_tee_component_ops);
	pxp->pxp_component_added = false;
}

int intel_pxp_tee_cmd_create_arb_session(struct intel_pxp *pxp,
					 int arb_session_id)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct pxp_tee_create_arb_in msg_in = {0};
	struct pxp_tee_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_TEE_APIVER;
	msg_in.header.command_id = PXP_TEE_ARB_CMDID;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP_TEE_ARB_PROTECTION_MODE;
	msg_in.session_id = arb_session_id;

	ret = intel_pxp_tee_io_message(pxp,
				       &msg_in, sizeof(msg_in),
				       &msg_out, sizeof(msg_out),
				       NULL);

	if (ret)
		drm_err(&i915->drm, "Failed to send tee msg ret=[%d]\n", ret);

	return ret;
}
