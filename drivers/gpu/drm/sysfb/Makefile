# SPDX-License-Identifier: GPL-2.0-only

drm_sysfb_helper-y := \
	drm_sysfb.o \
	drm_sysfb_modeset.o
drm_sysfb_helper-$(CONFIG_SCREEN_INFO) += drm_sysfb_screen_info.o
obj-$(CONFIG_DRM_SYSFB_HELPER)	+= drm_sysfb_helper.o

obj-$(CONFIG_DRM_EFIDRM)	+= efidrm.o
obj-$(CONFIG_DRM_OFDRM)		+= ofdrm.o
obj-$(CONFIG_DRM_SIMPLEDRM)	+= simpledrm.o
obj-$(CONFIG_DRM_VESADRM)	+= vesadrm.o
