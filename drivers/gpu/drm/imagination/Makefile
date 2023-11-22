# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (c) 2023 Imagination Technologies Ltd.

subdir-ccflags-y := -I$(srctree)/$(src)

powervr-y := \
	pvr_device.o \
	pvr_drv.o \

obj-$(CONFIG_DRM_POWERVR) += powervr.o
