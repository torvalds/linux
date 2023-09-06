# SPDX-License-Identifier: ISC

obj-$(CONFIG_MT7921_COMMON) += mt7921-common.o
obj-$(CONFIG_MT7921E) += mt7921e.o
obj-$(CONFIG_MT7921S) += mt7921s.o
obj-$(CONFIG_MT7921U) += mt7921u.o

mt7921-common-y := mac.o mcu.o main.o init.o debugfs.o
mt7921-common-$(CONFIG_NL80211_TESTMODE) += testmode.o
mt7921e-y := pci.o pci_mac.o pci_mcu.o
mt7921s-y := sdio.o sdio_mac.o sdio_mcu.o
mt7921u-y := usb.o
