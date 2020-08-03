# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux SuperH-specific parts of the memory manager.
#

obj-y			:= alignment.o cache.o init.o consistent.o mmap.o

cacheops-$(CONFIG_CPU_J2)		:= cache-j2.o
cacheops-$(CONFIG_CPU_SUBTYPE_SH7619)	:= cache-sh2.o
cacheops-$(CONFIG_CPU_SH2A)		:= cache-sh2a.o
cacheops-$(CONFIG_CPU_SH3)		:= cache-sh3.o
cacheops-$(CONFIG_CPU_SH4)		:= cache-sh4.o flush-sh4.o
cacheops-$(CONFIG_SH7705_CACHE_32KB)	+= cache-sh7705.o
cacheops-$(CONFIG_CPU_SHX3)		+= cache-shx3.o

obj-y			+= $(cacheops-y)

mmu-y			:= nommu.o extable_32.o
mmu-$(CONFIG_MMU)	:= extable_32.o fault.o ioremap.o kmap.o \
			   pgtable.o tlbex_32.o tlbflush_32.o

obj-y			+= $(mmu-y)

debugfs-y			:= asids-debugfs.o
ifndef CONFIG_CACHE_OFF
debugfs-$(CONFIG_CPU_SH4)	+= cache-debugfs.o
endif

ifdef CONFIG_MMU
debugfs-$(CONFIG_CPU_SH4)	+= tlb-debugfs.o
tlb-$(CONFIG_CPU_SH3)		:= tlb-sh3.o
tlb-$(CONFIG_CPU_SH4)		:= tlb-sh4.o tlb-urb.o
tlb-$(CONFIG_CPU_HAS_PTEAEX)	:= tlb-pteaex.o tlb-urb.o
obj-y				+= $(tlb-y)
endif

obj-$(CONFIG_DEBUG_FS)		+= $(debugfs-y)
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_PMB)		+= pmb.o
obj-$(CONFIG_NUMA)		+= numa.o
obj-$(CONFIG_IOREMAP_FIXED)	+= ioremap_fixed.o
obj-$(CONFIG_UNCACHED_MAPPING)	+= uncached.o
obj-$(CONFIG_HAVE_SRAM_POOL)	+= sram.o

GCOV_PROFILE_pmb.o := n

ccflags-y := -Werror
