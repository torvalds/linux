# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the OMAP1 framebuffer device driver
#

obj-$(CONFIG_FB_OMAP) += omapfb.o

objs-yy := omapfb_main.o lcdc.o

objs-y$(CONFIG_FB_OMAP_LCDC_EXTERNAL) += sossi.o

objs-y$(CONFIG_FB_OMAP_LCDC_HWA742) += hwa742.o

lcds-y$(CONFIG_MACH_AMS_DELTA) += lcd_ams_delta.o
lcds-y$(CONFIG_FB_OMAP_LCD_H3) += lcd_h3.o
lcds-y$(CONFIG_MACH_OMAP_PALMTE) += lcd_palmte.o
lcds-y$(CONFIG_MACH_OMAP_PALMTT) += lcd_palmtt.o
lcds-y$(CONFIG_MACH_OMAP_PALMZ71) += lcd_palmz71.o
lcds-$(CONFIG_ARCH_OMAP16XX)$(CONFIG_MACH_OMAP_INNOVATOR) += lcd_inn1610.o
lcds-$(CONFIG_ARCH_OMAP15XX)$(CONFIG_MACH_OMAP_INNOVATOR) += lcd_inn1510.o
lcds-y$(CONFIG_MACH_OMAP_OSK) += lcd_osk.o

lcds-y$(CONFIG_FB_OMAP_LCD_MIPID) += lcd_mipid.o
lcds-y$(CONFIG_MACH_HERALD) += lcd_htcherald.o

omapfb-objs := $(objs-yy)

obj-$(CONFIG_FB_OMAP) += $(lcds-yy)
