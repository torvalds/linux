#
# Makefile for the linux m68k-specific parts of the memory manager.
#

obj-y		:= cache.o init.o fault.o hwtest.o

obj-$(CONFIG_MMU_MOTOROLA)	+= kmap.o memory.o motorola.o
obj-$(CONFIG_MMU_SUN3)		+= sun3kmap.o sun3mmu.o
