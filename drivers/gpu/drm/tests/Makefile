# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_DRM_KUNIT_TEST_HELPERS) += \
	drm_kunit_helpers.o

obj-$(CONFIG_DRM_KUNIT_TEST) += \
	drm_atomic_state_test.o \
	drm_buddy_test.o \
	drm_cmdline_parser_test.o \
	drm_connector_test.o \
	drm_damage_helper_test.o \
	drm_dp_mst_helper_test.o \
	drm_exec_test.o \
	drm_format_helper_test.o \
	drm_format_test.o \
	drm_framebuffer_test.o \
	drm_gem_shmem_test.o \
	drm_hdmi_state_helper_test.o \
	drm_managed_test.o \
	drm_mm_test.o \
	drm_modes_test.o \
	drm_plane_helper_test.o \
	drm_probe_helper_test.o \
	drm_rect_test.o

CFLAGS_drm_mm_test.o := $(DISABLE_STRUCTLEAK_PLUGIN)
