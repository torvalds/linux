# SPDX-License-Identifier: GPL-2.0
ccflags-$(CONFIG_DRM_TEGRA_DEBUG) += -DDEBUG

tegra-drm-y := \
	drm.o \
	uapi.o \
	submit.o \
	firewall.o \
	gem.o \
	fb.o \
	dp.o \
	hub.o \
	plane.o \
	dc.o \
	output.o \
	rgb.o \
	hda.o \
	hdmi.o \
	mipi-phy.o \
	dsi.o \
	sor.o \
	dpaux.o \
	gr2d.o \
	gr3d.o \
	falcon.o \
	vic.o

tegra-drm-y += trace.o

obj-$(CONFIG_DRM_TEGRA) += tegra-drm.o
