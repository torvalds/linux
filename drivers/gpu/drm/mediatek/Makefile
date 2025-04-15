# SPDX-License-Identifier: GPL-2.0

mediatek-drm-y := mtk_crtc.o \
		  mtk_ddp_comp.o \
		  mtk_disp_aal.o \
		  mtk_disp_ccorr.o \
		  mtk_disp_color.o \
		  mtk_disp_gamma.o \
		  mtk_disp_merge.o \
		  mtk_disp_ovl.o \
		  mtk_disp_ovl_adaptor.o \
		  mtk_disp_rdma.o \
		  mtk_drm_drv.o \
		  mtk_dsi.o \
		  mtk_dpi.o \
		  mtk_ethdr.o \
		  mtk_gem.o \
		  mtk_mdp_rdma.o \
		  mtk_padding.o \
		  mtk_plane.o

obj-$(CONFIG_DRM_MEDIATEK) += mediatek-drm.o

obj-$(CONFIG_DRM_MEDIATEK_HDMI) += mtk_cec.o
obj-$(CONFIG_DRM_MEDIATEK_HDMI) += mtk_hdmi.o
obj-$(CONFIG_DRM_MEDIATEK_HDMI) += mtk_hdmi_ddc.o

obj-$(CONFIG_DRM_MEDIATEK_DP) += mtk_dp.o
