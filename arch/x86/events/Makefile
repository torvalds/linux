# SPDX-License-Identifier: GPL-2.0-only
obj-y					+= core.o probe.o
obj-$(PERF_EVENTS_INTEL_RAPL)		+= rapl.o
obj-y					+= amd/
obj-$(CONFIG_X86_LOCAL_APIC)            += msr.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/
obj-$(CONFIG_CPU_SUP_CENTAUR)		+= zhaoxin/
obj-$(CONFIG_CPU_SUP_ZHAOXIN)		+= zhaoxin/
