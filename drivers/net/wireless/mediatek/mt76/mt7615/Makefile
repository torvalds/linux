#SPDX-License-Identifier: ISC

obj-$(CONFIG_MT7615_COMMON) += mt7615-common.o
obj-$(CONFIG_MT7615E) += mt7615e.o

CFLAGS_trace.o := -I$(src)

mt7615-common-y := main.o init.o mcu.o eeprom.o mac.o \
		   debugfs.o trace.o

mt7615e-y := pci.o pci_init.o dma.o pci_mac.o mmio.o
mt7615e-$(CONFIG_MT7622_WMAC) += soc.o
