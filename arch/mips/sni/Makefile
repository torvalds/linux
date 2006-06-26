#
# Makefile for the SNI specific part of the kernel
#

obj-y	 	+= irq.o pcimt_scache.o reset.o setup.o
obj-$(CONFIG_CPU_BIG_ENDIAN) += sniprom.o

EXTRA_AFLAGS := $(CFLAGS)
