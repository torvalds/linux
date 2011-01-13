#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

obj-y   := unit-init.o leds.o irq-fpga.o

obj-$(CONFIG_SMSC911X) += smsc911x.o
