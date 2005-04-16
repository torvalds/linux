#
# Makefile for the linux kernel.
#

obj-y	+= common.o common-pci.o 

obj-$(CONFIG_ARCH_IXDP4XX)	+= ixdp425-pci.o ixdp425-setup.o
obj-$(CONFIG_MACH_IXDPG425)	+= ixdpg425-pci.o coyote-setup.o
obj-$(CONFIG_ARCH_ADI_COYOTE)	+= coyote-pci.o coyote-setup.o
obj-$(CONFIG_MACH_GTWX5715)	+= gtwx5715-pci.o gtwx5715-setup.o

