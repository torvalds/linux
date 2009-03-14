#
# Makefile for x86-compatible CPU details and quirks
#

# Don't trace early stages of a secondary CPU boot
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_common.o = -pg
endif

obj-y			:= intel_cacheinfo.o addon_cpuid_features.o
obj-y			+= proc.o capflags.o powerflags.o common.o
obj-y			+= vmware.o hypervisor.o

obj-$(CONFIG_X86_32)	+= bugs.o cmpxchg.o
obj-$(CONFIG_X86_64)	+= bugs_64.o

obj-$(CONFIG_X86_CPU_DEBUG)		+= cpu_debug.o

obj-$(CONFIG_CPU_SUP_INTEL)		+= intel.o
obj-$(CONFIG_CPU_SUP_AMD)		+= amd.o
obj-$(CONFIG_CPU_SUP_CYRIX_32)		+= cyrix.o
obj-$(CONFIG_CPU_SUP_CENTAUR)		+= centaur.o
obj-$(CONFIG_CPU_SUP_TRANSMETA_32)	+= transmeta.o
obj-$(CONFIG_CPU_SUP_UMC_32)		+= umc.o

obj-$(CONFIG_X86_MCE)	+= mcheck/
obj-$(CONFIG_MTRR)	+= mtrr/
obj-$(CONFIG_CPU_FREQ)	+= cpufreq/

obj-$(CONFIG_X86_LOCAL_APIC) += perfctr-watchdog.o

quiet_cmd_mkcapflags = MKCAP   $@
      cmd_mkcapflags = $(PERL) $(srctree)/$(src)/mkcapflags.pl $< $@

cpufeature = $(src)/../../include/asm/cpufeature.h

targets += capflags.c
$(obj)/capflags.c: $(cpufeature) $(src)/mkcapflags.pl FORCE
	$(call if_changed,mkcapflags)
