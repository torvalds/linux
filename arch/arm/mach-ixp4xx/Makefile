# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-pci-y	:=
obj-pci-n	:=

# Device tree platform
obj-pci-$(CONFIG_MACH_IXP4XX_OF)	+= ixp4xx-of.o

obj-pci-$(CONFIG_ARCH_IXDP4XX)		+= ixdp425-pci.o
obj-pci-$(CONFIG_MACH_AVILA)		+= avila-pci.o
obj-pci-$(CONFIG_MACH_IXDPG425)		+= ixdpg425-pci.o
obj-pci-$(CONFIG_ARCH_ADI_COYOTE)	+= coyote-pci.o
obj-pci-$(CONFIG_MACH_GTWX5715)		+= gtwx5715-pci.o
obj-pci-$(CONFIG_MACH_GATEWAY7001)	+= gateway7001-pci.o
obj-pci-$(CONFIG_MACH_FSG)		+= fsg-pci.o
obj-pci-$(CONFIG_MACH_ARCOM_VULCAN)	+= vulcan-pci.o

obj-y	+= common.o

obj-$(CONFIG_ARCH_IXDP4XX)	+= ixdp425-setup.o
obj-$(CONFIG_MACH_AVILA)	+= avila-setup.o
obj-$(CONFIG_MACH_IXDPG425)	+= coyote-setup.o
obj-$(CONFIG_ARCH_ADI_COYOTE)	+= coyote-setup.o
obj-$(CONFIG_MACH_GTWX5715)	+= gtwx5715-setup.o
obj-$(CONFIG_MACH_GATEWAY7001)	+= gateway7001-setup.o
obj-$(CONFIG_MACH_FSG)		+= fsg-setup.o
obj-$(CONFIG_MACH_GORAMO_MLR)	+= goramo_mlr.o
obj-$(CONFIG_MACH_ARCOM_VULCAN)	+= vulcan-setup.o

obj-$(CONFIG_PCI)		+= $(obj-pci-$(CONFIG_PCI)) common-pci.o
