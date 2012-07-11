#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/exynos
exynosdrm-y := exynos_drm_drv.o exynos_drm_encoder.o exynos_drm_connector.o \
		exynos_drm_crtc.o exynos_drm_fbdev.o exynos_drm_fb.o \
		exynos_drm_buf.o exynos_drm_gem.o exynos_drm_core.o \
		exynos_drm_plane.o

exynosdrm-$(CONFIG_DRM_EXYNOS_DMABUF) += exynos_drm_dmabuf.o
exynosdrm-$(CONFIG_DRM_EXYNOS_FIMD)	+= exynos_drm_fimd.o
exynosdrm-$(CONFIG_DRM_EXYNOS_HDMI)	+= exynos_hdmi.o exynos_mixer.o \
					   exynos_ddc.o exynos_hdmiphy.o \
					   exynos_drm_hdmi.o
exynosdrm-$(CONFIG_DRM_EXYNOS_VIDI)	+= exynos_drm_vidi.o
exynosdrm-$(CONFIG_DRM_EXYNOS_G2D)	+= exynos_drm_g2d.o

obj-$(CONFIG_DRM_EXYNOS)		+= exynosdrm.o
