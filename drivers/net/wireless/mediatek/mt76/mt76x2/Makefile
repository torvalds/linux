# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_MT76x2_COMMON) += mt76x2-common.o
obj-$(CONFIG_MT76x2E) += mt76x2e.o
obj-$(CONFIG_MT76x2U) += mt76x2u.o

mt76x2-common-y := \
	eeprom.o mac.o init.o phy.o mcu.o

mt76x2e-y := \
	pci.o pci_main.o pci_init.o pci_mcu.o \
	pci_phy.o

mt76x2u-y := \
	usb.o usb_init.o usb_main.o usb_mac.o usb_mcu.o \
	usb_phy.o

CFLAGS_pci_trace.o := -I$(src)
