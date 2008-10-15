#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
i830-y := i830_drv.o i830_dma.o i830_irq.o

obj-$(CONFIG_DRM_I830) += i830.o
