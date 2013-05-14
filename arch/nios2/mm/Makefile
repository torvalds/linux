#
# Makefile for the Nios2-specific parts of the memory manager.
#

obj-y := init.o

obj-y += cacheflush$(MMU).o

obj-$(CONFIG_MMU) += pgtable.o tlb.o uaccess.o fault.o
obj-$(CONFIG_MMU) += ioremap.o extable.o mmu_context.o

obj-y += dma-mapping$(MMU).o