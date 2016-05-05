#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

exynosdrm-y := exynos_drm_drv.o exynos_drm_crtc.o exynos_drm_fb.o \
		exynos_drm_gem.o exynos_drm_core.o exynos_drm_plane.o

exynosdrm-$(CONFIG_DRM_FBDEV_EMULATION) += exynos_drm_fbdev.o
exynosdrm-$(CONFIG_DRM_EXYNOS_IOMMU) += exynos_drm_iommu.o
exynosdrm-$(CONFIG_DRM_EXYNOS_FIMD)	+= exynos_drm_fimd.o
exynosdrm-$(CONFIG_DRM_EXYNOS5433_DECON)	+= exynos5433_drm_decon.o
exynosdrm-$(CONFIG_DRM_EXYNOS7_DECON)	+= exynos7_drm_decon.o
exynosdrm-$(CONFIG_DRM_EXYNOS_DPI)	+= exynos_drm_dpi.o
exynosdrm-$(CONFIG_DRM_EXYNOS_DSI)	+= exynos_drm_dsi.o
exynosdrm-$(CONFIG_DRM_EXYNOS_DP)	+= exynos_dp_core.o exynos_dp_reg.o
exynosdrm-$(CONFIG_DRM_EXYNOS_MIXER)	+= exynos_mixer.o
exynosdrm-$(CONFIG_DRM_EXYNOS_HDMI)	+= exynos_hdmi.o
exynosdrm-$(CONFIG_DRM_EXYNOS_VIDI)	+= exynos_drm_vidi.o
exynosdrm-$(CONFIG_DRM_EXYNOS_G2D)	+= exynos_drm_g2d.o
exynosdrm-$(CONFIG_DRM_EXYNOS_IPP)	+= exynos_drm_ipp.o
exynosdrm-$(CONFIG_DRM_EXYNOS_FIMC)	+= exynos_drm_fimc.o
exynosdrm-$(CONFIG_DRM_EXYNOS_ROTATOR)	+= exynos_drm_rotator.o
exynosdrm-$(CONFIG_DRM_EXYNOS_GSC)	+= exynos_drm_gsc.o
exynosdrm-$(CONFIG_DRM_EXYNOS_MIC)     += exynos_drm_mic.o

obj-$(CONFIG_DRM_EXYNOS)		+= exynosdrm.o
