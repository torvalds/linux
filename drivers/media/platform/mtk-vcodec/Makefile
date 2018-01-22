# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_VIDEO_MEDIATEK_VCODEC) += mtk-vcodec-dec.o \
				       mtk-vcodec-enc.o \
				       mtk-vcodec-common.o

mtk-vcodec-dec-y := vdec/vdec_h264_if.o \
		vdec/vdec_vp8_if.o \
		vdec/vdec_vp9_if.o \
		mtk_vcodec_dec_drv.o \
		vdec_drv_if.o \
		vdec_vpu_if.o \
		mtk_vcodec_dec.o \
		mtk_vcodec_dec_pm.o \


mtk-vcodec-enc-y := venc/venc_vp8_if.o \
		venc/venc_h264_if.o \
		mtk_vcodec_enc.o \
		mtk_vcodec_enc_drv.o \
		mtk_vcodec_enc_pm.o \
		venc_drv_if.o \
		venc_vpu_if.o \


mtk-vcodec-common-y := mtk_vcodec_intr.o \
		mtk_vcodec_util.o\

ccflags-y += -I$(srctree)/drivers/media/platform/mtk-vpu
