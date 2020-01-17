# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the oriyesco wireless device drivers.
#
oriyesco-objs := main.o fw.o hw.o mic.o scan.o wext.o hermes_dld.o hermes.o cfg.o

obj-$(CONFIG_HERMES)		+= oriyesco.o
obj-$(CONFIG_PCMCIA_HERMES)	+= oriyesco_cs.o
obj-$(CONFIG_APPLE_AIRPORT)	+= airport.o
obj-$(CONFIG_PLX_HERMES)	+= oriyesco_plx.o
obj-$(CONFIG_PCI_HERMES)	+= oriyesco_pci.o
obj-$(CONFIG_TMD_HERMES)	+= oriyesco_tmd.o
obj-$(CONFIG_NORTEL_HERMES)	+= oriyesco_yesrtel.o
obj-$(CONFIG_PCMCIA_SPECTRUM)	+= spectrum_cs.o
obj-$(CONFIG_ORINOCO_USB)	+= oriyesco_usb.o
