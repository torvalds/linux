# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the OMAP1 framebuffer device driver
#

obj-$(CONFIG_FB_OMAP) += omapfb.o

ifdef CONFIG_FB_OMAP
# must be built-in
obj-y += lcd_dma.o
endif

objs-yy := omapfb_main.o lcdc.o

objs-y$(CONFIG_FB_OMAP_LCDC_EXTERNAL) += sossi.o

objs-y$(CONFIG_FB_OMAP_LCDC_HWA742) += hwa742.o

lcds-y$(CONFIG_MACH_AMS_DELTA) += lcd_ams_delta.o
lcds-y$(CONFIG_MACH_OMAP_PALMTE) += lcd_palmte.o
lcds-y$(CONFIG_MACH_OMAP_OSK) += lcd_osk.o

lcds-y$(CONFIG_FB_OMAP_LCD_MIPID) += lcd_mipid.o

omapfb-objs := $(objs-yy)

obj-$(CONFIG_FB_OMAP) += $(lcds-yy)
