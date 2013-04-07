#
# Makefile for HDMI linux kernel module.
#

ccflags-$(CONFIG_RK_HDMI_DEBUG) = -DDEBUG -DHDMI_DEBUG
ccflags-$(CONFIG_HDCP_RK616_DEBUG) = -DHDCP_DEBUG

obj-$(CONFIG_HDMI_RK616) += rk616_hdmi_hw.o rk616_hdmi.o
obj-$(CONFIG_HDCP_RK616) += rk616_hdmi_hdcp.o rk616_hdcp.o
