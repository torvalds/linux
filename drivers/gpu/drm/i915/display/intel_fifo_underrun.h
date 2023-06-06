/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FIFO_UNDERRUN_H__
#define __INTEL_FIFO_UNDERRUN_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_crtc;
enum pipe;

void intel_init_fifo_underrun_reporting(struct drm_i915_private *i915,
					struct intel_crtc *crtc, bool enable);
bool intel_set_cpu_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum pipe pipe, bool enable);
bool intel_set_pch_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum pipe pch_transcoder,
					   bool enable);
void intel_cpu_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe);
void intel_pch_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pch_transcoder);
void intel_check_cpu_fifo_underruns(struct drm_i915_private *dev_priv);
void intel_check_pch_fifo_underruns(struct drm_i915_private *dev_priv);

#endif /* __INTEL_FIFO_UNDERRUN_H__ */
