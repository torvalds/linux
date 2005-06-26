#
# Makefile for the linux i386-specific parts of the memory manager.
#

obj-y	:= init.o pgtable.o fault.o ioremap.o extable.o pageattr.o mmap.o

obj-$(CONFIG_NUMA) += discontig.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_HIGHMEM) += highmem.o
obj-$(CONFIG_BOOT_IOREMAP) += boot_ioremap.o
