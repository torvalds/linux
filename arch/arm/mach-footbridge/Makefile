# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= common.o isa-irq.o isa.o isa-rtc.o dma-isa.o

pci-y			+= dc21285.o
pci-$(CONFIG_ARCH_EBSA285) += ebsa285-pci.o
pci-$(CONFIG_ARCH_NETWINDER) += netwinder-pci.o

obj-$(CONFIG_ARCH_EBSA285) += ebsa285.o dc21285-timer.o
obj-$(CONFIG_ARCH_NETWINDER) += netwinder-hw.o isa-timer.o

obj-$(CONFIG_PCI)	+=$(pci-y)

