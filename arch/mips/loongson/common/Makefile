#
# Makefile for loongson based machines.
#

obj-y += setup.o init.o cmdline.o env.o time.o reset.o irq.o \
    pci.o bonito-irq.o mem.o machtype.o uart_base.o

#
# Early printk support
#
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o
obj-$(CONFIG_SERIAL_8250) += serial.o

#
# Enable CS5536 Virtual Support Module(VSM) to virtulize the PCI configure
# space
#
obj-$(CONFIG_CS5536) += cs5536/
