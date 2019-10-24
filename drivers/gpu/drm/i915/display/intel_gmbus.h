/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_GMBUS_H__
#define __INTEL_GMBUS_H__

#include <linux/types.h>

struct drm_i915_private;
struct i2c_adapter;

int intel_gmbus_setup(struct drm_i915_private *dev_priv);
void intel_gmbus_teardown(struct drm_i915_private *dev_priv);
bool intel_gmbus_is_valid_pin(struct drm_i915_private *dev_priv,
			      unsigned int pin);
int intel_gmbus_output_aksv(struct i2c_adapter *adapter);

struct i2c_adapter *
intel_gmbus_get_adapter(struct drm_i915_private *dev_priv, unsigned int pin);
void intel_gmbus_set_speed(struct i2c_adapter *adapter, int speed);
void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit);
bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter);
void intel_gmbus_reset(struct drm_i915_private *dev_priv);

#endif /* __INTEL_GMBUS_H__ */
