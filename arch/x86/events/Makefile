obj-y			+= core.o

obj-$(CONFIG_CPU_SUP_AMD)               += amd/core.o amd/uncore.o
obj-$(CONFIG_X86_LOCAL_APIC)            += amd/ibs.o msr.o
ifdef CONFIG_AMD_IOMMU
obj-$(CONFIG_CPU_SUP_AMD)               += amd/iommu.o
endif
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/core.o intel/bts.o intel/cqm.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/cstate.o intel/ds.o intel/knc.o 
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/lbr.o intel/p4.o intel/p6.o intel/pt.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/rapl.o msr.o
obj-$(CONFIG_PERF_EVENTS_INTEL_UNCORE)	+= intel/uncore.o intel/uncore_nhmex.o
obj-$(CONFIG_PERF_EVENTS_INTEL_UNCORE)	+= intel/uncore_snb.o intel/uncore_snbep.o
