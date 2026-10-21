# SPDX-License-Identifier: GPL-2.0

subdir-ccflags-y += -I$(srctree)/drivers/gpu/drm/i915/display/

obj-$(CONFIG_DRM_I915_KUNIT_TEST) += i915_display_test.o
i915_display_test-y = \
	intel_dp_link_test.o
