#
# Makefile for nfc devices
#

obj-$(CONFIG_PN544_HCI_NFC)	+= pn544_hci.o
obj-$(CONFIG_NFC_PN533)		+= pn533.o
obj-$(CONFIG_NFC_WILINK)	+= nfcwilink.o

ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
