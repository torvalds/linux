#
# Makefile for the DECstation family specific parts of the kernel
#

obj-y		:= ecc-berr.o int-handler.o ioasic-irq.o kn01-berr.o \
		   kn02-irq.o kn02xa-berr.o reset.o setup.o time.o

obj-$(CONFIG_PROM_CONSOLE)	+= promcon.o
obj-$(CONFIG_TC)		+= tc.o
obj-$(CONFIG_CPU_HAS_WB)	+= wbflush.o
