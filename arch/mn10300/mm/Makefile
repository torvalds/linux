#
# Makefile for the MN10300-specific memory management code
#

cacheflush-y	:= cache.o cache-mn10300.o
cacheflush-$(CONFIG_MN10300_CACHE_WBACK) += cache-flush-mn10300.o

cacheflush-$(CONFIG_MN10300_CACHE_DISABLED) := cache-disabled.o

obj-y := \
	init.o fault.o pgtable.o extable.o tlb-mn10300.o mmu-context.o \
	misalignment.o dma-alloc.o $(cacheflush-y)
