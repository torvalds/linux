# SPDX-License-Identifier: GPL-2.0
armada-y	:= armada_crtc.o armada_drv.o armada_fb.o \
		   armada_gem.o armada_overlay.o armada_plane.o armada_trace.o
armada-y	+= armada_510.o
armada-$(CONFIG_DEBUG_FS) += armada_debugfs.o
armada-$(CONFIG_DRM_FBDEV_EMULATION) += armada_fbdev.o

obj-$(CONFIG_DRM_ARMADA) := armada.o
