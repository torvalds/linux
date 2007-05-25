#
# Makefile for the Linux IEEE 1394 implementation
#

firewire-core-y += fw-card.o fw-topology.o fw-transaction.o fw-iso.o \
                   fw-device.o fw-cdev.o
firewire-ohci-y += fw-ohci.o
firewire-sbp2-y += fw-sbp2.o

obj-$(CONFIG_FIREWIRE) += firewire-core.o
obj-$(CONFIG_FIREWIRE_OHCI) += firewire-ohci.o
obj-$(CONFIG_FIREWIRE_SBP2) += firewire-sbp2.o
