#
# Makefile for Linux arch/m68k/atari source directory
#

obj-y		:= config.o time.o debug.o ataints.o stdma.o \
			atasound.o stram.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_HADES)	+= hades-pci.o
endif
obj-$(CONFIG_ATARI_KBD_CORE)	+= atakeyb.o
