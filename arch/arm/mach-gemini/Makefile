#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= irq.o mm.o time.o devices.o gpio.o idle.o

# Board-specific support
obj-$(CONFIG_MACH_NAS4220B)	+= board-nas4220b.o
obj-$(CONFIG_MACH_RUT100)	+= board-rut1xx.o
obj-$(CONFIG_MACH_WBD111)	+= board-wbd111.o
obj-$(CONFIG_MACH_WBD222)	+= board-wbd222.o
