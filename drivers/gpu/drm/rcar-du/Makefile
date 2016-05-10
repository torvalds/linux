rcar-du-drm-y := rcar_du_crtc.o \
		 rcar_du_drv.o \
		 rcar_du_encoder.o \
		 rcar_du_group.o \
		 rcar_du_kms.o \
		 rcar_du_lvdscon.o \
		 rcar_du_plane.o \
		 rcar_du_vgacon.o

rcar-du-drm-$(CONFIG_DRM_RCAR_HDMI)	+= rcar_du_hdmicon.o \
					   rcar_du_hdmienc.o
rcar-du-drm-$(CONFIG_DRM_RCAR_LVDS)	+= rcar_du_lvdsenc.o

rcar-du-drm-$(CONFIG_DRM_RCAR_VSP)	+= rcar_du_vsp.o

obj-$(CONFIG_DRM_RCAR_DU)		+= rcar-du-drm.o
