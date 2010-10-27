#
# Makefile for the MN10300-specific memory management code
#

cacheflush-y	:= cache.o
cacheflush-$(CONFIG_MN10300_CACHE_INV_BY_TAG) += cache-inv-by-tag.o
cacheflush-$(CONFIG_MN10300_CACHE_INV_BY_REG) += cache-inv-by-reg.o
cacheflush-$(CONFIG_MN10300_CACHE_FLUSH_BY_TAG) += cache-flush-by-tag.o
cacheflush-$(CONFIG_MN10300_CACHE_FLUSH_BY_REG) += cache-flush-by-reg.o

cacheflush-$(CONFIG_MN10300_CACHE_DISABLED) := cache-disabled.o

obj-y := \
	init.o fault.o pgtable.o extable.o tlb-mn10300.o mmu-context.o \
	misalignment.o dma-alloc.o $(cacheflush-y)
