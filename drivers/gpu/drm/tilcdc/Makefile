ccflags-y := -Iinclude/drm
ifeq (, $(findstring -W,$(EXTRA_CFLAGS)))
	ccflags-y += -Werror
endif

tilcdc-y := \
	tilcdc_crtc.o \
	tilcdc_tfp410.o \
	tilcdc_slave.o \
	tilcdc_panel.o \
	tilcdc_drv.o

obj-$(CONFIG_DRM_TILCDC)	+= tilcdc.o
