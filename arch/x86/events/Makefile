obj-y			+= core.o

obj-$(CONFIG_CPU_SUP_AMD)               += amd/core.o amd/uncore.o
obj-$(CONFIG_PERF_EVENTS_AMD_POWER)	+= amd/power.o
obj-$(CONFIG_X86_LOCAL_APIC)            += amd/ibs.o msr.o
ifdef CONFIG_AMD_IOMMU
obj-$(CONFIG_CPU_SUP_AMD)               += amd/iommu.o
endif

obj-$(CONFIG_CPU_SUP_INTEL)		+= msr.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/
