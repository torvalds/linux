# SPDX-License-Identifier: GPL-2.0
obj-y				=  core.o severity.o genpool.o

obj-$(CONFIG_X86_ANCIENT_MCE)	+= winchip.o p5.o
obj-$(CONFIG_X86_MCE_INTEL)	+= intel.o
obj-$(CONFIG_X86_MCE_AMD)	+= amd.o
obj-$(CONFIG_X86_MCE_THRESHOLD) += threshold.o

mce-inject-y			:= inject.o
obj-$(CONFIG_X86_MCE_INJECT)	+= mce-inject.o

obj-$(CONFIG_X86_THERMAL_VECTOR) += therm_throt.o

obj-$(CONFIG_ACPI_APEI)		+= apei.o

obj-$(CONFIG_X86_MCELOG_LEGACY)	+= dev-mcelog.o
