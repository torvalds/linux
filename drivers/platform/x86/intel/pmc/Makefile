# SPDX-License-Identifier: GPL-2.0
#
# Intel x86 Platform-Specific Drivers
#

intel_pmc_core-y			:= core.o spt.o cnp.o icl.o \
					   tgl.o adl.o mtl.o arl.o lnl.o ptl.o wcl.o
obj-$(CONFIG_INTEL_PMC_CORE)		+= intel_pmc_core.o
intel_pmc_core_pltdrv-y			:= pltdrv.o
obj-$(CONFIG_INTEL_PMC_CORE)		+= intel_pmc_core_pltdrv.o

# Intel PMC SSRAM driver
intel_pmc_ssram_telemetry-y		+= ssram_telemetry.o
obj-$(CONFIG_INTEL_PMC_SSRAM_TELEMETRY)	+= intel_pmc_ssram_telemetry.o
