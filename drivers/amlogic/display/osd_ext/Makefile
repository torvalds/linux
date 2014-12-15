#
# Makefile for the AML Apollo framebuffer device driver
#


obj-$(CONFIG_AM_FB_EXT) += fb_ext.o
fb_ext-objs =  osd_hw.o osd_main.o osd_dev.o osd_clone.o

ifeq ($(CONFIG_ARCH_MESON8),y)
fb_ext-objs += osd_prot.o
endif

EXTRA_CFLAGS += -Iinclude/linux/osd/
EXTRA_CFLAGS += -Iinclude/linux/ge2d/
