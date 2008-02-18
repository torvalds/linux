#
# Makefile for the MN10300-specific memory management code
#

obj-y := \
	init.o fault.o pgtable.o extable.o tlb-mn10300.o mmu-context.o \
	misalignment.o dma-alloc.o

ifneq ($(CONFIG_MN10300_CACHE_DISABLED),y)
obj-y	+= cache.o cache-mn10300.o
ifeq ($(CONFIG_MN10300_CACHE_WBACK),y)
obj-y	+= cache-flush-mn10300.o
endif
endif
