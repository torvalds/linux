#
# Makefile for the Linux kernel EDAC drivers.
#
# Copyright 02 Jul 2003, Linux Networx (http://lnxi.com)
# This file may be distributed under the terms of the
# GNU General Public License.
#
# $Id: Makefile,v 1.4.2.3 2005/07/08 22:05:38 dsp_llnl Exp $


obj-$(CONFIG_EDAC_MM_EDAC)		+= edac_core.o

edac_core-objs	:= edac_mc.o edac_device.o edac_mc_sysfs.o edac_pci_sysfs.o

edac_core-objs	+= edac_module.o edac_device_sysfs.o

obj-$(CONFIG_EDAC_AMD76X)		+= amd76x_edac.o
obj-$(CONFIG_EDAC_I5000)		+= i5000_edac.o
obj-$(CONFIG_EDAC_E7XXX)		+= e7xxx_edac.o
obj-$(CONFIG_EDAC_E752X)		+= e752x_edac.o
obj-$(CONFIG_EDAC_I82875P)		+= i82875p_edac.o
obj-$(CONFIG_EDAC_I82860)		+= i82860_edac.o
obj-$(CONFIG_EDAC_R82600)		+= r82600_edac.o

