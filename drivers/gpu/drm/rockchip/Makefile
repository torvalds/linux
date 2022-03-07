# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

rockchipdrm-y := rockchip_drm_drv.o rockchip_drm_fb.o \
		rockchip_drm_gem.o rockchip_drm_logo.o \

rockchipdrm-$(CONFIG_ROCKCHIP_VOP) += rockchip_drm_vop.o rockchip_vop_reg.o
rockchipdrm-$(CONFIG_ROCKCHIP_VOP2) += rockchip_drm_vop2.o rockchip_vop2_reg.o

rockchipdrm-$(CONFIG_DRM_FBDEV_EMULATION) += rockchip_drm_fbdev.o
rockchipdrm-$(CONFIG_ROCKCHIP_DRM_DEBUG) += rockchip_drm_debugfs.o
rockchipdrm-$(CONFIG_ROCKCHIP_DRM_DIRECT_SHOW) += rockchip_drm_direct_show.o
rockchipdrm-$(CONFIG_ROCKCHIP_DRM_SELF_TEST) += rockchip_drm_display_pattern.o	\
						rockchip_drm_self_test.o

rockchipdrm-$(CONFIG_ROCKCHIP_ANALOGIX_DP) += analogix_dp-rockchip.o
rockchipdrm-$(CONFIG_ROCKCHIP_CDN_DP) += cdn-dp-core.o cdn-dp-reg.o
rockchipdrm-$(CONFIG_ROCKCHIP_DW_HDMI) += dw_hdmi-rockchip.o
rockchipdrm-$(CONFIG_ROCKCHIP_DW_MIPI_DSI) += dw-mipi-dsi-rockchip.o \
					      dw-mipi-dsi2-rockchip.o
rockchipdrm-$(CONFIG_ROCKCHIP_DW_DP) += dw-dp.o
rockchipdrm-$(CONFIG_ROCKCHIP_INNO_HDMI) += inno_hdmi.o
rockchipdrm-$(CONFIG_ROCKCHIP_LVDS) += rockchip_lvds.o
rockchipdrm-$(CONFIG_ROCKCHIP_RGB) += rockchip_rgb.o
rockchipdrm-$(CONFIG_ROCKCHIP_RK3066_HDMI) += rk3066_hdmi.o
rockchipdrm-$(CONFIG_ROCKCHIP_VCONN) += rockchip_drm_vconn.o
rockchipdrm-$(CONFIG_DRM_ROCKCHIP_VVOP) += rockchip_drm_vvop.o

obj-$(CONFIG_DRM_ROCKCHIP) += rockchipdrm.o

obj-$(CONFIG_DRM_ROCKCHIP_RK628) += rk628/
