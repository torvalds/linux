#
# Makefile for the arch-specific parts of the memory manager.
#

obj-y := init.o kmap.o

obj-$(CONFIG_MMU) += \
	pgalloc.o highmem.o fault.o extable.o cache-page.o tlb-flush.o tlb-miss.o \
	mmu-context.o dma-alloc.o unaligned.o elf-fdpic.o
