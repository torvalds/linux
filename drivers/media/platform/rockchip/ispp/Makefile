# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_ROCKCHIP_ISPP) += video_rkispp.o

video_rkispp-objs += hw.o \
		dev.o \
		common.o \
		ispp.o \
		stream.o \
		params.o \
		stats.o \
		procfs.o

video_rkispp-$(CONFIG_VIDEO_ROCKCHIP_ISPP_VERSION_V10) += \
		stream_v10.o \
		params_v10.o \

video_rkispp-$(CONFIG_VIDEO_ROCKCHIP_ISPP_VERSION_V20) += \
		stream_v20.o \
		params_v20.o \

ifdef CONFIG_VIDEO_ROCKCHIP_ISPP_FEC
video_rkispp-objs += fec.o
endif
