#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/rockchip
rockchipdrm-y := rockchip_drm_drv.o rockchip_drm_encoder.o rockchip_drm_connector.o \
		rockchip_drm_crtc.o rockchip_drm_fbdev.o rockchip_drm_fb.o \
		rockchip_drm_buf.o rockchip_drm_gem.o rockchip_drm_core.o \
		rockchip_drm_plane.o 

rockchipdrm-$(CONFIG_DRM_ROCKCHIP_IOMMU) += rockchip_drm_iommu.o
rockchipdrm-$(CONFIG_DRM_ROCKCHIP_DMABUF) += rockchip_drm_dmabuf.o
rockchipdrm-$(CONFIG_DRM_ROCKCHIP_PRIMARY)	+= rockchip_drm_primary.o
rockchipdrm-$(CONFIG_DRM_ROCKCHIP_HDMI)	+= rockchip_drm_extend.o
rockchipdrm-$(CONFIG_DRM_ROCKCHIP_VIDI)	+= rockchip_drm_vidi.o

obj-$(CONFIG_DRM_ROCKCHIP)		+= rockchipdrm.o
