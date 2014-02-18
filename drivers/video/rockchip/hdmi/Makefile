#
# Makefile for HDMI linux kernel module.
#

ccflags-$(CONFIG_RK_HDMI_DEBUG) = -DDEBUG -DHDMI_DEBUG

obj-$(CONFIG_RK_HDMI) += rk_hdmi_edid.o rk_hdmi_lcdc.o rk_hdmi_task.o rk_hdmi_sysfs.o rk_hdmi_parse_dt.o
obj-$(CONFIG_RK_HDMI) += chips/
