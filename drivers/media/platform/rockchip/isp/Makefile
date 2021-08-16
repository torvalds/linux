# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_ROCKCHIP_ISP) += video_rkisp.o

video_rkisp-objs += hw.o \
		dev.o \
		rkisp.o \
		regs.o \
		common.o \
		isp_stats.o \
		isp_params.o \
		capture.o \
		dmarx.o \
		csi.o \
		procfs.o \
		videobuf2-rdma-sg.o

video_rkisp-$(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V1X) += \
		isp_stats_v1x.o \
		isp_params_v1x.o \
		capture_v1x.o

video_rkisp-$(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V20) += \
		isp_stats_v2x.o \
		isp_params_v2x.o \
		capture_v20.o \
		bridge.o \
		isp_mipi_luma.o

video_rkisp-$(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V21) += \
		isp_stats_v21.o \
		isp_params_v21.o \
		capture_v21.o

video_rkisp-$(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) += rkisp_tb_helper.o
