#
# Makefile for Lemote Fulong mini-PC board.
#

obj-y += setup.o init.o reset.o irq.o pci.o bonito-irq.o mem.o \
		env.o cmdline.o time.o machtype.o

#
# Early printk support
#
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

EXTRA_CFLAGS += -Werror
