#
# Makefile for the linux m68k-specific parts of the memory manager.
#

obj-y	:= init.o

obj-$(CONFIG_MMU)		+= cache.o fault.o
obj-$(CONFIG_MMU_MOTOROLA)	+= kmap.o memory.o motorola.o hwtest.o
obj-$(CONFIG_MMU_SUN3)		+= sun3kmap.o sun3mmu.o hwtest.o
obj-$(CONFIG_MMU_COLDFIRE)	+= kmap.o memory.o mcfmmu.o

