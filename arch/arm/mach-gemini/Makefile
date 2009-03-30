#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= irq.o mm.o time.o devices.o gpio.o

# Board-specific support
obj-$(CONFIG_MACH_RUT100)	+= board-rut1xx.o
