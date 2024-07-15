# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

# Unconditionally enable W=1 warnings locally
# --- begin copy-paste W=1 warnings from scripts/Makefile.extrawarn
subdir-ccflags-y += -Wextra -Wunused -Wno-unused-parameter
subdir-ccflags-y += -Wmissing-declarations
subdir-ccflags-y += $(call cc-option, -Wrestrict)
subdir-ccflags-y += -Wmissing-format-attribute
subdir-ccflags-y += -Wmissing-prototypes
subdir-ccflags-y += -Wold-style-definition
subdir-ccflags-y += -Wmissing-include-dirs
subdir-ccflags-y += $(call cc-option, -Wunused-but-set-variable)
subdir-ccflags-y += $(call cc-option, -Wunused-const-variable)
subdir-ccflags-y += $(call cc-option, -Wpacked-not-aligned)
subdir-ccflags-y += $(call cc-option, -Wformat-overflow)
subdir-ccflags-y += $(call cc-option, -Wformat-truncation)
subdir-ccflags-y += $(call cc-option, -Wstringop-truncation)
# The following turn off the warnings enabled by -Wextra
ifeq ($(findstring 2, $(KBUILD_EXTRA_WARN)),)
subdir-ccflags-y += -Wno-missing-field-initializers
subdir-ccflags-y += -Wno-type-limits
subdir-ccflags-y += -Wno-shift-negative-value
endif
ifeq ($(findstring 3, $(KBUILD_EXTRA_WARN)),)
subdir-ccflags-y += -Wno-sign-compare
endif
# --- end copy-paste

# Enable -Werror in CI and development
subdir-ccflags-$(CONFIG_DRM_XE_WERROR) += -Werror

subdir-ccflags-y += -I$(obj) -I$(srctree)/$(src)

# generated sources
hostprogs := xe_gen_wa_oob

generated_oob := $(obj)/generated/xe_wa_oob.c $(obj)/generated/xe_wa_oob.h

quiet_cmd_wa_oob = GEN     $(notdir $(generated_oob))
      cmd_wa_oob = mkdir -p $(@D); $^ $(generated_oob)

$(obj)/generated/%_wa_oob.c $(obj)/generated/%_wa_oob.h: $(obj)/xe_gen_wa_oob \
		 $(srctree)/$(src)/xe_wa_oob.rules
	$(call cmd,wa_oob)

uses_generated_oob := \
	$(obj)/xe_gsc.o \
	$(obj)/xe_guc.o \
	$(obj)/xe_migrate.o \
	$(obj)/xe_ring_ops.o \
	$(obj)/xe_vm.o \
	$(obj)/xe_wa.o \
	$(obj)/xe_ttm_stolen_mgr.o

$(uses_generated_oob): $(generated_oob)

# Please keep these build lists sorted!

# core driver code

xe-y += xe_bb.o \
	xe_bo.o \
	xe_bo_evict.o \
	xe_debugfs.o \
	xe_devcoredump.o \
	xe_device.o \
	xe_device_sysfs.o \
	xe_dma_buf.o \
	xe_drm_client.o \
	xe_exec.o \
	xe_execlist.o \
	xe_exec_queue.o \
	xe_force_wake.o \
	xe_ggtt.o \
	xe_gpu_scheduler.o \
	xe_gsc.o \
	xe_gsc_proxy.o \
	xe_gsc_submit.o \
	xe_gt.o \
	xe_gt_ccs_mode.o \
	xe_gt_clock.o \
	xe_gt_debugfs.o \
	xe_gt_freq.o \
	xe_gt_idle.o \
	xe_gt_mcr.o \
	xe_gt_pagefault.o \
	xe_gt_sysfs.o \
	xe_gt_throttle_sysfs.o \
	xe_gt_tlb_invalidation.o \
	xe_gt_topology.o \
	xe_guc.o \
	xe_guc_ads.o \
	xe_guc_ct.o \
	xe_guc_db_mgr.o \
	xe_guc_debugfs.o \
	xe_guc_hwconfig.o \
	xe_guc_log.o \
	xe_guc_pc.o \
	xe_guc_submit.o \
	xe_heci_gsc.o \
	xe_hw_engine.o \
	xe_hw_engine_class_sysfs.o \
	xe_hw_fence.o \
	xe_huc.o \
	xe_huc_debugfs.o \
	xe_irq.o \
	xe_lrc.o \
	xe_migrate.o \
	xe_mmio.o \
	xe_mocs.o \
	xe_module.o \
	xe_pat.o \
	xe_pci.o \
	xe_pcode.o \
	xe_pm.o \
	xe_preempt_fence.o \
	xe_pt.o \
	xe_pt_walk.o \
	xe_query.o \
	xe_range_fence.o \
	xe_reg_sr.o \
	xe_reg_whitelist.o \
	xe_rtp.o \
	xe_ring_ops.o \
	xe_sa.o \
	xe_sched_job.o \
	xe_step.o \
	xe_sync.o \
	xe_tile.o \
	xe_tile_sysfs.o \
	xe_trace.o \
	xe_ttm_sys_mgr.o \
	xe_ttm_stolen_mgr.o \
	xe_ttm_vram_mgr.o \
	xe_tuning.o \
	xe_uc.o \
	xe_uc_debugfs.o \
	xe_uc_fw.o \
	xe_vm.o \
	xe_vram_freq.o \
	xe_wait_user_fence.o \
	xe_wa.o \
	xe_wopcm.o

