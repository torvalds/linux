# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Riva framebuffer driver
#

obj-$(CONFIG_FB_RIVA)     += rivafb.o

rivafb-objs               := fbdev.o riva_hw.o nv_driver.o

ifdef CONFIG_FB_RIVA_I2C
	rivafb-objs       += rivafb-i2c.o
endif
