# SPDX-License-Identifier: GPL-2.0
pl111_drm-y +=	pl111_display.o \
		pl111_versatile.o \
		pl111_drv.o

pl111_drm-$(CONFIG_ARCH_NOMADIK) += pl111_nomadik.o
pl111_drm-$(CONFIG_DEBUG_FS) += pl111_debugfs.o

obj-$(CONFIG_DRM_PL111) += pl111_drm.o
