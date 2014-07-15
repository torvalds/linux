#
# Makefile for HDMI linux kernel module.
#

ccflags-$(CONFIG_RK_HDMI_DEBUG) = -DDEBUG -DHDMI_DEBUG
ccflags-$(CONFIG_HDCP_RK616_DEBUG) = -DHDCP_DEBUG

obj-$(CONFIG_HDMI_RK3036) += rk3036_hdmi_hw.o rk3036_hdmi.o
obj-$(CONFIG_HDCP_RK3036) += rk3036_hdmi_hdcp.o rk3036_hdcp.o
