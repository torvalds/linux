# SPDX-License-Identifier: GPL-2.0
#
# Makefile for HDMI linux kernel module.
#

ccflags-$(CONFIG_RK_HDMI_DEBUG) = -DDEBUG -DHDMI_DEBUG

obj-$(CONFIG_RK_HDMI_V1) += rockchip_hdmiv1_hw.o rockchip_hdmiv1.o rockchip_hdmiv1_cec.o rockchip_hdmiv1_hdcp.o
