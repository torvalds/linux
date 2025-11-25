# SPDX-License-Identifier: BSD-3-Clause-Clear

obj-$(CONFIG_MT7925_COMMON) += mt7925-common.o
obj-$(CONFIG_MT7925E) += mt7925e.o
obj-$(CONFIG_MT7925U) += mt7925u.o

mt7925-common-y := mac.o mcu.o regd.o main.o init.o debugfs.o
mt7925-common-$(CONFIG_NL80211_TESTMODE) += testmode.o
mt7925e-y := pci.o pci_mac.o pci_mcu.o
mt7925u-y := usb.o
