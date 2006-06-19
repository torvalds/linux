obj-y			+= interrupt.o iommu.o setup.o spider-pic.o
obj-y			+= cbe_regs.o pervasive.o
obj-$(CONFIG_CBE_RAS)	+= ras.o

obj-$(CONFIG_SMP)	+= smp.o

# needed only when building loadable spufs.ko
spufs-modular-$(CONFIG_SPU_FS) += spu_syscalls.o
obj-y			+= $(spufs-modular-m)

# always needed in kernel
spufs-builtin-$(CONFIG_SPU_FS) += spu_callbacks.o spu_base.o spu_priv1.o spufs/
obj-y			+= $(spufs-builtin-y) $(spufs-builtin-m)
