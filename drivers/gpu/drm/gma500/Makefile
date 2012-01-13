#
#	KMS driver for the GMA500
#
ccflags-y += -Iinclude/drm

gma500_gfx-y += gem_glue.o \
	  accel_2d.o \
	  backlight.o \
	  framebuffer.o \
	  gem.o \
	  gtt.o \
	  intel_bios.o \
	  intel_i2c.o \
	  intel_gmbus.o \
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

gma500_gfx-$(CONFIG_DRM_GMA3600) +=  cdv_device.o \
	  cdv_intel_crt.o \
	  cdv_intel_display.o \
	  cdv_intel_hdmi.o \
	  cdv_intel_lvds.o

gma500_gfx-$(CONFIG_DRM_GMA600) += oaktrail_device.o \
	  oaktrail_crtc.o \
	  oaktrail_lvds.o \
	  oaktrail_hdmi.o \
	  oaktrail_hdmi_i2c.o

obj-$(CONFIG_DRM_GMA500) += gma500_gfx.o
