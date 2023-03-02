# SPDX-License-Identifier: ISC
#
# Copyright (c) 2022 Broadcom Corporation

ccflags-y += \
	-I $(srctree)/$(src) \
	-I $(srctree)/$(src)/.. \
	-I $(srctree)/$(src)/../../include

obj-m += brcmfmac-wcc.o
brcmfmac-wcc-objs += \
		core.o module.o
