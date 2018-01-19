# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				+= cache.o dma-default.o extable.o fault.o \
				   gup.o init.o mmap.o page.o page-funcs.o \
				   pgtable.o tlbex.o tlbex-fault.o tlb-funcs.o

ifdef CONFIG_CPU_MICROMIPS
obj-y				+= uasm-micromips.o
else
obj-y				+= uasm-mips.o
endif

obj-$(CONFIG_32BIT)		+= ioremap.o pgtable-32.o
obj-$(CONFIG_64BIT)		+= pgtable-64.o
obj-$(CONFIG_HIGHMEM)		+= highmem.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o

obj-$(CONFIG_CPU_R4K_CACHE_TLB) += c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_R3000)		+= c-r3k.o tlb-r3k.o
obj-$(CONFIG_CPU_R8000)		+= c-r4k.o cex-gen.o tlb-r8k.o
obj-$(CONFIG_CPU_SB1)		+= c-r4k.o cerr-sb1.o cex-sb1.o tlb-r4k.o
obj-$(CONFIG_CPU_TX39XX)	+= c-tx39.o tlb-r3k.o
obj-$(CONFIG_CPU_CAVIUM_OCTEON) += c-octeon.o cex-oct.o tlb-r4k.o

obj-$(CONFIG_IP22_CPU_SCACHE)	+= sc-ip22.o
obj-$(CONFIG_R5000_CPU_SCACHE)	+= sc-r5k.o
obj-$(CONFIG_RM7000_CPU_SCACHE) += sc-rm7k.o
obj-$(CONFIG_MIPS_CPU_SCACHE)	+= sc-mips.o
obj-$(CONFIG_SCACHE_DEBUGFS)	+= sc-debugfs.o
