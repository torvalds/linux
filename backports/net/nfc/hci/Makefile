#
# Makefile for the Linux NFC HCI layer.
#

obj-$(CONFIG_BACKPORT_NFC_HCI) += hci.o

hci-y			:= core.o hcp.o command.o llc.o llc_nop.o
hci-$(CONFIG_BACKPORT_NFC_SHDLC) += llc_shdlc.o
