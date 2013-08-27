#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
i915-y := i915_drv.o i915_dma.o i915_irq.o \
	  i915_debugfs.o \
	  i915_gpu_error.o \
          i915_suspend.o \
	  i915_gem.o \
	  i915_gem_context.o \
	  i915_gem_debug.o \
	  i915_gem_evict.o \
	  i915_gem_execbuffer.o \
	  i915_gem_gtt.o \
	  i915_gem_stolen.o \
	  i915_gem_tiling.o \
	  i915_sysfs.o \
	  i915_trace_points.o \
	  i915_ums.o \
	  intel_display.o \
	  intel_crt.o \
	  intel_lvds.o \
	  intel_dsi_cmd.o \
	  intel_bios.o \
	  intel_ddi.o \
	  intel_dp.o \
	  intel_hdmi.o \
	  intel_sdvo.o \
	  intel_modes.o \
	  intel_panel.o \
	  intel_pm.o \
	  intel_i2c.o \
	  intel_fb.o \
	  intel_tv.o \
	  intel_dvo.o \
	  intel_ringbuffer.o \
	  intel_overlay.o \
	  intel_sprite.o \
	  intel_opregion.o \
	  intel_sideband.o \
	  intel_uncore.o \
	  dvo_ch7xxx.o \
	  dvo_ch7017.o \
	  dvo_ivch.o \
	  dvo_tfp410.o \
	  dvo_sil164.o \
	  dvo_ns2501.o \
	  i915_gem_dmabuf.o

i915-$(CONFIG_COMPAT)   += i915_ioc32.o

i915-$(CONFIG_ACPI)	+= intel_acpi.o

obj-$(CONFIG_DRM_I915)  += i915.o

CFLAGS_i915_trace_points.o := -I$(src)
