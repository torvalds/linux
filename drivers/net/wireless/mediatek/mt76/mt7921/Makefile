# SPDX-License-Identifier: ISC

obj-$(CONFIG_MT7921E) += mt7921e.o

CFLAGS_trace.o := -I$(src)

mt7921e-y := pci.o pci_mac.o pci_mcu.o mac.o mcu.o dma.o eeprom.o main.o \
	     init.o debugfs.o trace.o
mt7921e-$(CONFIG_NL80211_TESTMODE) += testmode.o
