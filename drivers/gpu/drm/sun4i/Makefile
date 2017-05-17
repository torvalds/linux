sun4i-drm-y += sun4i_drv.o
sun4i-drm-y += sun4i_framebuffer.o

sun4i-tcon-y += sun4i_tcon.o
sun4i-tcon-y += sun4i_rgb.o
sun4i-tcon-y += sun4i_dotclock.o
sun4i-tcon-y += sun4i_crtc.o

sun4i-backend-y += sun4i_backend.o sun4i_layer.o

sun8i-mixer-y += sun8i_mixer.o sun8i_layer.o

obj-$(CONFIG_DRM_SUN4I)		+= sun4i-drm.o sun4i-tcon.o
obj-$(CONFIG_DRM_SUN4I)		+= sun6i_drc.o
obj-$(CONFIG_DRM_SUN4I)		+= sun4i_tv.o

obj-$(CONFIG_DRM_SUN4I_BACKEND)		+= sun4i-backend.o
obj-$(CONFIG_DRM_SUN8I_MIXER)		+= sun8i-mixer.o
