# SPDX-License-Identifier: GPL-2.0-only
vkms-y := \
	vkms_drv.o \
	vkms_plane.o \
	vkms_output.o \
	vkms_formats.o \
	vkms_crtc.o \
	vkms_composer.o \
	vkms_writeback.o \
	vkms_connector.o \
	vkms_config.o

obj-$(CONFIG_DRM_VKMS) += vkms.o
obj-$(CONFIG_DRM_VKMS_KUNIT_TEST) += tests/
