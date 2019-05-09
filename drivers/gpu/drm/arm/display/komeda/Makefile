# SPDX-License-Identifier: GPL-2.0

ccflags-y := \
	-I $(srctree)/$(src)/../include \
	-I $(srctree)/$(src)

komeda-y := \
	komeda_drv.o \
	komeda_dev.o \
	komeda_format_caps.o \
	komeda_pipeline.o \
	komeda_pipeline_state.o \
	komeda_framebuffer.o \
	komeda_kms.o \
	komeda_crtc.o \
	komeda_plane.o \
	komeda_private_obj.o

komeda-y += \
	d71/d71_dev.o \
	d71/d71_component.o

obj-$(CONFIG_DRM_KOMEDA) += komeda.o
