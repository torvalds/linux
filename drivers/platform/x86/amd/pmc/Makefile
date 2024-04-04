# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform/x86/amd/pmc
# AMD Power Management Controller Driver
#

amd-pmc-objs := pmc.o pmc-quirks.o
obj-$(CONFIG_AMD_PMC) += amd-pmc.o
amd-pmc-$(CONFIG_AMD_MP2_STB) += mp2_stb.o
