#
# Makefile for the linux tile-specific parts of the memory manager.
#

obj-y	:= init.o pgtable.o fault.o extable.o elf.o \
	   mmap.o homecache.o migrate_$(BITS).o

obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_HIGHMEM) += highmem.o
