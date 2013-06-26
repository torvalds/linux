ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/msm
ifeq (, $(findstring -W,$(EXTRA_CFLAGS)))
	ccflags-y += -Werror
endif

msm-y := \
	hdmi/hdmi.o \
	hdmi/hdmi_connector.o \
	hdmi/hdmi_i2c.o \
	hdmi/hdmi_phy_8960.o \
	hdmi/hdmi_phy_8x60.o \
	mdp4/mdp4_crtc.o \
	mdp4/mdp4_dtv_encoder.o \
	mdp4/mdp4_format.o \
	mdp4/mdp4_irq.o \
	mdp4/mdp4_kms.o \
	mdp4/mdp4_plane.o \
	msm_connector.o \
	msm_drv.o \
	msm_fb.o \
	msm_gem.o

msm-$(CONFIG_DRM_MSM_FBDEV) += msm_fbdev.o

obj-$(CONFIG_DRM_MSM)	+= msm.o
