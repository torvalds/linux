obj-y			+= interrupt.o iommu.o setup.o spider-pic.o
obj-y			+= pervasive.o

obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_SPU_FS)	+= spu-base.o spufs/

spu-base-y		+= spu_base.o spu_priv1.o

builtin-spufs-$(CONFIG_SPU_FS)	+= spu_syscalls.o
obj-y			+= $(builtin-spufs-m)
