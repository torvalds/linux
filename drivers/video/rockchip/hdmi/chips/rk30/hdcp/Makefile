#
# Makefile for HDCP linux kernel module.
#

ccflags-$(CONFIG_HDCP_RK30_DEBUG) = -DDEBUG -DHDCP_DEBUG

obj-$(CONFIG_HDCP_RK30) += hdcp.o
hdcp-y := rk30_hdcp.o rk30_hdmi_hdcp.o
