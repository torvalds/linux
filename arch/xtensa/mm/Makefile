#
# Makefile for the Linux/Xtensa-specific parts of the memory manager.
#

obj-y			:= init.o misc.o
obj-$(CONFIG_MMU)	+= cache.o fault.o ioremap.o mmu.o tlb.o
obj-$(CONFIG_HIGHMEM)	+= highmem.o
obj-$(CONFIG_KASAN)	+= kasan_init.o

KASAN_SANITIZE_fault.o := n
KASAN_SANITIZE_kasan_init.o := n
KASAN_SANITIZE_mmu.o := n
