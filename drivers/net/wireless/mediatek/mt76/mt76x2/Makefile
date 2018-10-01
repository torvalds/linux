obj-$(CONFIG_MT76x2_COMMON) += mt76x2-common.o
obj-$(CONFIG_MT76x2E) += mt76x2e.o
obj-$(CONFIG_MT76x2U) += mt76x2u.o

mt76x2-common-y := \
	eeprom.o tx.o mac.o init.o common.o phy.o \
	debugfs.o mcu.o

mt76x2e-y := \
	pci.o pci_dma.o pci_main.o pci_init.o pci_tx.o \
	pci_core.o pci_mac.o pci_mcu.o pci_phy.o \
	pci_dfs.o pci_trace.o

mt76x2u-y := \
	usb.o usb_init.o usb_main.o usb_mac.o usb_mcu.o \
	usb_phy.o usb_core.o

CFLAGS_pci_trace.o := -I$(src)
