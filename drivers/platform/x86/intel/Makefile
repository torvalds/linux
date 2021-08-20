# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/platform/x86/intel
# Intel x86 Platform-Specific Drivers
#

obj-$(CONFIG_INTEL_SAR_INT1092)		+= int1092/
obj-$(CONFIG_INTEL_CHT_INT33FE)		+= int33fe/
obj-$(CONFIG_INTEL_SKL_INT3472)		+= int3472/
obj-$(CONFIG_INTEL_PMT_CLASS)		+= pmt/

# Intel PMIC / PMC / P-Unit drivers
intel_bxtwc_tmu-y			:= bxtwc_tmu.o
obj-$(CONFIG_INTEL_BXTWC_PMIC_TMU)	+= intel_bxtwc_tmu.o
