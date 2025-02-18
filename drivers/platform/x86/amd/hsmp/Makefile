# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/platform/x86/amd/hsmp
# AMD HSMP Driver
#

obj-$(CONFIG_AMD_HSMP)			+= hsmp_common.o
hsmp_common-y				:= hsmp.o
obj-$(CONFIG_AMD_HSMP_PLAT)		+= amd_hsmp.o
amd_hsmp-y				:= plat.o
obj-$(CONFIG_AMD_HSMP_ACPI)		+= hsmp_acpi.o
hsmp_acpi-y				:= acpi.o
