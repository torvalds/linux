#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
i915-y := i915_drv.o i915_dma.o i915_irq.o i915_mem.o \
	  i915_debugfs.o \
          i915_suspend.o \
	  i915_gem.o \
	  i915_gem_debug.o \
	  i915_gem_tiling.o \
	  i915_trace_points.o \
	  intel_display.o \
	  intel_crt.o \
	  intel_lvds.o \
	  intel_bios.o \
	  intel_dp.o \
	  intel_dp_i2c.o \
	  intel_hdmi.o \
	  intel_sdvo.o \
	  intel_modes.o \
	  intel_i2c.o \
	  intel_fb.o \
	  intel_tv.o \
	  intel_dvo.o \
	  dvo_ch7xxx.o \
	  dvo_ch7017.o \
	  dvo_ivch.o \
	  dvo_tfp410.o \
	  dvo_sil164.o

i915-$(CONFIG_ACPI)	+= i915_opregion.o
i915-$(CONFIG_COMPAT)   += i915_ioc32.o

obj-$(CONFIG_DRM_I915)  += i915.o
