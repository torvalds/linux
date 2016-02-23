#
# Makefile for the linux Meta-specific parts of the memory manager.
#

obj-y				+= cache.o
obj-y				+= extable.o
obj-y				+= fault.o
obj-y				+= init.o
obj-y				+= ioremap.o
obj-y				+= maccess.o

mmu-y				:= mmu-meta1.o
mmu-$(CONFIG_METAG_META21_MMU)	:= mmu-meta2.o
obj-y				+= $(mmu-y)

obj-$(CONFIG_HIGHMEM)		+= highmem.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_METAG_L2C)		+= l2cache.o
obj-$(CONFIG_NUMA)		+= numa.o
