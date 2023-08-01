# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

# Add a set of useful warning flags and enable -Werror for CI to prevent
# trivial mistakes from creeping in. We have to do this piecemeal as we reject
# any patch that isn't warning clean, so turning on -Wall -Wextra (or W=1) we
# need to filter out dubious warnings.  Still it is our interest
# to keep running locally with W=1 C=1 until we are completely clean.
#
# Note the danger in using -Wall -Wextra is that when CI updates gcc we
# will most likely get a sudden build breakage... Hopefully we will fix
# new warnings before CI updates!
subdir-ccflags-y := -Wall -Wextra
subdir-ccflags-y += $(call cc-disable-warning, unused-parameter)
subdir-ccflags-y += $(call cc-disable-warning, type-limits)
subdir-ccflags-y += $(call cc-disable-warning, missing-field-initializers)
subdir-ccflags-y += $(call cc-disable-warning, unused-but-set-variable)
# clang warnings
subdir-ccflags-y += $(call cc-disable-warning, sign-compare)
subdir-ccflags-y += $(call cc-disable-warning, sometimes-uninitialized)
subdir-ccflags-y += $(call cc-disable-warning, initializer-overrides)
subdir-ccflags-y += $(call cc-disable-warning, frame-address)
subdir-ccflags-$(CONFIG_DRM_XE_WERROR) += -Werror

# Fine grained warnings disable
CFLAGS_xe_pci.o = $(call cc-disable-warning, override-init)

subdir-ccflags-y += -I$(obj) -I$(srctree)/$(src)

# generated sources
hostprogs := xe_gen_wa_oob

generated_oob := $(obj)/generated/xe_wa_oob.c $(obj)/generated/xe_wa_oob.h

quiet_cmd_wa_oob = GEN     $(notdir $(generated_oob))
      cmd_wa_oob = mkdir -p $(@D); $^ $(generated_oob)

$(generated_oob) &: $(obj)/xe_gen_wa_oob $(srctree)/$(src)/xe_wa_oob.rules
	$(call cmd,wa_oob)

$(obj)/xe_guc.o $(obj)/xe_wa.o $(obj)/xe_ring_ops.o: $(generated_oob)

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
	xe_exec.o \
	xe_execlist.o \
	xe_exec_queue.o \
	xe_force_wake.o \
	xe_ggtt.o \
	xe_gpu_scheduler.o \
	xe_gt.o \
	xe_gt_clock.o \
	xe_gt_debugfs.o \
	xe_gt_idle_sysfs.o \
	xe_gt_mcr.o \
	xe_gt_pagefault.o \
	xe_gt_sysfs.o \
	xe_gt_tlb_invalidation.o \
	xe_gt_topology.o \
	xe_guc.o \
	xe_guc_ads.o \
	xe_guc_ct.o \
	xe_guc_debugfs.o \
	xe_guc_hwconfig.o \
	xe_guc_log.o \
	xe_guc_pc.o \
	xe_guc_submit.o \
	xe_hw_engine.o \
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
	xe_vm_madvise.o \
	xe_wait_user_fence.o \
	xe_wa.o \
	xe_wopcm.o

obj-$(CONFIG_DRM_XE) += xe.o
obj-$(CONFIG_DRM_XE_KUNIT_TEST) += tests/

# header test
hdrtest_find_args := -not -path xe_rtp_helpers.h

always-$(CONFIG_DRM_XE_WERROR) += \
	$(patsubst %.h,%.hdrtest, $(shell cd $(srctree)/$(src) && find * -name '*.h' $(hdrtest_find_args)))

quiet_cmd_hdrtest = HDRTEST $(patsubst %.hdrtest,%.h,$@)
      cmd_hdrtest = $(CC) -DHDRTEST $(filter-out $(CFLAGS_GCOV), $(c_flags)) -S -o /dev/null -x c /dev/null -include $<; touch $@

$(obj)/%.hdrtest: $(src)/%.h FORCE
	$(call if_changed_dep,hdrtest)
