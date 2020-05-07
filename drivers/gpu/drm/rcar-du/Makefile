# SPDX-License-Identifier: GPL-2.0
rcar-du-drm-y := rcar_du_crtc.o \
		 rcar_du_drv.o \
		 rcar_du_encoder.o \
		 rcar_du_group.o \
		 rcar_du_kms.o \
		 rcar_du_plane.o \

rcar-du-drm-$(CONFIG_DRM_RCAR_LVDS)	+= rcar_du_of.o \
					   rcar_du_of_lvds_r8a7790.dtb.o \
					   rcar_du_of_lvds_r8a7791.dtb.o \
					   rcar_du_of_lvds_r8a7793.dtb.o \
					   rcar_du_of_lvds_r8a7795.dtb.o \
					   rcar_du_of_lvds_r8a7796.dtb.o
rcar-du-drm-$(CONFIG_DRM_RCAR_VSP)	+= rcar_du_vsp.o
rcar-du-drm-$(CONFIG_DRM_RCAR_WRITEBACK) += rcar_du_writeback.o

obj-$(CONFIG_DRM_RCAR_CMM)		+= rcar_cmm.o
obj-$(CONFIG_DRM_RCAR_DU)		+= rcar-du-drm.o
obj-$(CONFIG_DRM_RCAR_DW_HDMI)		+= rcar_dw_hdmi.o
obj-$(CONFIG_DRM_RCAR_LVDS)		+= rcar_lvds.o

# 'remote-endpoint' is fixed up at run-time
DTC_FLAGS_rcar_du_of_lvds_r8a7790 += -Wno-graph_endpoint
DTC_FLAGS_rcar_du_of_lvds_r8a7791 += -Wno-graph_endpoint
DTC_FLAGS_rcar_du_of_lvds_r8a7793 += -Wno-graph_endpoint
DTC_FLAGS_rcar_du_of_lvds_r8a7795 += -Wno-graph_endpoint
DTC_FLAGS_rcar_du_of_lvds_r8a7796 += -Wno-graph_endpoint
