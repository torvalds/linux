# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform/x86/amd/pmf
# AMD Platform Management Framework
#

obj-$(CONFIG_AMD_PMF)		+= amd-pmf.o
amd-pmf-y 			:= core.o acpi.o sps.o \
				   auto-mode.o cnqf.o \
				   tee-if.o spc.o metrics.o
# Build util.c only when AMD_PMF_UTIL_SUPPORT is enabled
amd-pmf-$(CONFIG_AMD_PMF_UTIL_SUPPORT) += util.o
