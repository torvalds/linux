#
# Makefile for the SNI specific part of the kernel
#

obj-y += irq.o reset.o setup.o a20r.o rm200.o pcimt.o pcit.o time.o
obj-$(CONFIG_CPU_BIG_ENDIAN) += sniprom.o

EXTRA_CFLAGS += -Werror
