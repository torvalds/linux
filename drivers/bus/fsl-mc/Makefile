# SPDX-License-Identifier: GPL-2.0
#
# Freescale Management Complex (MC) bus drivers
#
# Copyright (C) 2014 Freescale Semiconductor, Inc.
#
obj-$(CONFIG_FSL_MC_BUS) += mc-bus-driver.o

mc-bus-driver-objs := fsl-mc-bus.o \
		      mc-sys.o \
		      mc-io.o \
		      dpbp.o \
		      dpcon.o \
		      dprc.o \
		      dprc-driver.o \
		      fsl-mc-allocator.o \
		      fsl-mc-msi.o \
		      dpmcp.o \
		      obj-api.o

# MC userspace support
obj-$(CONFIG_FSL_MC_UAPI_SUPPORT) += fsl-mc-uapi.o
