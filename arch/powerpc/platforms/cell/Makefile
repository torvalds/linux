obj-$(CONFIG_PPC_CELL_NATIVE)		+= interrupt.o iommu.o setup.o \
					   cbe_regs.o spider-pic.o pervasive.o \
					   pmu.o
obj-$(CONFIG_CBE_RAS)			+= ras.o

obj-$(CONFIG_CBE_THERM)			+= cbe_thermal.o
obj-$(CONFIG_CBE_CPUFREQ)		+= cbe_cpufreq.o

ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_PPC_CELL_NATIVE)		+= smp.o
endif

# needed only when building loadable spufs.ko
spufs-modular-$(CONFIG_SPU_FS)		+= spu_syscalls.o
spu-priv1-$(CONFIG_PPC_CELL_NATIVE)	+= spu_priv1_mmio.o

obj-$(CONFIG_SPU_BASE)			+= spu_callbacks.o spu_base.o \
					   $(spufs-modular-m) \
					   $(spu-priv1-y) spufs/
