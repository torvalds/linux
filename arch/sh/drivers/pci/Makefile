# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the PCI specific kernel interface routines under Linux.
#
obj-y					+= common.o pci.o

obj-$(CONFIG_CPU_SUBTYPE_SH7751)	+= pci-sh7751.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751R)	+= pci-sh7751.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7763)	+= pci-sh7780.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7780)	+= pci-sh7780.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7785)	+= pci-sh7780.o ops-sh4.o
obj-$(CONFIG_CPU_SUBTYPE_SH7786)	+= pcie-sh7786.o ops-sh7786.o

obj-$(CONFIG_SH_DREAMCAST)		+= ops-dreamcast.o fixups-dreamcast.o \
					   pci-dreamcast.o
obj-$(CONFIG_SH_SECUREEDGE5410)		+= fixups-snapgear.o
obj-$(CONFIG_SH_7751_SOLUTION_ENGINE)	+= fixups-se7751.o
obj-$(CONFIG_SH_RTS7751R2D)		+= fixups-rts7751r2d.o
obj-$(CONFIG_SH_SH03)			+= fixups-sh03.o
obj-$(CONFIG_SH_HIGHLANDER)		+= fixups-r7780rp.o
obj-$(CONFIG_SH_SH7785LCR)		+= fixups-r7780rp.o
obj-$(CONFIG_SH_SDK7786)		+= fixups-sdk7786.o
obj-$(CONFIG_SH_SDK7780)		+= fixups-sdk7780.o
obj-$(CONFIG_SH_7780_SOLUTION_ENGINE)	+= fixups-sdk7780.o
obj-$(CONFIG_SH_TITAN)			+= fixups-titan.o
obj-$(CONFIG_SH_LANDISK)		+= fixups-landisk.o
obj-$(CONFIG_SH_LBOX_RE2)		+= fixups-rts7751r2d.o
obj-$(CONFIG_SH_CAYMAN)			+= fixups-cayman.o
