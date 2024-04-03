# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_VIDEO_MEDIATEK_VCODEC) += mtk-vcodec-common.o

mtk-vcodec-common-y := mtk_vcodec_intr.o \
		mtk_vcodec_util.o \
		mtk_vcodec_fw.o \

ifneq ($(CONFIG_VIDEO_MEDIATEK_VCODEC_VPU),)
mtk-vcodec-common-y += mtk_vcodec_fw_vpu.o
endif

ifneq ($(CONFIG_VIDEO_MEDIATEK_VCODEC_SCP),)
mtk-vcodec-common-y += mtk_vcodec_fw_scp.o
endif

ifneq ($(CONFIG_DEBUG_FS),)
obj-$(CONFIG_VIDEO_MEDIATEK_VCODEC) += mtk-vcodec-dbgfs.o

mtk-vcodec-dbgfs-y := mtk_vcodec_dbgfs.o
endif