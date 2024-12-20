# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I $(src)
ccflags-y += -I $(obj)/generated
ccflags-y += -I $(src)/disp/dpu1
ccflags-$(CONFIG_DRM_MSM_DSI) += -I $(src)/dsi
ccflags-$(CONFIG_DRM_MSM_DP) += -I $(src)/dp

adreno-y := \
	adreno/adreno_device.o \
	adreno/adreno_gpu.o \
	adreno/a2xx_catalog.o \
	adreno/a2xx_gpu.o \
	adreno/a2xx_gpummu.o \
	adreno/a3xx_catalog.o \
	adreno/a3xx_gpu.o \
	adreno/a4xx_catalog.o \
	adreno/a4xx_gpu.o \
	adreno/a5xx_catalog.o \
	adreno/a5xx_gpu.o \
	adreno/a5xx_power.o \
	adreno/a5xx_preempt.o \
	adreno/a6xx_catalog.o \
	adreno/a6xx_gpu.o \
	adreno/a6xx_gmu.o \
	adreno/a6xx_hfi.o \
	adreno/a6xx_preempt.o \

adreno-$(CONFIG_DEBUG_FS) += adreno/a5xx_debugfs.o \

adreno-$(CONFIG_DRM_MSM_GPU_STATE)	+= adreno/a6xx_gpu_state.o

msm-display-$(CONFIG_DRM_MSM_HDMI) += \
	hdmi/hdmi.o \
	hdmi/hdmi_audio.o \
	hdmi/hdmi_bridge.o \
	hdmi/hdmi_hpd.o \
	hdmi/hdmi_i2c.o \
	hdmi/hdmi_phy.o \
	hdmi/hdmi_phy_8960.o \
	hdmi/hdmi_phy_8996.o \
	hdmi/hdmi_phy_8998.o \
	hdmi/hdmi_phy_8x60.o \
	hdmi/hdmi_phy_8x74.o \
	hdmi/hdmi_pll_8960.o \

msm-display-$(CONFIG_DRM_MSM_MDP4) += \
	disp/mdp4/mdp4_crtc.o \
	disp/mdp4/mdp4_dsi_encoder.o \
	disp/mdp4/mdp4_dtv_encoder.o \
	disp/mdp4/mdp4_lcdc_encoder.o \
	disp/mdp4/mdp4_lvds_connector.o \
	disp/mdp4/mdp4_lvds_pll.o \
	disp/mdp4/mdp4_irq.o \
	disp/mdp4/mdp4_kms.o \
	disp/mdp4/mdp4_plane.o \

msm-display-$(CONFIG_DRM_MSM_MDP5) += \
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

msm-display-$(CONFIG_DRM_MSM_DPU) += \
	disp/dpu1/dpu_core_perf.o \
	disp/dpu1/dpu_crtc.o \
	disp/dpu1/dpu_encoder.o \
	disp/dpu1/dpu_encoder_phys_cmd.o \
	disp/dpu1/dpu_encoder_phys_vid.o \
	disp/dpu1/dpu_encoder_phys_wb.o \
	disp/dpu1/dpu_formats.o \
	disp/dpu1/dpu_hw_catalog.o \
	disp/dpu1/dpu_hw_cdm.o \
	disp/dpu1/dpu_hw_ctl.o \
	disp/dpu1/dpu_hw_dsc.o \
	disp/dpu1/dpu_hw_dsc_1_2.o \
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

msm-display-$(CONFIG_DRM_MSM_MDSS) += \
	msm_mdss.o \

msm-display-y += \
	disp/mdp_format.o \
	disp/mdp_kms.o \
	disp/msm_disp_snapshot.o \
	disp/msm_disp_snapshot_util.o \

msm-y += \
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
	msm_kms.o \
	msm_perf.o \
	msm_rd.o \
	msm_ringbuffer.o \
	msm_submitqueue.o \
	msm_gpu_tracepoints.o \

msm-$(CONFIG_DRM_FBDEV_EMULATION) += msm_fbdev.o

msm-display-$(CONFIG_DEBUG_FS) += \
	dp/dp_debug.o

