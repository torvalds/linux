ccflags-y := -Iinclude/drm
ccflags-$(CONFIG_DRM_TEGRA_DEBUG) += -DDEBUG

tegra-drm-y := drm.o fb.o dc.o host1x.o
tegra-drm-y += output.o rgb.o hdmi.o

obj-$(CONFIG_DRM_TEGRA) += tegra-drm.o
