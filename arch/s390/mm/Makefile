#
# Makefile for the linux s390-specific parts of the memory manager.
#

obj-y		:= init.o fault.o extmem.o mmap.o vmem.o pgtable.o maccess.o
obj-y		+= page-states.o gup.o extable.o pageattr.o

obj-$(CONFIG_CMM)		+= cmm.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_S390_PTDUMP)	+= dump_pagetables.o
