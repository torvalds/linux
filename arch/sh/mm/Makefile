#
# Makefile for the Linux SuperH-specific parts of the memory manager.
#

obj-y			:= init.o extable.o consistent.o

obj-$(CONFIG_CPU_SH2)	+= cache-sh2.o
obj-$(CONFIG_CPU_SH3)	+= cache-sh3.o
obj-$(CONFIG_CPU_SH4)	+= cache-sh4.o

mmu-y			:= fault-nommu.o tlb-nommu.o pg-nommu.o
mmu-$(CONFIG_MMU)	:= fault.o clear_page.o copy_page.o tlb-flush.o	\
			   ioremap.o

obj-y			+= $(mmu-y)

ifdef CONFIG_DEBUG_FS
obj-$(CONFIG_CPU_SH4)		+= cache-debugfs.o
endif

ifdef CONFIG_MMU
obj-$(CONFIG_CPU_SH3)		+= tlb-sh3.o
obj-$(CONFIG_CPU_SH4)		+= tlb-sh4.o pg-sh4.o
obj-$(CONFIG_SH7705_CACHE_32KB) += pg-sh7705.o
endif

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_SH7705_CACHE_32KB)	+= cache-sh7705.o
obj-$(CONFIG_32BIT)		+= pmb.o
obj-$(CONFIG_NUMA)		+= numa.o
