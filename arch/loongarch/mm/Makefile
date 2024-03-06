# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/LoongArch-specific parts of the memory manager.
#

obj-y				+= init.o cache.o tlb.o tlbex.o extable.o \
				   fault.o ioremap.o maccess.o mmap.o pgtable.o page.o

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_KASAN)		+= kasan_init.o

KASAN_SANITIZE_kasan_init.o     := n
