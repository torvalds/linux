obj-$(CONFIG_PPC_CELL_NATIVE)		+= interrupt.o iommu.o setup.o \
					   cbe_regs.o spider-pic.o \
					   pervasive.o pmu.o io-workarounds.o
obj-$(CONFIG_CBE_RAS)			+= ras.o

obj-$(CONFIG_CBE_THERM)			+= cbe_thermal.o
obj-$(CONFIG_CBE_CPUFREQ_PMI)		+= cbe_cpufreq_pmi.o
obj-$(CONFIG_CBE_CPUFREQ)		+= cbe-cpufreq.o
cbe-cpufreq-y				+= cbe_cpufreq_pervasive.o cbe_cpufreq.o

ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_PPC_CELL_NATIVE)		+= smp.o
endif

# needed only when building loadable spufs.ko
spu-priv1-$(CONFIG_PPC_CELL_NATIVE)	+= spu_priv1_mmio.o

spu-manage-$(CONFIG_PPC_CELLEB)		+= spu_manage.o
spu-manage-$(CONFIG_PPC_CELL_NATIVE)	+= spu_manage.o

obj-$(CONFIG_SPU_BASE)			+= spu_callbacks.o spu_base.o \
					   spu_syscalls.o \
					   $(spu-priv1-y) \
					   $(spu-manage-y) \
					   spufs/

obj-$(CONFIG_PCI_MSI)			+= axon_msi.o
