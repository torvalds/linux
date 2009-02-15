#
# Makefile for the orinoco wireless device drivers.
#
orinoco-objs := main.o fw.o hw.o mic.o scan.o wext.o hermes_dld.o hermes.o

obj-$(CONFIG_HERMES)		+= orinoco.o
obj-$(CONFIG_PCMCIA_HERMES)	+= orinoco_cs.o
obj-$(CONFIG_APPLE_AIRPORT)	+= airport.o
obj-$(CONFIG_PLX_HERMES)	+= orinoco_plx.o
obj-$(CONFIG_PCI_HERMES)	+= orinoco_pci.o
obj-$(CONFIG_TMD_HERMES)	+= orinoco_tmd.o
obj-$(CONFIG_NORTEL_HERMES)	+= orinoco_nortel.o
obj-$(CONFIG_PCMCIA_SPECTRUM)	+= spectrum_cs.o
