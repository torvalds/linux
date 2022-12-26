# SPDX-License-Identifier: GPL-2.0
rcar-du-drm-y := rcar_du_crtc.o \
		 rcar_du_drv.o \
		 rcar_du_encoder.o \
		 rcar_du_group.o \
		 rcar_du_kms.o \
		 rcar_du_plane.o \

rcar-du-drm-$(CONFIG_DRM_RCAR_VSP)	+= rcar_du_vsp.o
rcar-du-drm-$(CONFIG_DRM_RCAR_WRITEBACK) += rcar_du_writeback.o

obj-$(CONFIG_DRM_RCAR_CMM)		+= rcar_cmm.o
obj-$(CONFIG_DRM_RCAR_DU)		+= rcar-du-drm.o
obj-$(CONFIG_DRM_RCAR_DW_HDMI)		+= rcar_dw_hdmi.o
obj-$(CONFIG_DRM_RCAR_LVDS)		+= rcar_lvds.o
obj-$(CONFIG_DRM_RCAR_MIPI_DSI)		+= rcar_mipi_dsi.o

obj-$(CONFIG_DRM_RZG2L_MIPI_DSI)	+= rzg2l_mipi_dsi.o
