#
# Makefile for x86-compatible CPU details and quirks
#

obj-y			:= intel_cacheinfo.o addon_cpuid_features.o
obj-y			+= proc.o feature_names.o

obj-$(CONFIG_X86_32)	+= common.o bugs.o
obj-$(CONFIG_X86_32)	+= amd.o
obj-$(CONFIG_X86_32)	+= cyrix.o
obj-$(CONFIG_X86_32)	+= centaur.o
obj-$(CONFIG_X86_32)	+= transmeta.o
obj-$(CONFIG_X86_32)	+= intel.o
obj-$(CONFIG_X86_32)	+= nexgen.o
obj-$(CONFIG_X86_32)	+= umc.o

obj-$(CONFIG_X86_MCE)	+= mcheck/
obj-$(CONFIG_MTRR)	+= mtrr/
obj-$(CONFIG_CPU_FREQ)	+= cpufreq/

obj-$(CONFIG_X86_LOCAL_APIC) += perfctr-watchdog.o
