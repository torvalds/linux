#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
radeon-y := radeon_drv.o radeon_cp.o radeon_state.o radeon_mem.o \
	radeon_irq.o r300_cmdbuf.o r600_cp.o

radeon-$(CONFIG_DRM_RADEON_KMS) += radeon_device.o radeon_kms.o \
	radeon_atombios.o radeon_agp.o atombios_crtc.o radeon_combios.o \
	atom.o radeon_fence.o radeon_ttm.o radeon_object.o radeon_gart.o \
	radeon_legacy_crtc.o radeon_legacy_encoders.o radeon_connectors.o \
	radeon_encoders.o radeon_display.o radeon_cursor.o radeon_i2c.o \
	radeon_clocks.o radeon_fb.o radeon_gem.o radeon_ring.o radeon_irq_kms.o \
	radeon_cs.o radeon_bios.o radeon_benchmark.o r100.o r300.o r420.o \
	rs400.o rs600.o rs690.o rv515.o r520.o r600.o rs780.o rv770.o

radeon-$(CONFIG_COMPAT) += radeon_ioc32.o

obj-$(CONFIG_DRM_RADEON)+= radeon.o
