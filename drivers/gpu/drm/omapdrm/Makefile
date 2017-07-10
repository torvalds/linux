#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI)
#

obj-y += dss/
obj-y += displays/

omapdrm-y := omap_drv.o \
	omap_irq.o \
	omap_debugfs.o \
	omap_crtc.o \
	omap_plane.o \
	omap_encoder.o \
	omap_connector.o \
	omap_fb.o \
	omap_gem.o \
	omap_gem_dmabuf.o \
	omap_dmm_tiler.o \
	tcm-sita.o

omapdrm-$(CONFIG_DRM_FBDEV_EMULATION) += omap_fbdev.o

obj-$(CONFIG_DRM_OMAP)	+= omapdrm.o
