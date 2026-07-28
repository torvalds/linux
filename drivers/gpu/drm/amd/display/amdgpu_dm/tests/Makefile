# SPDX-License-Identifier: GPL-2.0
#
# Makefile for amdgpu_dm KUnit tests.

ccflags-y += -I$(src)/..
ccflags-y += -I$(src)/../..
ccflags-y += -I$(src)/../../include
ccflags-y += -I$(src)/../../modules/inc
ccflags-y += -I$(src)/../../dc
ccflags-y += -I$(src)/../../../amdgpu
ccflags-y += -I$(src)/../../../amdkfd
ccflags-y += -I$(src)/../../../include

obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_kunit_helpers.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_crc_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_hdcp_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_audio_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_color_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_colorop_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_connector_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_backlight_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_dmub_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_psr_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_replay_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_ism_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_irq_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_wb_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_plane_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_mst_types_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_pp_smu_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_crtc_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_services_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_helpers_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_quirks_test.o
obj-$(CONFIG_DRM_AMD_DC_KUNIT_TEST) += amdgpu_dm_plane_test.o
