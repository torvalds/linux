# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_ROCKCHIP_ISP) += video_rkisp.o

video_rkisp-objs += rkisp.o \
			dev.o \
			regs.o \
			isp_stats.o \
			isp_stats_v1x.o \
			isp_stats_v2x.o \
			isp_params.o \
			isp_params_v1x.o \
			isp_params_v2x.o \
			capture.o \
			dmarx.o \
			csi.o \
			bridge.o \
			isp_mipi_luma.o

ifdef CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP
video_rkisp-objs += rkisp_tb_helper.o
endif
