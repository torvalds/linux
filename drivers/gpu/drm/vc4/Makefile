ccflags-y := -Iinclude/drm

# Please keep these build lists sorted!

# core driver code
vc4-y := \
	vc4_bo.o \
	vc4_crtc.o \
	vc4_drv.o \
	vc4_kms.o \
	vc4_hdmi.o \
	vc4_hvs.o \
	vc4_plane.o \
	vc4_v3d.o \
	vc4_validate_shaders.o

vc4-$(CONFIG_DEBUG_FS) += vc4_debugfs.o

obj-$(CONFIG_DRM_VC4)  += vc4.o
