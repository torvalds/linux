#
# Makefile for x86-compatible CPU details and quirks
#

obj-y			:= intel_cacheinfo.o addon_cpuid_features.o
obj-y			+= proc.o capflags.o powerflags.o

obj-$(CONFIG_X86_32)	+= common.o bugs.o cmpxchg.o
obj-$(CONFIG_X86_64)	+= common_64.o bugs_64.o

obj-$(CONFIG_CPU_SUP_INTEL_32)		+= intel.o
obj-$(CONFIG_CPU_SUP_INTEL_64)		+= intel_64.o
obj-$(CONFIG_CPU_SUP_AMD_32)		+= amd.o
obj-$(CONFIG_CPU_SUP_AMD_64)		+= amd_64.o
obj-$(CONFIG_CPU_SUP_CYRIX_32)		+= cyrix.o
obj-$(CONFIG_CPU_SUP_CENTAUR_32)	+= centaur.o
obj-$(CONFIG_CPU_SUP_CENTAUR_64)	+= centaur_64.o
obj-$(CONFIG_CPU_SUP_TRANSMETA_32)	+= transmeta.o
obj-$(CONFIG_CPU_SUP_UMC_32)		+= umc.o

obj-$(CONFIG_X86_MCE)	+= mcheck/
obj-$(CONFIG_MTRR)	+= mtrr/
obj-$(CONFIG_CPU_FREQ)	+= cpufreq/

obj-$(CONFIG_X86_LOCAL_APIC) += perfctr-watchdog.o

quiet_cmd_mkcapflags = MKCAP   $@
      cmd_mkcapflags = $(PERL) $(srctree)/$(src)/mkcapflags.pl $< $@

cpufeature = $(src)/../../../../include/asm-x86/cpufeature.h

targets += capflags.c
$(obj)/capflags.c: $(cpufeature) $(src)/mkcapflags.pl FORCE
	$(call if_changed,mkcapflags)
