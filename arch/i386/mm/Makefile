#
# Makefile for the linux i386-specific parts of the memory manager.
#

obj-y	:= init_32.o pgtable.o fault.o ioremap_32.o extable.o pageattr.o mmap.o

obj-$(CONFIG_NUMA) += discontig_32.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_HIGHMEM) += highmem.o
obj-$(CONFIG_BOOT_IOREMAP) += boot_ioremap_32.o
