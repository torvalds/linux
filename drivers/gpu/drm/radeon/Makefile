#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

hostprogs-y := mkregtable
clean-files := rn50_reg_safe.h r100_reg_safe.h r200_reg_safe.h rv515_reg_safe.h r300_reg_safe.h r420_reg_safe.h rs600_reg_safe.h r600_reg_safe.h evergreen_reg_safe.h cayman_reg_safe.h

quiet_cmd_mkregtable = MKREGTABLE $@
      cmd_mkregtable = $(obj)/mkregtable $< > $@

$(obj)/rn50_reg_safe.h: $(src)/reg_srcs/rn50 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/r100_reg_safe.h: $(src)/reg_srcs/r100 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/r200_reg_safe.h: $(src)/reg_srcs/r200 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/rv515_reg_safe.h: $(src)/reg_srcs/rv515 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/r300_reg_safe.h: $(src)/reg_srcs/r300 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/r420_reg_safe.h: $(src)/reg_srcs/r420 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/rs600_reg_safe.h: $(src)/reg_srcs/rs600 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/r600_reg_safe.h: $(src)/reg_srcs/r600 $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/evergreen_reg_safe.h: $(src)/reg_srcs/evergreen $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/cayman_reg_safe.h: $(src)/reg_srcs/cayman $(obj)/mkregtable
	$(call if_changed,mkregtable)

$(obj)/r100.o: $(obj)/r100_reg_safe.h $(obj)/rn50_reg_safe.h

$(obj)/r200.o: $(obj)/r200_reg_safe.h

$(obj)/rv515.o: $(obj)/rv515_reg_safe.h

$(obj)/r300.o: $(obj)/r300_reg_safe.h

$(obj)/r420.o: $(obj)/r420_reg_safe.h

$(obj)/rs600.o: $(obj)/rs600_reg_safe.h

$(obj)/r600_cs.o: $(obj)/r600_reg_safe.h

$(obj)/evergreen_cs.o: $(obj)/evergreen_reg_safe.h $(obj)/cayman_reg_safe.h

radeon-y := radeon_drv.o

# add UMS driver
radeon-$(CONFIG_DRM_RADEON_UMS)+= radeon_cp.o radeon_state.o radeon_mem.o \
	radeon_irq.o r300_cmdbuf.o r600_cp.o r600_blit.o

# add KMS driver
radeon-y += radeon_device.o radeon_asic.o radeon_kms.o \
	radeon_atombios.o radeon_agp.o atombios_crtc.o radeon_combios.o \
	atom.o radeon_fence.o radeon_ttm.o radeon_object.o radeon_gart.o \
	radeon_legacy_crtc.o radeon_legacy_encoders.o radeon_connectors.o \
	radeon_encoders.o radeon_display.o radeon_cursor.o radeon_i2c.o \
	radeon_clocks.o radeon_fb.o radeon_gem.o radeon_ring.o radeon_irq_kms.o \
	radeon_cs.o radeon_bios.o radeon_benchmark.o r100.o r300.o r420.o \
	rs400.o rs600.o rs690.o rv515.o r520.o r600.o rv770.o radeon_test.o \
	r200.o radeon_legacy_tv.o r600_cs.o r600_blit_shaders.o \
	radeon_pm.o atombios_dp.o r600_audio.o r600_hdmi.o \
	evergreen.o evergreen_cs.o evergreen_blit_shaders.o \
	evergreen_hdmi.o radeon_trace_points.o ni.o cayman_blit_shaders.o \
	atombios_encoders.o radeon_semaphore.o radeon_sa.o atombios_i2c.o si.o \
	si_blit_shaders.o radeon_prime.o radeon_uvd.o cik.o cik_blit_shaders.o \
	r600_dpm.o rs780_dpm.o rv6xx_dpm.o rv770_dpm.o rv730_dpm.o rv740_dpm.o \
	rv770_smc.o cypress_dpm.o btc_dpm.o sumo_dpm.o sumo_smc.o trinity_dpm.o \
	trinity_smc.o ni_dpm.o si_smc.o si_dpm.o kv_smc.o kv_dpm.o ci_smc.o \
	ci_dpm.o

# add UVD block
radeon-y += \
	radeon_uvd.o \
	uvd_v1_0.o \
	uvd_v2_2.o \
	uvd_v3_1.o \
	uvd_v4_2.o

radeon-$(CONFIG_COMPAT) += radeon_ioc32.o
radeon-$(CONFIG_VGA_SWITCHEROO) += radeon_atpx_handler.o
radeon-$(CONFIG_ACPI) += radeon_acpi.o

obj-$(CONFIG_DRM_RADEON)+= radeon.o

CFLAGS_radeon_trace_points.o := -I$(src)
