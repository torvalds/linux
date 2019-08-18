# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				+= cache.o
obj-y				+= context.o
obj-y				+= extable.o
obj-y				+= fault.o
obj-y				+= gup.o
obj-y				+= init.o
obj-y				+= mmap.o
obj-y				+= page.o
obj-y				+= page-funcs.o
obj-y				+= pgtable.o
obj-y				+= tlbex.o
obj-y				+= tlbex-fault.o
obj-y				+= tlb-funcs.o

ifdef CONFIG_CPU_MICROMIPS
obj-y				+= uasm-micromips.o
else
obj-y				+= uasm-mips.o
endif

obj-$(CONFIG_32BIT)		+= ioremap.o pgtable-32.o
obj-$(CONFIG_64BIT)		+= pgtable-64.o
obj-$(CONFIG_HIGHMEM)		+= highmem.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_DMA_NONCOHERENT)	+= dma-noncoherent.o

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
