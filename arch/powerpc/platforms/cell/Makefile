obj-y			+= interrupt.o iommu.o setup.o spider-pic.o
obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_SPU_FS)	+= spufs/ spu_base.o
builtin-spufs-$(CONFIG_SPU_FS)	+= spu_syscalls.o
obj-y			+= $(builtin-spufs-m)
