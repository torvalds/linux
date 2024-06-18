# SPDX-License-Identifier: ISC

obj-$(CONFIG_MT7915E) += mt7915e.o

mt7915e-y := pci.o init.o dma.o eeprom.o main.o mcu.o mac.o \
	     debugfs.o mmio.o

mt7915e-$(CONFIG_NL80211_TESTMODE) += testmode.o
mt7915e-$(CONFIG_MT798X_WMAC) += soc.o
mt7915e-$(CONFIG_DEV_COREDUMP) += coredump.o
