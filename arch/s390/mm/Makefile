# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux s390-specific parts of the memory manager.
#

obj-y		:= init.o fault.o extmem.o mmap.o vmem.o maccess.o
obj-y		+= page-states.o pageattr.o pgtable.o pgalloc.o extable.o

obj-$(CONFIG_CMM)		+= cmm.o
obj-$(CONFIG_DEBUG_VIRTUAL)	+= physaddr.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_PTDUMP)		+= dump_pagetables.o
obj-$(CONFIG_PGSTE)		+= gmap.o
obj-$(CONFIG_PFAULT)		+= pfault.o
