#
# Makefile for the linux s390-specific parts of the memory manager.
#

obj-y	 := init.o fault.o extmem.o mmap.o vmem.o pgtable.o
obj-$(CONFIG_CMM) += cmm.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_PAGE_STATES) += page-states.o
