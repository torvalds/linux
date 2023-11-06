# SPDX-License-Identifier: GPL-2.0
tegra-video-objs := \
		video.o \
		vi.o \
		vip.o \
		csi.o

tegra-video-$(CONFIG_ARCH_TEGRA_2x_SOC)  += tegra20.o
tegra-video-$(CONFIG_ARCH_TEGRA_210_SOC) += tegra210.o
obj-$(CONFIG_VIDEO_TEGRA) += tegra-video.o
