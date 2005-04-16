#
# Makefile for the DECstation family specific parts of the kernel
#

obj-y		:= ecc-berr.o int-handler.o ioasic-irq.o kn02-irq.o reset.o \
		   setup.o time.o

obj-$(CONFIG_PROM_CONSOLE)	+= promcon.o
obj-$(CONFIG_CPU_HAS_WB)	+= wbflush.o

EXTRA_AFLAGS := $(CFLAGS)
