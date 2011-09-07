#
#	KMS driver for the GMA500
#
ccflags-y += -Iinclude/drm

psb_gfx-y += gem_glue.o \
	  accel_2d.o \
	  backlight.o \
	  framebuffer.o \
	  gem.o \
	  gtt.o \
	  intel_bios.o \
	  intel_i2c.o \
	  intel_opregion.o \
	  mmu.o \
	  power.o \
	  psb_drv.o \
	  psb_intel_display.o \
	  psb_intel_lvds.o \
	  psb_intel_modes.o \
	  psb_intel_sdvo.o \
	  psb_lid.o \
	  psb_irq.o \
	  psb_device.o \
	  mid_bios.o

psb_gfx-$(CONFIG_DRM_PSB_CDV) +=  cdv_device.o \
	  cdv_intel_crt.o \
	  cdv_intel_display.o \
	  cdv_intel_hdmi.o \
	  cdv_intel_lvds.o

psb_gfx-$(CONFIG_DRM_PSB_MRST) += mrst_device.o \
	  mrst_crtc.o \
	  mrst_lvds.o \
	  mrst_hdmi.o \
	  mrst_hdmi_i2c.o

psb_gfx-$(CONFIG_DRM_PSB_MFLD) += mdfld_device.o \
	  mdfld_output.o \
	  mdfld_pyr_cmd.o \
	  mdfld_tmd_vid.o \
	  mdfld_tpo_cmd.o \
	  mdfld_tpo_vid.o \
	  mdfld_dsi_pkg_sender.o \
	  mdfld_dsi_dpi.o \
	  mdfld_dsi_output.o \
	  mdfld_dsi_dbi.o \
	  mdfld_dsi_dbi_dpu.o \
	  mdfld_intel_display.o

obj-$(CONFIG_DRM_PSB) += psb_gfx.o
