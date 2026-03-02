# SPDX-License-Identifier: GPL-2.0
ifeq (, $(findstring -W,$(KCFLAGS)))
	ccflags-y += -Werror
endif

tilcdc-y := \
	tilcdc_plane.o \
	tilcdc_crtc.o \
	tilcdc_encoder.o \
	tilcdc_drv.o

obj-$(CONFIG_DRM_TILCDC)	+= tilcdc.o
obj-$(CONFIG_DRM_TILCDC_PANEL_LEGACY)	+= tilcdc_panel_legacy.o \
					   tilcdc_panel_legacy.dtbo.o
