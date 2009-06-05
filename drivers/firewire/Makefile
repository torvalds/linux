#
# Makefile for the Linux IEEE 1394 implementation
#

firewire-core-y += core-card.o core-cdev.o core-device.o \
                   core-iso.o core-topology.o core-transaction.o
firewire-ohci-y += ohci.o
firewire-sbp2-y += sbp2.o

obj-$(CONFIG_FIREWIRE) += firewire-core.o
obj-$(CONFIG_FIREWIRE_OHCI) += firewire-ohci.o
obj-$(CONFIG_FIREWIRE_SBP2) += firewire-sbp2.o
