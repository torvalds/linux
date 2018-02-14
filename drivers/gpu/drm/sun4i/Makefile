# SPDX-License-Identifier: GPL-2.0
sun4i-backend-y			+= sun4i_backend.o sun4i_layer.o
sun4i-frontend-y		+= sun4i_frontend.o

sun4i-drm-y			+= sun4i_drv.o
sun4i-drm-y			+= sun4i_framebuffer.o

sun4i-drm-hdmi-y		+= sun4i_hdmi_ddc_clk.o
sun4i-drm-hdmi-y		+= sun4i_hdmi_enc.o
sun4i-drm-hdmi-y		+= sun4i_hdmi_i2c.o
sun4i-drm-hdmi-y		+= sun4i_hdmi_tmds_clk.o

sun8i-drm-hdmi-y		+= sun8i_dw_hdmi.o
sun8i-drm-hdmi-y		+= sun8i_hdmi_phy.o

sun8i-mixer-y			+= sun8i_mixer.o sun8i_ui_layer.o \
				   sun8i_vi_layer.o sun8i_ui_scaler.o \
				   sun8i_vi_scaler.o sun8i_csc.o

sun4i-tcon-y			+= sun4i_crtc.o
sun4i-tcon-y			+= sun4i_dotclock.o
sun4i-tcon-y			+= sun4i_lvds.o
sun4i-tcon-y			+= sun4i_tcon.o
sun4i-tcon-y			+= sun4i_rgb.o

obj-$(CONFIG_DRM_SUN4I)		+= sun4i-drm.o
obj-$(CONFIG_DRM_SUN4I)		+= sun4i-tcon.o
obj-$(CONFIG_DRM_SUN4I)		+= sun4i_tv.o
obj-$(CONFIG_DRM_SUN4I)		+= sun6i_drc.o

obj-$(CONFIG_DRM_SUN4I_BACKEND)	+= sun4i-backend.o sun4i-frontend.o
obj-$(CONFIG_DRM_SUN4I_HDMI)	+= sun4i-drm-hdmi.o
obj-$(CONFIG_DRM_SUN8I_DW_HDMI)	+= sun8i-drm-hdmi.o
obj-$(CONFIG_DRM_SUN8I_MIXER)	+= sun8i-mixer.o
