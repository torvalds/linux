#
# Makefile for x86-compatible CPU details, features and quirks
#

# Don't trace early stages of a secondary CPU boot
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_common.o = -pg
CFLAGS_REMOVE_perf_event.o = -pg
endif

# Make sure load_percpu_segment has no stackprotector
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_common.o		:= $(nostackp)

obj-y			:= intel_cacheinfo.o scattered.o topology.o
obj-y			+= proc.o capflags.o powerflags.o common.o
obj-y			+= vmware.o hypervisor.o sched.o mshyperv.o
obj-y			+= rdrand.o
obj-y			+= match.o

obj-$(CONFIG_X86_32)	+= bugs.o
obj-$(CONFIG_X86_64)	+= bugs_64.o

obj-$(CONFIG_CPU_SUP_INTEL)		+= intel.o
obj-$(CONFIG_CPU_SUP_AMD)		+= amd.o
obj-$(CONFIG_CPU_SUP_CYRIX_32)		+= cyrix.o
obj-$(CONFIG_CPU_SUP_CENTAUR)		+= centaur.o
obj-$(CONFIG_CPU_SUP_TRANSMETA_32)	+= transmeta.o
obj-$(CONFIG_CPU_SUP_UMC_32)		+= umc.o

obj-$(CONFIG_PERF_EVENTS)		+= perf_event.o

ifdef CONFIG_PERF_EVENTS
obj-$(CONFIG_CPU_SUP_AMD)		+= perf_event_amd.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= perf_event_p6.o perf_event_p4.o perf_event_intel_lbr.o perf_event_intel_ds.o perf_event_intel.o
endif

obj-$(CONFIG_X86_MCE)			+= mcheck/
obj-$(CONFIG_MTRR)			+= mtrr/

obj-$(CONFIG_X86_LOCAL_APIC)		+= perfctr-watchdog.o perf_event_amd_ibs.o

quiet_cmd_mkcapflags = MKCAP   $@
      cmd_mkcapflags = $(PERL) $(srctree)/$(src)/mkcapflags.pl $< $@

cpufeature = $(src)/../../include/asm/cpufeature.h

targets += capflags.c
$(obj)/capflags.c: $(cpufeature) $(src)/mkcapflags.pl FORCE
	$(call if_changed,mkcapflags)
