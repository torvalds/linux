# SPDX-License-Identifier: GPL-2.0
#
#	KMS driver for the GMA500
#

gma500_gfx-y += \
	  backlight.o \
	  cdv_device.o \
	  cdv_intel_crt.o \
	  cdv_intel_display.o \
	  cdv_intel_dp.o \
	  cdv_intel_hdmi.o \
	  cdv_intel_lvds.o \
	  framebuffer.o \
	  gem.o \
	  gma_device.o \
	  gma_display.o \
	  gtt.o \
	  intel_bios.o \
	  intel_gmbus.o \
	  intel_i2c.o \
	  mid_bios.o \
	  mmu.o \
	  oaktrail_device.o \
	  oaktrail_crtc.o \
	  oaktrail_hdmi.o \
	  oaktrail_hdmi_i2c.o \
	  oaktrail_lvds.o \
	  oaktrail_lvds_i2c.o \
	  power.o \
	  psb_device.o \
	  psb_drv.o \
	  psb_intel_display.o \
	  psb_intel_lvds.o \
	  psb_intel_modes.o \
	  psb_intel_sdvo.o \
	  psb_irq.o

gma500_gfx-$(CONFIG_ACPI) +=  opregion.o
gma500_gfx-$(CONFIG_DRM_FBDEV_EMULATION) +=  fbdev.o

obj-$(CONFIG_DRM_GMA500) += gma500_gfx.o
