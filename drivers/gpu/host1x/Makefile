ccflags-y = -Idrivers/gpu/host1x

host1x-y = \
	syncpt.o \
	dev.o \
	intr.o \
	cdma.o \
	channel.o \
	job.o \
	debug.o \
	hw/host1x01.o

ccflags-y += -Iinclude/drm
ccflags-$(CONFIG_DRM_TEGRA_DEBUG) += -DDEBUG

host1x-$(CONFIG_DRM_TEGRA) += drm/drm.o drm/fb.o drm/dc.o
host1x-$(CONFIG_DRM_TEGRA) += drm/output.o drm/rgb.o drm/hdmi.o
host1x-$(CONFIG_DRM_TEGRA) += drm/gem.o
host1x-$(CONFIG_DRM_TEGRA) += drm/gr2d.o
obj-$(CONFIG_TEGRA_HOST1X) += host1x.o
