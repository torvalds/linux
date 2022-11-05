# SPDX-License-Identifier: GPL-2.0+
visl-y := visl-core.o visl-video.o visl-dec.o visl-trace-points.o

ifeq ($(CONFIG_VISL_DEBUGFS),y)
  visl-y += visl-debugfs.o
endif

obj-$(CONFIG_VIDEO_VISL) += visl.o
