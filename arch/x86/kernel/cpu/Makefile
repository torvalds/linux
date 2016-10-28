#
# Makefile for x86-compatible CPU details, features and quirks
#

# Don't trace early stages of a secondary CPU boot
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_common.o = -pg
CFLAGS_REMOVE_perf_event.o = -pg
endif

# If these files are instrumented, boot hangs during the first second.
KCOV_INSTRUMENT_common.o := n
KCOV_INSTRUMENT_perf_event.o := n

# Make sure load_percpu_segment has no stackprotector
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_common.o		:= $(nostackp)

obj-y			:= intel_cacheinfo.o scattered.o topology.o
obj-y			+= common.o
obj-y			+= rdrand.o
obj-y			+= match.o

obj-$(CONFIG_PROC_FS)	+= proc.o
obj-$(CONFIG_X86_FEATURE_NAMES) += capflags.o powerflags.o

obj-$(CONFIG_X86_32)	+= bugs.o
obj-$(CONFIG_X86_64)	+= bugs_64.o

obj-$(CONFIG_CPU_SUP_INTEL)		+= intel.o
obj-$(CONFIG_CPU_SUP_AMD)		+= amd.o
obj-$(CONFIG_CPU_SUP_CYRIX_32)		+= cyrix.o
obj-$(CONFIG_CPU_SUP_CENTAUR)		+= centaur.o
obj-$(CONFIG_CPU_SUP_TRANSMETA_32)	+= transmeta.o
obj-$(CONFIG_CPU_SUP_UMC_32)		+= umc.o

obj-$(CONFIG_INTEL_RDT_A)	+= intel_rdt.o intel_rdt_rdtgroup.o

obj-$(CONFIG_X86_MCE)			+= mcheck/
obj-$(CONFIG_MTRR)			+= mtrr/
obj-$(CONFIG_MICROCODE)			+= microcode/

obj-$(CONFIG_X86_LOCAL_APIC)		+= perfctr-watchdog.o

obj-$(CONFIG_HYPERVISOR_GUEST)		+= vmware.o hypervisor.o mshyperv.o

ifdef CONFIG_X86_FEATURE_NAMES
quiet_cmd_mkcapflags = MKCAP   $@
      cmd_mkcapflags = $(CONFIG_SHELL) $(srctree)/$(src)/mkcapflags.sh $< $@

cpufeature = $(src)/../../include/asm/cpufeatures.h

targets += capflags.c
$(obj)/capflags.c: $(cpufeature) $(src)/mkcapflags.sh FORCE
	$(call if_changed,mkcapflags)
endif
clean-files += capflags.c
