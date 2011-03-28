#
# Makefile for the linux unicore-specific parts of the memory manager.
#

obj-y				:= extable.o fault.o init.o pgd.o mmu.o
obj-y				+= flush.o ioremap.o

obj-$(CONFIG_SWIOTLB)		+= dma-swiotlb.o

obj-$(CONFIG_MODULES)		+= proc-syms.o

obj-$(CONFIG_ALIGNMENT_TRAP)	+= alignment.o

obj-$(CONFIG_CPU_UCV2)		+= cache-ucv2.o tlb-ucv2.o proc-ucv2.o

