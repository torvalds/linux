obj-y			+= core.o

obj-$(CONFIG_CPU_SUP_AMD)               += amd/core.o amd/uncore.o
obj-$(CONFIG_X86_LOCAL_APIC)            += amd/ibs.o
ifdef CONFIG_AMD_IOMMU
obj-$(CONFIG_CPU_SUP_AMD)               += amd/iommu.o
endif
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/core.o intel/bts.o intel/cqm.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/cstate.o intel/ds.o intel/lbr.o
obj-$(CONFIG_CPU_SUP_INTEL)		+= intel/pt.o
