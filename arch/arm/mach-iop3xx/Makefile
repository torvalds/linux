#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= common.o

obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_ARCH_IOP321)  += iop321-setup.o iop321-irq.o iop321-pci.o iop321-time.o

obj-$(CONFIG_ARCH_IOP331)  += iop331-setup.o iop331-irq.o iop331-pci.o iop331-time.o

obj-$(CONFIG_ARCH_IQ80321) += iq80321-mm.o iq80321-pci.o

obj-$(CONFIG_ARCH_IQ31244) += iq31244-mm.o iq31244-pci.o

obj-$(CONFIG_ARCH_IQ80331) += iq80331-mm.o iq80331-pci.o

obj-$(CONFIG_MACH_IQ80332) += iq80332-mm.o iq80332-pci.o