# graphics hardware monitoring (HWMON) support
xe-$(CONFIG_HWMON) += xe_hwmon.o

# graphics virtualization (SR-IOV) support
xe-y += \
	xe_guc_relay.o \
	xe_memirq.o \
	xe_sriov.o

xe-$(CONFIG_PCI_IOV) += \
	xe_lmtt.o \
	xe_lmtt_2l.o \
	xe_lmtt_ml.o

# include helpers for tests even when XE is built-in
ifdef CONFIG_DRM_XE_KUNIT_TEST
xe-y += tests/xe_kunit_helpers.o
endif

# i915 Display compat #defines and #includes
subdir-ccflags-$(CONFIG_DRM_XE_DISPLAY) += \
	-I$(srctree)/$(src)/display/ext \
	-I$(srctree)/$(src)/compat-i915-headers \
	-I$(srctree)/drivers/gpu/drm/i915/display/ \
	-Ddrm_i915_gem_object=xe_bo \
	-Ddrm_i915_private=xe_device

CFLAGS_i915-display/intel_fbdev.o = -Wno-override-init
CFLAGS_i915-display/intel_display_device.o = -Wno-override-init

# Rule to build SOC code shared with i915
$(obj)/i915-soc/%.o: $(srctree)/drivers/gpu/drm/i915/soc/%.c FORCE
	$(call cmd,force_checksrc)
	$(call if_changed_rule,cc_o_c)

# Rule to build display code shared with i915
$(obj)/i915-display/%.o: $(srctree)/drivers/gpu/drm/i915/display/%.c FORCE
	$(call cmd,force_checksrc)
	$(call if_changed_rule,cc_o_c)

# Display code specific to xe
xe-$(CONFIG_DRM_XE_DISPLAY) += \
	display/ext/i915_irq.o \
	display/ext/i915_utils.o \
	display/intel_fb_bo.o \
	display/intel_fbdev_fb.o \
	display/xe_display.o \
	display/xe_display_misc.o \
	display/xe_display_rps.o \
	display/xe_dsb_buffer.o \
	display/xe_fb_pin.o \
	display/xe_hdcp_gsc.o \
	display/xe_plane_initial.o

# SOC code shared with i915
xe-$(CONFIG_DRM_XE_DISPLAY) += \
	i915-soc/intel_dram.o \
	i915-soc/intel_pch.o

