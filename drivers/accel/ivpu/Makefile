# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2023-2024 Intel Corporation

intel_vpu-y := \
	ivpu_drv.o \
	ivpu_fw.o \
	ivpu_fw_log.o \
	ivpu_gem.o \
	ivpu_hw.o \
	ivpu_hw_btrs.o \
	ivpu_hw_ip.o \
	ivpu_ipc.o \
	ivpu_job.o \
	ivpu_jsm_msg.o \
	ivpu_mmu.o \
	ivpu_mmu_context.o \
	ivpu_ms.o \
	ivpu_pm.o \
	ivpu_sysfs.o \
	ivpu_trace_points.o

intel_vpu-$(CONFIG_DEBUG_FS) += ivpu_debugfs.o
intel_vpu-$(CONFIG_DEV_COREDUMP) += ivpu_coredump.o

obj-$(CONFIG_DRM_ACCEL_IVPU) += intel_vpu.o

subdir-ccflags-$(CONFIG_DRM_ACCEL_IVPU_DEBUG) += -DDEBUG

CFLAGS_ivpu_trace_points.o = -I$(src)
