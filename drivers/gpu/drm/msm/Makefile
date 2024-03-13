# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I $(srctree)/$(src)
ccflags-y += -I $(srctree)/$(src)/disp/dpu1
ccflags-$(CONFIG_DRM_MSM_DSI) += -I $(srctree)/$(src)/dsi
ccflags-$(CONFIG_DRM_MSM_DP) += -I $(srctree)/$(src)/dp

msm-y := \
	adreno/adreno_device.o \
	adreno/adreno_gpu.o \
	adreno/a2xx_gpu.o \
	adreno/a3xx_gpu.o \
	adreno/a4xx_gpu.o \
	adreno/a5xx_gpu.o \
	adreno/a5xx_power.o \
	adreno/a5xx_preempt.o \
	adreno/a6xx_gpu.o \
	adreno/a6xx_gmu.o \
	adreno/a6xx_hfi.o \

msm-$(CONFIG_DRM_MSM_HDMI) += \
	hdmi/hdmi.o \
	hdmi/hdmi_audio.o \
	hdmi/hdmi_bridge.o \
	hdmi/hdmi_hpd.o \
	hdmi/hdmi_i2c.o \
	hdmi/hdmi_phy.o \
	hdmi/hdmi_phy_8960.o \
	hdmi/hdmi_phy_8996.o \
	hdmi/hdmi_phy_8x60.o \
	hdmi/hdmi_phy_8x74.o \
	hdmi/hdmi_pll_8960.o \

msm-$(CONFIG_DRM_MSM_MDP4) += \
	disp/mdp4/mdp4_crtc.o \
	disp/mdp4/mdp4_dsi_encoder.o \
	disp/mdp4/mdp4_dtv_encoder.o \
	disp/mdp4/mdp4_lcdc_encoder.o \
	disp/mdp4/mdp4_lvds_connector.o \
	disp/mdp4/mdp4_lvds_pll.o \
	disp/mdp4/mdp4_irq.o \
	disp/mdp4/mdp4_kms.o \
	disp/mdp4/mdp4_plane.o \

msm-$(CONFIG_DRM_MSM_MDP5) += \
	disp/mdp5/mdp5_cfg.o \
	disp/mdp5/mdp5_cmd_encoder.o \
	disp/mdp5/mdp5_ctl.o \
	disp/mdp5/mdp5_crtc.o \
	disp/mdp5/mdp5_encoder.o \
	disp/mdp5/mdp5_irq.o \
	disp/mdp5/mdp5_kms.o \
	disp/mdp5/mdp5_pipe.o \
	disp/mdp5/mdp5_mixer.o \
	disp/mdp5/mdp5_plane.o \
	disp/mdp5/mdp5_smp.o \

msm-$(CONFIG_DRM_MSM_DPU) += \
	disp/dpu1/dpu_core_perf.o \
	disp/dpu1/dpu_crtc.o \
	disp/dpu1/dpu_encoder.o \
	disp/dpu1/dpu_encoder_phys_cmd.o \
	disp/dpu1/dpu_encoder_phys_vid.o \
	disp/dpu1/dpu_encoder_phys_wb.o \
	disp/dpu1/dpu_formats.o \
	disp/dpu1/dpu_hw_catalog.o \
	disp/dpu1/dpu_hw_ctl.o \
	disp/dpu1/dpu_hw_dsc.o \
	disp/dpu1/dpu_hw_interrupts.o \
	disp/dpu1/dpu_hw_intf.o \
	disp/dpu1/dpu_hw_lm.o \
	disp/dpu1/dpu_hw_pingpong.o \
	disp/dpu1/dpu_hw_sspp.o \
	disp/dpu1/dpu_hw_dspp.o \
	disp/dpu1/dpu_hw_merge3d.o \
	disp/dpu1/dpu_hw_top.o \
	disp/dpu1/dpu_hw_util.o \
	disp/dpu1/dpu_hw_vbif.o \
	disp/dpu1/dpu_hw_wb.o \
	disp/dpu1/dpu_kms.o \
	disp/dpu1/dpu_plane.o \
	disp/dpu1/dpu_rm.o \
	disp/dpu1/dpu_vbif.o \
	disp/dpu1/dpu_writeback.o

msm-$(CONFIG_DRM_MSM_MDSS) += \
	msm_mdss.o \

msm-y += \
	disp/mdp_format.o \
	disp/mdp_kms.o \
	disp/msm_disp_snapshot.o \
	disp/msm_disp_snapshot_util.o \
	msm_atomic.o \
	msm_atomic_tracepoints.o \
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
	msm_gpu_devfreq.o \
	msm_io_utils.o \
	msm_iommu.o \
	msm_perf.o \
	msm_rd.o \
	msm_ringbuffer.o \
	msm_submitqueue.o \
	msm_gpu_tracepoints.o \
	msm_gpummu.o

msm-$(CONFIG_DEBUG_FS) += adreno/a5xx_debugfs.o \
	dp/dp_debug.o

msm-$(CONFIG_DRM_MSM_GPU_STATE)	+= adreno/a6xx_gpu_state.o

msm-$(CONFIG_DRM_MSM_DP)+= dp/dp_aux.o \
	dp/dp_catalog.o \
	dp/dp_ctrl.o \
	dp/dp_display.o \
	dp/dp_drm.o \
	dp/dp_hpd.o \
	dp/dp_link.o \
	dp/dp_panel.o \
	dp/dp_parser.o \
	dp/dp_power.o \
	dp/dp_audio.o

msm-$(CONFIG_DRM_FBDEV_EMULATION) += msm_fbdev.o

msm-$(CONFIG_DRM_MSM_HDMI_HDCP) += hdmi/hdmi_hdcp.o

msm-$(CONFIG_DRM_MSM_DSI) += dsi/dsi.o \
			dsi/dsi_cfg.o \
			dsi/dsi_host.o \
			dsi/dsi_manager.o \
			dsi/phy/dsi_phy.o

msm-$(CONFIG_DRM_MSM_DSI_28NM_PHY) += dsi/phy/dsi_phy_28nm.o
msm-$(CONFIG_DRM_MSM_DSI_20NM_PHY) += dsi/phy/dsi_phy_20nm.o
msm-$(CONFIG_DRM_MSM_DSI_28NM_8960_PHY) += dsi/phy/dsi_phy_28nm_8960.o
msm-$(CONFIG_DRM_MSM_DSI_14NM_PHY) += dsi/phy/dsi_phy_14nm.o
msm-$(CONFIG_DRM_MSM_DSI_10NM_PHY) += dsi/phy/dsi_phy_10nm.o
msm-$(CONFIG_DRM_MSM_DSI_7NM_PHY) += dsi/phy/dsi_phy_7nm.o

obj-$(CONFIG_DRM_MSM)	+= msm.o
