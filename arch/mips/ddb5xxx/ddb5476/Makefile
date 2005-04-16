#
# Makefile for the NEC DDB Vrc-5476 specific kernel interface routines
# under Linux.
#

obj-y			+= setup.o irq.o int-handler.o nile4_pic.o vrc5476_irq.o
obj-$(CONFIG_KGDB)	+= dbg_io.o

EXTRA_AFLAGS := $(CFLAGS)
