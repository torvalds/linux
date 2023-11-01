# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2023 Intel Corporation

intel_vpu-y := \
	ivpu_drv.o \
	ivpu_fw.o \
	ivpu_fw_log.o \
	ivpu_gem.o \
	ivpu_hw_37xx.o \
	ivpu_hw_40xx.o \
	ivpu_ipc.o \
	ivpu_job.o \
	ivpu_jsm_msg.o \
	ivpu_mmu.o \
	ivpu_mmu_context.o \
	ivpu_pm.o

intel_vpu-$(CONFIG_DEBUG_FS) += ivpu_debugfs.o

obj-$(CONFIG_DRM_ACCEL_IVPU) += intel_vpu.o
