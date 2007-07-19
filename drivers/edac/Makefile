#
# Makefile for the Linux kernel EDAC drivers.
#
# Copyright 02 Jul 2003, Linux Networx (http://lnxi.com)
# This file may be distributed under the terms of the
# GNU General Public License.
#


obj-$(CONFIG_EDAC)			:= edac_stub.o
obj-$(CONFIG_EDAC_MM_EDAC)		+= edac_core.o

edac_core-objs	:= edac_mc.o edac_device.o edac_mc_sysfs.o edac_pci_sysfs.o
edac_core-objs	+= edac_module.o edac_device_sysfs.o

ifdef CONFIG_PCI
edac_core-objs	+= edac_pci.o edac_pci_sysfs.o
endif

obj-$(CONFIG_EDAC_AMD76X)		+= amd76x_edac.o
obj-$(CONFIG_EDAC_I5000)		+= i5000_edac.o
obj-$(CONFIG_EDAC_E7XXX)		+= e7xxx_edac.o
obj-$(CONFIG_EDAC_E752X)		+= e752x_edac.o
obj-$(CONFIG_EDAC_I82443BXGX)		+= i82443bxgx_edac.o
obj-$(CONFIG_EDAC_I82875P)		+= i82875p_edac.o
obj-$(CONFIG_EDAC_I3000)		+= i3000_edac.o
obj-$(CONFIG_EDAC_I82860)		+= i82860_edac.o
obj-$(CONFIG_EDAC_R82600)		+= r82600_edac.o
obj-$(CONFIG_EDAC_PASEMI)		+= pasemi_edac.o

