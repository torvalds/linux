# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/platform/x86/intel
# Intel x86 Platform-Specific Drivers
#

obj-$(CONFIG_INTEL_SAR_INT1092)		+= int1092/
obj-$(CONFIG_INTEL_CHT_INT33FE)		+= int33fe/
obj-$(CONFIG_INTEL_SKL_INT3472)		+= int3472/
obj-$(CONFIG_INTEL_PMC_CORE)		+= pmc/
obj-$(CONFIG_INTEL_PMT_CLASS)		+= pmt/
obj-$(CONFIG_INTEL_TELEMETRY)		+= telemetry/

# Intel PMIC / PMC / P-Unit drivers
intel_bxtwc_tmu-y			:= bxtwc_tmu.o
obj-$(CONFIG_INTEL_BXTWC_PMIC_TMU)	+= intel_bxtwc_tmu.o
intel_chtdc_ti_pwrbtn-y			:= chtdc_ti_pwrbtn.o
obj-$(CONFIG_INTEL_CHTDC_TI_PWRBTN)	+= intel_chtdc_ti_pwrbtn.o
intel_mrfld_pwrbtn-y			:= mrfld_pwrbtn.o
obj-$(CONFIG_INTEL_MRFLD_PWRBTN)	+= intel_mrfld_pwrbtn.o
intel_punit_ipc-y			:= punit_ipc.o
obj-$(CONFIG_INTEL_PUNIT_IPC)		+= intel_punit_ipc.o

# Intel Uncore drivers
intel-rst-y				:= rst.o
obj-$(CONFIG_INTEL_RST)			+= intel-rst.o
