# arch/arm/mach-ks8695/Makefile
#
# Makefile for KS8695 architecture support
#

obj-y				:= cpu.o irq.o time.o devices.o
obj-m				:=
obj-n				:=
obj-				:=

# PCI support is optional
#obj-$(CONFIG_PCI)		+= pci.o

# Board-specific support
obj-$(CONFIG_MACH_KS8695)	+= board-micrel.o
