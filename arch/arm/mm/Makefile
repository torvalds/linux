#
# Makefile for the linux arm-specific parts of the memory manager.
#

obj-y				:= dma-mapping.o extable.o fault.o init.o \
				   iomap.o

obj-$(CONFIG_MMU)		+= fault-armv.o flush.o ioremap.o mmap.o \
				   pgd.o mmu.o

ifneq ($(CONFIG_MMU),y)
obj-y				+= nommu.o
endif

obj-$(CONFIG_MODULES)		+= proc-syms.o

obj-$(CONFIG_ALIGNMENT_TRAP)	+= alignment.o
obj-$(CONFIG_DISCONTIGMEM)	+= discontig.o

obj-$(CONFIG_CPU_ABRT_NOMMU)	+= abort-nommu.o
obj-$(CONFIG_CPU_ABRT_EV4)	+= abort-ev4.o
obj-$(CONFIG_CPU_ABRT_EV4T)	+= abort-ev4t.o
obj-$(CONFIG_CPU_ABRT_LV4T)	+= abort-lv4t.o
obj-$(CONFIG_CPU_ABRT_EV5T)	+= abort-ev5t.o
obj-$(CONFIG_CPU_ABRT_EV5TJ)	+= abort-ev5tj.o
obj-$(CONFIG_CPU_ABRT_EV6)	+= abort-ev6.o
obj-$(CONFIG_CPU_ABRT_EV7)	+= abort-ev7.o

obj-$(CONFIG_CPU_CACHE_V3)	+= cache-v3.o
obj-$(CONFIG_CPU_CACHE_V4)	+= cache-v4.o
obj-$(CONFIG_CPU_CACHE_V4WT)	+= cache-v4wt.o
obj-$(CONFIG_CPU_CACHE_V4WB)	+= cache-v4wb.o
obj-$(CONFIG_CPU_CACHE_V6)	+= cache-v6.o
obj-$(CONFIG_CPU_CACHE_V7)	+= cache-v7.o

obj-$(CONFIG_CPU_COPY_V3)	+= copypage-v3.o
obj-$(CONFIG_CPU_COPY_V4WT)	+= copypage-v4wt.o
obj-$(CONFIG_CPU_COPY_V4WB)	+= copypage-v4wb.o
obj-$(CONFIG_CPU_COPY_FEROCEON)	+= copypage-feroceon.o
obj-$(CONFIG_CPU_COPY_V6)	+= copypage-v6.o context.o
obj-$(CONFIG_CPU_SA1100)	+= copypage-v4mc.o
obj-$(CONFIG_CPU_XSCALE)	+= copypage-xscale.o
obj-$(CONFIG_CPU_XSC3)		+= copypage-xsc3.o

obj-$(CONFIG_CPU_TLB_V3)	+= tlb-v3.o
obj-$(CONFIG_CPU_TLB_V4WT)	+= tlb-v4.o
obj-$(CONFIG_CPU_TLB_V4WB)	+= tlb-v4wb.o
obj-$(CONFIG_CPU_TLB_V4WBI)	+= tlb-v4wbi.o
obj-$(CONFIG_CPU_TLB_FEROCEON)	+= tlb-v4wbi.o	# reuse v4wbi TLB functions
obj-$(CONFIG_CPU_TLB_V6)	+= tlb-v6.o
obj-$(CONFIG_CPU_TLB_V7)	+= tlb-v7.o

obj-$(CONFIG_CPU_ARM610)	+= proc-arm6_7.o
obj-$(CONFIG_CPU_ARM710)	+= proc-arm6_7.o
obj-$(CONFIG_CPU_ARM7TDMI)	+= proc-arm7tdmi.o
obj-$(CONFIG_CPU_ARM720T)	+= proc-arm720.o
obj-$(CONFIG_CPU_ARM740T)	+= proc-arm740.o
obj-$(CONFIG_CPU_ARM9TDMI)	+= proc-arm9tdmi.o
obj-$(CONFIG_CPU_ARM920T)	+= proc-arm920.o
obj-$(CONFIG_CPU_ARM922T)	+= proc-arm922.o
obj-$(CONFIG_CPU_ARM925T)	+= proc-arm925.o
obj-$(CONFIG_CPU_ARM926T)	+= proc-arm926.o
obj-$(CONFIG_CPU_ARM940T)	+= proc-arm940.o
obj-$(CONFIG_CPU_ARM946E)	+= proc-arm946.o
obj-$(CONFIG_CPU_ARM1020)	+= proc-arm1020.o
obj-$(CONFIG_CPU_ARM1020E)	+= proc-arm1020e.o
obj-$(CONFIG_CPU_ARM1022)	+= proc-arm1022.o
obj-$(CONFIG_CPU_ARM1026)	+= proc-arm1026.o
obj-$(CONFIG_CPU_SA110)		+= proc-sa110.o
obj-$(CONFIG_CPU_SA1100)	+= proc-sa1100.o
obj-$(CONFIG_CPU_XSCALE)	+= proc-xscale.o
obj-$(CONFIG_CPU_XSC3)		+= proc-xsc3.o
obj-$(CONFIG_CPU_FEROCEON)	+= proc-feroceon.o
obj-$(CONFIG_CPU_V6)		+= proc-v6.o
obj-$(CONFIG_CPU_V7)		+= proc-v7.o

obj-$(CONFIG_CACHE_FEROCEON_L2)	+= cache-feroceon-l2.o
obj-$(CONFIG_CACHE_L2X0)	+= cache-l2x0.o
obj-$(CONFIG_CACHE_XSC3L2)	+= cache-xsc3l2.o