# Display code shared with i915
xe-$(CONFIG_DRM_XE_DISPLAY) += \
	i915-display/icl_dsi.o \
	i915-display/intel_atomic.o \
	i915-display/intel_atomic_plane.o \
	i915-display/intel_audio.o \
	i915-display/intel_backlight.o \
	i915-display/intel_bios.o \
	i915-display/intel_bw.o \
	i915-display/intel_cdclk.o \
	i915-display/intel_color.o \
	i915-display/intel_combo_phy.o \
	i915-display/intel_connector.o \
	i915-display/intel_crtc.o \
	i915-display/intel_crtc_state_dump.o \
	i915-display/intel_cursor.o \
	i915-display/intel_cx0_phy.o \
	i915-display/intel_ddi.o \
	i915-display/intel_ddi_buf_trans.o \
	i915-display/intel_display.o \
	i915-display/intel_display_device.o \
	i915-display/intel_display_driver.o \
	i915-display/intel_display_irq.o \
	i915-display/intel_display_params.o \
	i915-display/intel_display_power.o \
	i915-display/intel_display_power_map.o \
	i915-display/intel_display_power_well.o \
	i915-display/intel_display_trace.o \
	i915-display/intel_display_wa.o \
	i915-display/intel_dkl_phy.o \
	i915-display/intel_dmc.o \
	i915-display/intel_dp.o \
	i915-display/intel_dp_aux.o \
	i915-display/intel_dp_aux_backlight.o \
	i915-display/intel_dp_hdcp.o \
	i915-display/intel_dp_link_training.o \
	i915-display/intel_dp_mst.o \
	i915-display/intel_dpll.o \
	i915-display/intel_dpll_mgr.o \
	i915-display/intel_dpt_common.o \
	i915-display/intel_drrs.o \
	i915-display/intel_dsb.o \
	i915-display/intel_dsi.o \
	i915-display/intel_dsi_dcs_backlight.o \
	i915-display/intel_dsi_vbt.o \
	i915-display/intel_fb.o \
	i915-display/intel_fbc.o \
	i915-display/intel_fdi.o \
	i915-display/intel_fifo_underrun.o \
	i915-display/intel_frontbuffer.o \
	i915-display/intel_global_state.o \
	i915-display/intel_gmbus.o \
	i915-display/intel_hdcp.o \
	i915-display/intel_hdmi.o \
	i915-display/intel_hotplug.o \
	i915-display/intel_hotplug_irq.o \
	i915-display/intel_hti.o \
	i915-display/intel_link_bw.o \
	i915-display/intel_lspcon.o \
	i915-display/intel_modeset_lock.o \
	i915-display/intel_modeset_setup.o \
	i915-display/intel_modeset_verify.o \
	i915-display/intel_panel.o \
	i915-display/intel_pmdemand.o \
	i915-display/intel_pps.o \
	i915-display/intel_psr.o \
	i915-display/intel_qp_tables.o \
	i915-display/intel_quirks.o \
	i915-display/intel_snps_phy.o \
	i915-display/intel_tc.o \
	i915-display/intel_vblank.o \
	i915-display/intel_vdsc.o \
	i915-display/intel_vga.o \
	i915-display/intel_vrr.o \
	i915-display/intel_wm.o \
	i915-display/skl_scaler.o \
	i915-display/skl_universal_plane.o \
	i915-display/skl_watermark.o

ifeq ($(CONFIG_ACPI),y)
	xe-$(CONFIG_DRM_XE_DISPLAY) += \
		i915-display/intel_acpi.o \
		i915-display/intel_opregion.o
endif

ifeq ($(CONFIG_DRM_FBDEV_EMULATION),y)
	xe-$(CONFIG_DRM_XE_DISPLAY) += i915-display/intel_fbdev.o
endif

ifeq ($(CONFIG_DEBUG_FS),y)
	xe-$(CONFIG_DRM_XE_DISPLAY) += \
		i915-display/intel_display_debugfs.o \
		i915-display/intel_display_debugfs_params.o \
		i915-display/intel_pipe_crc.o
endif

obj-$(CONFIG_DRM_XE) += xe.o
obj-$(CONFIG_DRM_XE_KUNIT_TEST) += tests/

# header test
hdrtest_find_args := -not -path xe_rtp_helpers.h
ifneq ($(CONFIG_DRM_XE_DISPLAY),y)
	hdrtest_find_args += -not -path display/\* -not -path compat-i915-headers/\* -not -path xe_display.h
endif

always-$(CONFIG_DRM_XE_WERROR) += \
	$(patsubst %.h,%.hdrtest, $(shell cd $(srctree)/$(src) && find * -name '*.h' $(hdrtest_find_args)))

quiet_cmd_hdrtest = HDRTEST $(patsubst %.hdrtest,%.h,$@)
      cmd_hdrtest = $(CC) -DHDRTEST $(filter-out $(CFLAGS_GCOV), $(c_flags)) -S -o /dev/null -x c /dev/null -include $<; touch $@

$(obj)/%.hdrtest: $(src)/%.h FORCE
	$(call if_changed_dep,hdrtest)
