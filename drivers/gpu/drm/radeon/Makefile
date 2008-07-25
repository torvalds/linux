#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
radeon-y := radeon_drv.o radeon_cp.o radeon_state.o radeon_mem.o radeon_irq.o r300_cmdbuf.o

radeon-$(CONFIG_COMPAT) += radeon_ioc32.o

obj-$(CONFIG_DRM_RADEON)+= radeon.o
