#
# Makefile for the linux ppc-specific parts of the memory manager.
#

EXTRA_CFLAGS += -mno-minimal-toc

obj-y := fault.o init.o imalloc.o hash_utils.o hash_low.o tlb.o \
	slb_low.o slb.o stab.o mmap.o
obj-$(CONFIG_NEED_MULTIPLE_NODES) += numa.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_PPC_MULTIPLATFORM) += hash_native.o
