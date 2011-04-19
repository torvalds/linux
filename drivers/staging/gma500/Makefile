#
#	KMS driver for the GMA500
#
ccflags-y += -Iinclude/drm

psb_gfx-y += psb_bl.o \
	  psb_drv.o \
	  psb_gem.o \
	  psb_fb.o \
	  psb_2d.o \
	  psb_gtt.o \
	  psb_intel_bios.o \
	  psb_intel_opregion.o \
	  psb_intel_display.o \
	  psb_intel_i2c.o \
	  psb_intel_lvds.o \
	  psb_intel_modes.o \
	  psb_intel_sdvo.o \
	  psb_lid.o \
	  psb_mmu.o \
	  psb_powermgmt.o \
	  psb_irq.o \
	  mrst_crtc.o \
	  mrst_lvds.o

obj-$(CONFIG_DRM_PSB) += psb_gfx.o
