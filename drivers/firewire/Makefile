#
# Makefile for the Linux IEEE 1394 implementation
#

fw-core-y += fw-card.o fw-topology.o fw-transaction.o fw-iso.o \
	fw-device.o fw-cdev.o

obj-$(CONFIG_FIREWIRE) += fw-core.o
obj-$(CONFIG_FIREWIRE_OHCI) += fw-ohci.o
obj-$(CONFIG_FIREWIRE_SBP2) += fw-sbp2.o
