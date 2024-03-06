# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_APERTURE_HELPERS)    += aperture.o
obj-$(CONFIG_STI_CORE)            += sticore.o
obj-$(CONFIG_VGASTATE)            += vgastate.o
obj-$(CONFIG_VIDEO_CMDLINE)       += cmdline.o
obj-$(CONFIG_VIDEO_NOMODESET)     += nomodeset.o
obj-$(CONFIG_HDMI)                += hdmi.o

obj-$(CONFIG_VT)		  += console/
obj-$(CONFIG_FB_STI)		  += console/
obj-$(CONFIG_LOGO)		  += logo/
obj-y				  += backlight/

obj-y				  += fbdev/

obj-$(CONFIG_VIDEOMODE_HELPERS) += display_timing.o videomode.o
ifeq ($(CONFIG_OF),y)
obj-$(CONFIG_VIDEOMODE_HELPERS) += of_display_timing.o of_videomode.o
endif
