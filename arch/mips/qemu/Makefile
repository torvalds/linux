#
# Makefile for Qemu specific kernel interface routines under Linux.
#

obj-y		= q-firmware.o q-irq.o q-mem.o q-setup.o q-reset.o

obj-$(CONFIG_EARLY_PRINTK)	+= q-console.o
obj-$(CONFIG_SMP)		+= q-smp.o

EXTRA_CFLAGS += -Werror
