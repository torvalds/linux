ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/msm

msm-y := \
	adreno/adreno_device.o \
	adreno/adreno_gpu.o \
	adreno/a3xx_gpu.o \
	adreno/a4xx_gpu.o \
	mdp/mdp_format.o \
	mdp/mdp_kms.o \
	mdp/mdp4/mdp4_crtc.o \
	mdp/mdp4/mdp4_dtv_encoder.o \
	mdp/mdp4/mdp4_lcdc_encoder.o \
	mdp/mdp4/mdp4_lvds_connector.o \
	mdp/mdp4/mdp4_irq.o \
	mdp/mdp4/mdp4_kms.o \
	mdp/mdp4/mdp4_plane.o \
	mdp/mdp5/mdp5_cfg.o \
	mdp/mdp5/mdp5_ctl.o \
	mdp/mdp5/mdp5_crtc.o \
	mdp/mdp5/mdp5_encoder.o \
	mdp/mdp5/mdp5_irq.o \
	mdp/mdp5/mdp5_kms.o \
	mdp/mdp5/mdp5_plane.o \
	mdp/mdp5/mdp5_smp.o \
	msm_atomic.o \
	msm_drv.o \
	msm_fb.o \
	msm_gem.o \
	msm_gem_prime.o \
	msm_gem_submit.o \
	msm_gpu.o \
	msm_iommu.o \
	msm_perf.o \
	msm_rd.o \
	msm_ringbuffer.o

msm-$(CONFIG_DRM_MSM_DSI) += mdp/mdp4/mdp4_dsi_encoder.o \
			     mdp/mdp5/mdp5_cmd_encoder.o

msm-$(CONFIG_DRM_FBDEV_EMULATION) += msm_fbdev.o
msm-$(CONFIG_COMMON_CLK) += mdp/mdp4/mdp4_lvds_pll.o

obj-$(CONFIG_DRM_MSM_DSI) += dsi/
obj-$(CONFIG_DRM_MSM_HDMI) += hdmi/
obj-$(CONFIG_DRM_MSM_EDP) += edp/

obj-$(CONFIG_DRM_MSM)	+= msm.o
