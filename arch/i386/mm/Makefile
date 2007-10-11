#
# Makefile for the linux i386-specific parts of the memory manager.
#

obj-y	:= init_32.o pgtable_32.o fault.o ioremap_32.o extable_32.o pageattr_32.o mmap_32.o

obj-$(CONFIG_NUMA) += discontig_32.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_HIGHMEM) += highmem_32.o
obj-$(CONFIG_BOOT_IOREMAP) += boot_ioremap_32.o
