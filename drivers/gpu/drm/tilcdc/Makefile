# SPDX-License-Identifier: GPL-2.0
ifeq (, $(findstring -W,$(EXTRA_CFLAGS)))
	ccflags-y += -Werror
endif

tilcdc-y := \
	tilcdc_plane.o \
	tilcdc_crtc.o \
	tilcdc_panel.o \
	tilcdc_external.o \
	tilcdc_drv.o

obj-$(CONFIG_DRM_TILCDC)	+= tilcdc.o
