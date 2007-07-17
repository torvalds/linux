#
# Makefile for the new OMAP framebuffer device driver
#

obj-$(CONFIG_FB_OMAP) += omapfb.o

objs-yy := omapfb_main.o

objs-y$(CONFIG_ARCH_OMAP1) += lcdc.o
objs-y$(CONFIG_ARCH_OMAP2) += dispc.o

omapfb-objs := $(objs-yy)

