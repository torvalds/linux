# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_VIDEO_HANTRO) += hantro-vpu.o

hantro-vpu-y += \
		hantro_drv.o \
		hantro_v4l2.o \
		hantro_postproc.o \
		hantro_h1_jpeg_enc.o \
		hantro_g1.o \
		hantro_g1_h264_dec.o \
		hantro_g1_mpeg2_dec.o \
		hantro_g1_vp8_dec.o \
		hantro_g2.o \
		hantro_g2_hevc_dec.o \
		hantro_g2_vp9_dec.o \
		hantro_jpeg.o \
		hantro_h264.o \
		hantro_hevc.o \
		hantro_mpeg2.o \
		hantro_vp8.o \
		hantro_vp9.o

hantro-vpu-$(CONFIG_VIDEO_HANTRO_IMX8M) += \
		imx8m_vpu_hw.o

hantro-vpu-$(CONFIG_VIDEO_HANTRO_SAMA5D4) += \
		sama5d4_vdec_hw.o

hantro-vpu-$(CONFIG_VIDEO_HANTRO_ROCKCHIP) += \
		rockchip_vpu2_hw_jpeg_enc.o \
		rockchip_vpu2_hw_h264_dec.o \
		rockchip_vpu2_hw_mpeg2_dec.o \
		rockchip_vpu2_hw_vp8_dec.o \
		rockchip_vpu981_hw_av1_dec.o \
		rockchip_av1_filmgrain.o \
		rockchip_av1_entropymode.o \
		rockchip_vpu_hw.o

hantro-vpu-$(CONFIG_VIDEO_HANTRO_SUNXI) += \
		sunxi_vpu_hw.o

hantro-vpu-$(CONFIG_VIDEO_HANTRO_STM32MP25) += \
		stm32mp25_vpu_hw.o
