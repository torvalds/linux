# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux M32R-specific parts of the memory manager.
#

ifdef CONFIG_MMU
obj-y	:= init.o fault.o mmu.o extable.o ioremap.o cache.o page.o
else
obj-y 	:= init.o fault-nommu.o mmu.o extable.o ioremap-nommu.o cache.o page.o
endif

obj-$(CONFIG_DISCONTIGMEM)	+= discontig.o

