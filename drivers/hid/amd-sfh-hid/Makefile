# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - AMD SFH HID drivers
# Copyright (c) 2019-2020, Advanced Micro Devices, Inc.
#
#
obj-$(CONFIG_AMD_SFH_HID) += amd_sfh.o
amd_sfh-objs := amd_sfh_hid.o
amd_sfh-objs += amd_sfh_client.o
amd_sfh-objs += amd_sfh_pcie.o
amd_sfh-objs += hid_descriptor/amd_sfh_hid_desc.o
amd_sfh-objs += sfh1_1/amd_sfh_init.o
amd_sfh-objs += sfh1_1/amd_sfh_interface.o
amd_sfh-objs += sfh1_1/amd_sfh_desc.o

ccflags-y += -I $(srctree)/$(src)/