msm-display-$(CONFIG_DRM_MSM_DP)+= dp/dp_aux.o \
	dp/dp_catalog.o \
	dp/dp_ctrl.o \
	dp/dp_display.o \
	dp/dp_drm.o \
	dp/dp_link.o \
	dp/dp_panel.o \
	dp/dp_audio.o \
	dp/dp_utils.o

msm-display-$(CONFIG_DRM_MSM_HDMI_HDCP) += hdmi/hdmi_hdcp.o

msm-display-$(CONFIG_DRM_MSM_DSI) += dsi/dsi.o \
			dsi/dsi_cfg.o \
			dsi/dsi_host.o \
			dsi/dsi_manager.o \
			dsi/phy/dsi_phy.o

msm-display-$(CONFIG_DRM_MSM_DSI_28NM_PHY) += dsi/phy/dsi_phy_28nm.o
msm-display-$(CONFIG_DRM_MSM_DSI_20NM_PHY) += dsi/phy/dsi_phy_20nm.o
msm-display-$(CONFIG_DRM_MSM_DSI_28NM_8960_PHY) += dsi/phy/dsi_phy_28nm_8960.o
msm-display-$(CONFIG_DRM_MSM_DSI_14NM_PHY) += dsi/phy/dsi_phy_14nm.o
msm-display-$(CONFIG_DRM_MSM_DSI_10NM_PHY) += dsi/phy/dsi_phy_10nm.o
msm-display-$(CONFIG_DRM_MSM_DSI_7NM_PHY) += dsi/phy/dsi_phy_7nm.o

msm-y += $(adreno-y) $(msm-display-y)

obj-$(CONFIG_DRM_MSM)	+= msm.o

ifeq (y,$(CONFIG_DRM_MSM_VALIDATE_XML))
	headergen-opts += --validate
else
	headergen-opts += --no-validate
endif

quiet_cmd_headergen = GENHDR  $@
      cmd_headergen = mkdir -p $(obj)/generated && $(PYTHON3) $(src)/registers/gen_header.py \
		      $(headergen-opts) --rnn $(src)/registers --xml $< c-defines > $@

$(obj)/generated/%.xml.h: $(src)/registers/adreno/%.xml \
		$(src)/registers/adreno/adreno_common.xml \
		$(src)/registers/adreno/adreno_pm4.xml \
		$(src)/registers/freedreno_copyright.xml \
		$(src)/registers/gen_header.py \
		$(src)/registers/rules-fd.xsd \
		FORCE
	$(call if_changed,headergen)

$(obj)/generated/%.xml.h: $(src)/registers/display/%.xml \
		$(src)/registers/freedreno_copyright.xml \
		$(src)/registers/gen_header.py \
		$(src)/registers/rules-fd.xsd \
		FORCE
	$(call if_changed,headergen)

ADRENO_HEADERS = \
	generated/a2xx.xml.h \
	generated/a3xx.xml.h \
	generated/a4xx.xml.h \
	generated/a5xx.xml.h \
	generated/a6xx.xml.h \
	generated/a6xx_gmu.xml.h \
	generated/adreno_common.xml.h \
	generated/adreno_pm4.xml.h \

DISPLAY_HEADERS = \
	generated/dsi_phy_7nm.xml.h \
	generated/dsi_phy_10nm.xml.h \
	generated/dsi_phy_14nm.xml.h \
	generated/dsi_phy_20nm.xml.h \
	generated/dsi_phy_28nm_8960.xml.h \
	generated/dsi_phy_28nm.xml.h \
	generated/dsi.xml.h \
	generated/hdmi.xml.h \
	generated/mdp4.xml.h \
	generated/mdp5.xml.h \
	generated/mdp_common.xml.h \
	generated/mdss.xml.h \
	generated/sfpb.xml.h

$(addprefix $(obj)/,$(adreno-y)): $(addprefix $(obj)/,$(ADRENO_HEADERS))
$(addprefix $(obj)/,$(msm-display-y)): $(addprefix $(obj)/,$(DISPLAY_HEADERS))

targets += $(ADRENO_HEADERS) $(DISPLAY_HEADERS)
