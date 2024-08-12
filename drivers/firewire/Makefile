# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux IEEE 1394 implementation
#

firewire-core-y += core-trace.o core-card.o core-cdev.o core-device.o \
                   core-iso.o core-topology.o core-transaction.o
firewire-ohci-y += ohci.o
firewire-sbp2-y += sbp2.o
firewire-net-y  += net.o

obj-$(CONFIG_FIREWIRE)      += firewire-core.o
obj-$(CONFIG_FIREWIRE_OHCI) += firewire-ohci.o
obj-$(CONFIG_FIREWIRE_SBP2) += firewire-sbp2.o
obj-$(CONFIG_FIREWIRE_NET)  += firewire-net.o
obj-$(CONFIG_FIREWIRE_NOSY) += nosy.o
obj-$(CONFIG_PROVIDE_OHCI1394_DMA_INIT) += init_ohci1394_dma.o

obj-$(CONFIG_FIREWIRE_KUNIT_UAPI_TEST) += uapi-test.o
obj-$(CONFIG_FIREWIRE_KUNIT_PACKET_SERDES_TEST) += packet-serdes-test.o
obj-$(CONFIG_FIREWIRE_KUNIT_SELF_ID_SEQUENCE_HELPER_TEST) += self-id-sequence-helper-test.o
obj-$(CONFIG_FIREWIRE_KUNIT_OHCI_SERDES_TEST) += ohci-serdes-test.o
