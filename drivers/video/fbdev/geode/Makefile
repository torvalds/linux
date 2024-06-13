# SPDX-License-Identifier: GPL-2.0
# Makefile for the Geode family framebuffer drivers

obj-$(CONFIG_FB_GEODE_GX1) += gx1fb.o
obj-$(CONFIG_FB_GEODE_GX)  += gxfb.o
obj-$(CONFIG_FB_GEODE_LX)  += lxfb.o

gx1fb-objs := gx1fb_core.o display_gx1.o video_cs5530.o
gxfb-objs  := gxfb_core.o display_gx.o video_gx.o suspend_gx.o
lxfb-objs  := lxfb_core.o lxfb_ops.o
