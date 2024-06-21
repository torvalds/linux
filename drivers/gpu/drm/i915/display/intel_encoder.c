// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/workqueue.h>

#include "i915_drv.h"

#include "intel_display_types.h"
#include "intel_encoder.h"

static void intel_encoder_link_check_work_fn(struct work_struct *work)
{
	struct intel_encoder *encoder =
		container_of(work, typeof(*encoder), link_check_work.work);

	encoder->link_check(encoder);
}

void intel_encoder_link_check_init(struct intel_encoder *encoder,
				   void (*callback)(struct intel_encoder *encoder))
{
	INIT_DELAYED_WORK(&encoder->link_check_work, intel_encoder_link_check_work_fn);
	encoder->link_check = callback;
}

void intel_encoder_link_check_flush_work(struct intel_encoder *encoder)
{
	cancel_delayed_work_sync(&encoder->link_check_work);
}

void intel_encoder_link_check_queue_work(struct intel_encoder *encoder, int delay_ms)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	mod_delayed_work(i915->unordered_wq,
			 &encoder->link_check_work, msecs_to_jiffies(delay_ms));
}
