# SPDX-License-Identifier: GPL-2.0

rockchip-isp1-y := rkisp1-capture.o \
		   rkisp1-common.o \
		   rkisp1-csi.o \
		   rkisp1-dev.o \
		   rkisp1-isp.o \
		   rkisp1-resizer.o \
		   rkisp1-stats.o \
		   rkisp1-params.o

rockchip-isp1-$(CONFIG_DEBUG_FS) += rkisp1-debug.o

obj-$(CONFIG_VIDEO_ROCKCHIP_ISP1) += rockchip-isp1.o
