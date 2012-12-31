# drivers/media/video/samsung/tvout/Makefile
#
# Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
#	http://www.samsung.com/
# Tomasz Stanislawski <t.stanislaws@samsung.com>
#
# Licensed under GPL
obj-$(CONFIG_VIDEO_EXYNOS_HDMIPHY) += s5p-hdmiphy.o
s5p-hdmiphy-y += hdmiphy_drv.o
obj-$(CONFIG_VIDEO_EXYNOS_HDMI) += s5p-hdmi.o
s5p-hdmi-y += hdcp_drv.o hdmi_drv.o
obj-$(CONFIG_VIDEO_EXYNOS_SDO) += s5p-sdo.o
s5p-sdo-y += sdo_drv.o
obj-$(CONFIG_VIDEO_EXYNOS_MIXER) += s5p-mixer.o
s5p-mixer-y += mixer_vb2.o mixer_drv.o mixer_video.o mixer_reg.o mixer_grp_layer.o
obj-$(CONFIG_VIDEO_EXYNOS_HDMI_CEC) += s5p-hdmi_cec.o
s5p-hdmi_cec-y += hdmi_cec.o hdmi_cec_ctrl.o

ifeq ($(CONFIG_ARCH_EXYNOS4), y)
	s5p-mixer-y += mixer_vp_layer.o
else
	s5p-mixer-y += mixer_video_layer.o
endif

ifeq ($(CONFIG_VIDEO_EXYNOS_HDMI),y)
	ifeq ($(CONFIG_CPU_EXYNOS4210), y)
		s5p-hdmi-y += hdmi_reg_4210.o hdmiphy_conf_4210.o
	else
		s5p-hdmi-y += hdmi_reg_5250.o hdmiphy_conf_5250.o
	endif
endif
