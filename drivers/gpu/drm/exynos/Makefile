# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

exyyessdrm-y := exyyess_drm_drv.o exyyess_drm_crtc.o exyyess_drm_fb.o \
		exyyess_drm_gem.o exyyess_drm_plane.o exyyess_drm_dma.o

exyyessdrm-$(CONFIG_DRM_FBDEV_EMULATION) += exyyess_drm_fbdev.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_FIMD)	+= exyyess_drm_fimd.o
exyyessdrm-$(CONFIG_DRM_EXYNOS5433_DECON)	+= exyyess5433_drm_decon.o
exyyessdrm-$(CONFIG_DRM_EXYNOS7_DECON)	+= exyyess7_drm_decon.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_DPI)	+= exyyess_drm_dpi.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_DSI)	+= exyyess_drm_dsi.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_DP)	+= exyyess_dp.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_MIXER)	+= exyyess_mixer.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_HDMI)	+= exyyess_hdmi.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_VIDI)	+= exyyess_drm_vidi.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_G2D)	+= exyyess_drm_g2d.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_IPP)	+= exyyess_drm_ipp.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_FIMC)	+= exyyess_drm_fimc.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_ROTATOR)	+= exyyess_drm_rotator.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_SCALER)	+= exyyess_drm_scaler.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_GSC)	+= exyyess_drm_gsc.o
exyyessdrm-$(CONFIG_DRM_EXYNOS_MIC)     += exyyess_drm_mic.o

obj-$(CONFIG_DRM_EXYNOS)		+= exyyessdrm.o
