# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (c) 2023 Imagination Technologies Ltd.

subdir-ccflags-y := -I$(srctree)/$(src)

powervr-y := \
	pvr_ccb.o \
	pvr_device.o \
	pvr_device_info.o \
	pvr_drv.o \
	pvr_fw.o \
	pvr_fw_meta.o \
	pvr_fw_startstop.o \
	pvr_fw_trace.o \
	pvr_gem.o \
	pvr_mmu.o \
	pvr_power.o \
	pvr_vm.o

obj-$(CONFIG_DRM_POWERVR) += powervr.o
