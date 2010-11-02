#
# Makefile for the MN10300-specific memory management code
#

cache-smp-wback-$(CONFIG_MN10300_CACHE_WBACK) := cache-smp-flush.o

cacheflush-y	:= cache.o
cacheflush-$(CONFIG_SMP) += cache-smp.o cache-smp-inv.o $(cache-smp-wback-y)
cacheflush-$(CONFIG_MN10300_CACHE_INV_ICACHE) += cache-inv-icache.o
cacheflush-$(CONFIG_MN10300_CACHE_FLUSH_ICACHE) += cache-flush-icache.o
cacheflush-$(CONFIG_MN10300_CACHE_INV_BY_TAG) += cache-inv-by-tag.o
cacheflush-$(CONFIG_MN10300_CACHE_INV_BY_REG) += cache-inv-by-reg.o
cacheflush-$(CONFIG_MN10300_CACHE_FLUSH_BY_TAG) += cache-flush-by-tag.o
cacheflush-$(CONFIG_MN10300_CACHE_FLUSH_BY_REG) += cache-flush-by-reg.o

cacheflush-$(CONFIG_MN10300_CACHE_DISABLED) := cache-disabled.o

obj-y := \
	init.o fault.o pgtable.o extable.o tlb-mn10300.o mmu-context.o \
	misalignment.o dma-alloc.o $(cacheflush-y)

obj-$(CONFIG_SMP) += tlb-smp.o
