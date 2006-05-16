#
# Makefile for the linux kernel.
#

obj-y	+= common.o

obj-$(CONFIG_PCI)		+= common-pci.o
obj-$(CONFIG_ARCH_IXDP4XX)	+= ixdp425-pci.o ixdp425-setup.o
obj-$(CONFIG_MACH_IXDPG425)	+= ixdpg425-pci.o coyote-setup.o
obj-$(CONFIG_ARCH_ADI_COYOTE)	+= coyote-pci.o coyote-setup.o
obj-$(CONFIG_MACH_GTWX5715)	+= gtwx5715-pci.o gtwx5715-setup.o
obj-$(CONFIG_MACH_NSLU2)	+= nslu2-pci.o nslu2-setup.o nslu2-power.o
obj-$(CONFIG_MACH_NAS100D)	+= nas100d-pci.o nas100d-setup.o nas100d-power.o

