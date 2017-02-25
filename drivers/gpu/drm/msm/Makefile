ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/msm
ccflags-$(CONFIG_DRM_MSM_DSI) += -Idrivers/gpu/drm/msm/dsi

msm-y := \
	adreno/adreno_device.o \
	adreno/adreno_gpu.o \
	adreno/a3xx_gpu.o \
	adreno/a4xx_gpu.o \
	adreno/a5xx_gpu.o \
	adreno/a5xx_power.o \
	hdmi/hdmi.o \
	hdmi/hdmi_audio.o \
	hdmi/hdmi_bridge.o \
	hdmi/hdmi_connector.o \
	hdmi/hdmi_i2c.o \
	hdmi/hdmi_phy.o \
	hdmi/hdmi_phy_8960.o \
	hdmi/hdmi_phy_8x60.o \
	hdmi/hdmi_phy_8x74.o \
	edp/edp.o \
	edp/edp_aux.o \
	edp/edp_bridge.o \
	edp/edp_connector.o \
	edp/edp_ctrl.o \
	edp/edp_phy.o \
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
	mdp/mdp5/mdp5_mdss.o \
	mdp/mdp5/mdp5_kms.o \
	mdp/mdp5/mdp5_pipe.o \
	mdp/mdp5/mdp5_plane.o \
	mdp/mdp5/mdp5_smp.o \
	msm_atomic.o \
	msm_debugfs.o \
	msm_drv.o \
	msm_fb.o \
	msm_fence.o \
	msm_gem.o \
	msm_gem_prime.o \
	msm_gem_shrinker.o \
	msm_gem_submit.o \
	msm_gem_vma.o \
	msm_gpu.o \
	msm_iommu.o \
	msm_perf.o \
	msm_rd.o \
	msm_ringbuffer.o

msm-$(CONFIG_DRM_FBDEV_EMULATION) += msm_fbdev.o
msm-$(CONFIG_COMMON_CLK) += mdp/mdp4/mdp4_lvds_pll.o
msm-$(CONFIG_COMMON_CLK) += hdmi/hdmi_pll_8960.o
msm-$(CONFIG_COMMON_CLK) += hdmi/hdmi_phy_8996.o

msm-$(CONFIG_DRM_MSM_HDMI_HDCP) += hdmi/hdmi_hdcp.o

msm-$(CONFIG_DRM_MSM_DSI) += dsi/dsi.o \
			mdp/mdp4/mdp4_dsi_encoder.o \
			dsi/dsi_cfg.o \
			dsi/dsi_host.o \
			dsi/dsi_manager.o \
			dsi/phy/dsi_phy.o \
			mdp/mdp5/mdp5_cmd_encoder.o

msm-$(CONFIG_DRM_MSM_DSI_28NM_PHY) += dsi/phy/dsi_phy_28nm.o
msm-$(CONFIG_DRM_MSM_DSI_20NM_PHY) += dsi/phy/dsi_phy_20nm.o
msm-$(CONFIG_DRM_MSM_DSI_28NM_8960_PHY) += dsi/phy/dsi_phy_28nm_8960.o
msm-$(CONFIG_DRM_MSM_DSI_14NM_PHY) += dsi/phy/dsi_phy_14nm.o

ifeq ($(CONFIG_DRM_MSM_DSI_PLL),y)
msm-y += dsi/pll/dsi_pll.o
msm-$(CONFIG_DRM_MSM_DSI_28NM_PHY) += dsi/pll/dsi_pll_28nm.o
msm-$(CONFIG_DRM_MSM_DSI_28NM_8960_PHY) += dsi/pll/dsi_pll_28nm_8960.o
msm-$(CONFIG_DRM_MSM_DSI_14NM_PHY) += dsi/pll/dsi_pll_14nm.o
endif

obj-$(CONFIG_DRM_MSM)	+= msm.o
