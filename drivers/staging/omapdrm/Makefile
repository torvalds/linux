#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI)
#

ccflags-y := -Iinclude/drm -Werror
omapdrm-y := omap_drv.o \
	omap_debugfs.o \
	omap_crtc.o \
	omap_encoder.o \
	omap_connector.o \
	omap_fb.o \
	omap_fbdev.o \
	omap_gem.o \
	omap_dmm_tiler.o \
	tcm-sita.o

# temporary:
omapdrm-y += omap_gem_helpers.o

obj-$(CONFIG_DRM_OMAP)	+= omapdrm.o
