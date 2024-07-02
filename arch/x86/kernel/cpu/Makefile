# SPDX-License-Identifier: GPL-2.0
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
KMSAN_SANITIZE_common.o := n

# As above, instrumenting secondary CPU boot code causes boot hangs.
KCSAN_SANITIZE_common.o := n

obj-y			:= cacheinfo.o scattered.o
obj-y			+= topology_common.o topology_ext.o topology_amd.o
obj-y			+= common.o
obj-y			+= rdrand.o
obj-y			+= match.o
obj-y			+= bugs.o
obj-y			+= aperfmperf.o
obj-y			+= cpuid-deps.o
obj-y			+= umwait.o
obj-y 			+= capflags.o powerflags.o

obj-$(CONFIG_X86_LOCAL_APIC)		+= topology.o

obj-$(CONFIG_PROC_FS)			+= proc.o

obj-$(CONFIG_IA32_FEAT_CTL)		+= feat_ctl.o
ifdef CONFIG_CPU_SUP_INTEL
obj-y					+= intel.o intel_pconfig.o tsx.o
obj-$(CONFIG_PM)			+= intel_epb.o
endif
obj-$(CONFIG_CPU_SUP_AMD)		+= amd.o
obj-$(CONFIG_CPU_SUP_HYGON)		+= hygon.o
obj-$(CONFIG_CPU_SUP_CYRIX_32)		+= cyrix.o
obj-$(CONFIG_CPU_SUP_CENTAUR)		+= centaur.o
obj-$(CONFIG_CPU_SUP_TRANSMETA_32)	+= transmeta.o
obj-$(CONFIG_CPU_SUP_UMC_32)		+= umc.o
obj-$(CONFIG_CPU_SUP_ZHAOXIN)		+= zhaoxin.o
obj-$(CONFIG_CPU_SUP_VORTEX_32)		+= vortex.o

obj-$(CONFIG_X86_MCE)			+= mce/
obj-$(CONFIG_MTRR)			+= mtrr/
obj-$(CONFIG_MICROCODE)			+= microcode/
obj-$(CONFIG_X86_CPU_RESCTRL)		+= resctrl/
obj-$(CONFIG_X86_SGX)			+= sgx/

obj-$(CONFIG_X86_LOCAL_APIC)		+= perfctr-watchdog.o

obj-$(CONFIG_HYPERVISOR_GUEST)		+= vmware.o hypervisor.o mshyperv.o
obj-$(CONFIG_ACRN_GUEST)		+= acrn.o

obj-$(CONFIG_DEBUG_FS)			+= debugfs.o

quiet_cmd_mkcapflags = MKCAP   $@
      cmd_mkcapflags = $(CONFIG_SHELL) $(srctree)/$(src)/mkcapflags.sh $@ $^

cpufeature = $(src)/../../include/asm/cpufeatures.h
vmxfeature = $(src)/../../include/asm/vmxfeatures.h

$(obj)/capflags.c: $(cpufeature) $(vmxfeature) $(src)/mkcapflags.sh FORCE
	$(call if_changed,mkcapflags)
targets += capflags.c
