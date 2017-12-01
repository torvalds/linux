armada-y	:= armada_crtc.o armada_drv.o armada_fb.o armada_fbdev.o \
		   armada_gem.o armada_overlay.o
armada-y	+= armada_510.o
armada-$(CONFIG_DEBUG_FS) += armada_debugfs.o

obj-$(CONFIG_DRM_ARMADA) := armada.o

CFLAGS_armada_trace.o := -I$(src)
