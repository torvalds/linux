# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ia64-specific parts of the memory manager.
#

obj-y := init.o fault.o tlb.o extable.o ioremap.o

obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_NUMA)	   += numa.o
obj-$(CONFIG_DISCONTIGMEM) += discontig.o
obj-$(CONFIG_SPARSEMEM)	   += discontig.o
obj-$(CONFIG_FLATMEM)	   += contig.o
