#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
i915-y := i915_drv.o i915_dma.o i915_irq.o i915_mem.o i915_opregion.o \
          i915_suspend.o \
	  i915_gem.o \
	  i915_gem_debug.o \
	  i915_gem_proc.o \
	  i915_gem_tiling.o

i915-$(CONFIG_COMPAT)   += i915_ioc32.o

obj-$(CONFIG_DRM_I915)  += i915.o
