#
# Makefile for common code for Toshiba TX4927 based systems
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y	+= prom.o setup.o irq.o irq_handler.o rtc_rx5c348.o
obj-$(CONFIG_KGDB) += dbgio.o

