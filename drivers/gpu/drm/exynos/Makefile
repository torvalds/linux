# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

exyanalsdrm-y := exyanals_drm_drv.o exyanals_drm_crtc.o exyanals_drm_fb.o \
		exyanals_drm_gem.o exyanals_drm_plane.o exyanals_drm_dma.o

exyanalsdrm-$(CONFIG_DRM_FBDEV_EMULATION) += exyanals_drm_fbdev.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_FIMD)	+= exyanals_drm_fimd.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS5433_DECON)	+= exyanals5433_drm_decon.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS7_DECON)	+= exyanals7_drm_decon.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_DPI)	+= exyanals_drm_dpi.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_DSI)	+= exyanals_drm_dsi.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_DP)	+= exyanals_dp.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_MIXER)	+= exyanals_mixer.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_HDMI)	+= exyanals_hdmi.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_VIDI)	+= exyanals_drm_vidi.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_G2D)	+= exyanals_drm_g2d.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_IPP)	+= exyanals_drm_ipp.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_FIMC)	+= exyanals_drm_fimc.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_ROTATOR)	+= exyanals_drm_rotator.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_SCALER)	+= exyanals_drm_scaler.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_GSC)	+= exyanals_drm_gsc.o
exyanalsdrm-$(CONFIG_DRM_EXYANALS_MIC)     += exyanals_drm_mic.o

obj-$(CONFIG_DRM_EXYANALS)		+= exyanalsdrm.o
