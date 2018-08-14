#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

rockchipdrm-y := rockchip_drm_drv.o rockchip_drm_fb.o \
		rockchip_drm_gem.o rockchip_drm_vop.o
rockchipdrm-$(CONFIG_DRM_FBDEV_EMULATION) += rockchip_drm_fbdev.o

obj-$(CONFIG_ROCKCHIP_DW_HDMI) += dw_hdmi-rockchip.o
obj-$(CONFIG_ROCKCHIP_CDN_DP) += cdn-dp.o
cdn-dp-objs := cdn-dp-core.o cdn-dp-reg.o cdn-dp-link-training.o
obj-$(CONFIG_ROCKCHIP_DW_MIPI_DSI) += dw-mipi-dsi.o
obj-$(CONFIG_ROCKCHIP_MIPI_CSI_TX) += rockchip-mipi-csi-tx.o
obj-$(CONFIG_ROCKCHIP_ANALOGIX_DP) += analogix_dp-rockchip.o
obj-$(CONFIG_ROCKCHIP_INNO_HDMI) += inno_hdmi.o
obj-$(CONFIG_ROCKCHIP_LVDS) += rockchip_lvds.o
obj-$(CONFIG_ROCKCHIP_RGB) += rockchip_rgb.o

obj-$(CONFIG_ROCKCHIP_DRM_BACKLIGHT) += rockchip_drm_backlight.o
obj-$(CONFIG_DRM_ROCKCHIP) += rockchip_vop_reg.o rockchipdrm.o
obj-$(CONFIG_ROCKCHIP_DRM_TVE) += rockchip_drm_tve.o

obj-$(CONFIG_ROCKCHIP_RK3066_HDMI) += rk3066_hdmi.o
obj-$(CONFIG_DRM_ROCKCHIP_RK618) += rk618/
