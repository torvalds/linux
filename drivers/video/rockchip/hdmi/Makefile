# SPDX-License-Identifier: GPL-2.0
#
# Makefile for HDMI linux kernel module.
#

ccflags-$(CONFIG_RK_HDMI_DEBUG) = -DDEBUG -DHDMI_DEBUG

obj-$(CONFIG_RK_HDMI) += rockchip-hdmi-core.o rockchip-hdmi-lcdc.o rockchip-hdmi-edid.o rockchip-hdmi-sysfs.o rockchip-hdmi-cec.o
obj-$(CONFIG_RK_HDMI_V2) += rockchip-hdmiv2/
obj-$(CONFIG_RK_HDMI_V1) += rockchip-hdmiv1/
