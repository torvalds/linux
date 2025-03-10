# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (c) 2023 Imagination Technologies Ltd.

powervr-y := \
	pvr_ccb.o \
	pvr_cccb.o \
	pvr_context.o \
	pvr_device.o \
	pvr_device_info.o \
	pvr_drv.o \
	pvr_free_list.o \
	pvr_fw.o \
	pvr_fw_meta.o \
	pvr_fw_mips.o \
	pvr_fw_startstop.o \
	pvr_fw_trace.o \
	pvr_gem.o \
	pvr_hwrt.o \
	pvr_job.o \
	pvr_mmu.o \
	pvr_params.o \
	pvr_power.o \
	pvr_queue.o \
	pvr_stream.o \
	pvr_stream_defs.o \
	pvr_sync.o \
	pvr_vm.o \
	pvr_vm_mips.o

powervr-$(CONFIG_DEBUG_FS) += \
	pvr_debugfs.o

obj-$(CONFIG_DRM_POWERVR) += powervr.o
