#
# Makefile for HDMI linux kernel module.
#

ccflags-$(CONFIG_HDMI_RK30_DEBUG) = -DDEBUG -DHDMI_DEBUG

obj-y += rk2928_hdmi_hw.o rk2928_hdmi.o
obj-$(CONFIG_HDCP_RK2928) += hdcp/
